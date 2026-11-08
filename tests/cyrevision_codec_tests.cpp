#include "cyrevision_codec.hpp"
#include <fstream>
#include <iostream>
#include <functional>
#include <stdexcept>

using namespace cysynck;
namespace cr = cysynck::cyrevision;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void refuses(const std::function<void()> &action, const char *message) {
    bool refused = false;
    try {
        action();
    } catch (const std::exception &) {
        refused = true;
    }
    require(refused, message);
}
int main(int argc, char **argv) {
    fs::path scratch;
    try {
        if (argc != 3)
            throw std::runtime_error("Pass fixture directory and codec or crypto");
        auto fixtures = fs::u8path(argv[1]);
        auto vectors = cr::parse(read_file(fixtures / "cyrevision-canonical.json"));
        auto inventory_vector = cr::parse(read_file(fixtures / "cyrevision-inventory.json"));
        if (std::string(argv[2]) == "codec") {
            auto escaping = cr::parse(read_file(fixtures / "dotnet-json-escaping.json"));
            require(cr::base64_encode(cr::canonical_inventory(
                        inventory_vector.at("document").at("inventory"), true)) ==
                        inventory_vector.at("canonical").get<std::string>(),
                    "Real CyRevision LFS canonical bytes differ");
            for (auto &entry : escaping)
                require(cr::encode_string(entry.at("value").get<std::string>()) ==
                            entry.at("encoded").get<std::string>(),
                        "System.Text.Json escaping differs");
            for (auto &entry : vectors) {
                auto manifest = entry.at("document").at("manifest");
                auto expected = entry.at("canonical").get<std::string>();
                require(cr::base64_encode(cr::canonical_bundle(manifest, true)) == expected,
                        "Real CyRevision Windows canonical bytes differ");
                auto invalid = manifest;
                invalid["createdAt"] = "2026-02-29T00:00:00+00:00";
                refuses([&] { cr::canonical_bundle(invalid); },
                        "Impossible publication date accepted");
                invalid = manifest;
                invalid["createdAt"] = "0001-01-01T00:00:00+14:00";
                refuses([&] { cr::canonical_bundle(invalid); }, "Out-of-range UTC date accepted");
                invalid = manifest;
                invalid["bundleFileName"] = "../private.key";
                refuses([&] { cr::canonical_bundle(invalid); }, "Escaping bundle path accepted");
                invalid = manifest;
                invalid["sequence"] = uint64_t(-1);
                refuses([&] { cr::canonical_bundle(invalid); },
                        "Overflowing publication sequence accepted");
                invalid = manifest;
                invalid["unexpected"] = true;
                refuses([&] { cr::canonical_bundle(invalid); },
                        "Unknown unsigned semantics accepted");
                invalid = manifest;
                invalid["author"]["privateKey"] = "not a key";
                refuses([&] { cr::canonical_bundle(invalid); },
                        "Private key field accepted in public identity");
            }
            refuses([] { cr::parse("{\"manifest\":{},\"manifest\":{}}"); },
                    "Duplicate manifest property accepted");
            refuses([] { cr::parse(std::string(100, '[') + std::string(100, ']')); },
                    "Unbounded JSON nesting accepted");
            auto encoded = cr::base64_encode(std::string(91, '\0'));
            require(cr::base64_decode(encoded, 91) == std::string(91, '\0'),
                    "Binary base64 roundtrip failed");
            encoded[encoded.size() - 3] = 'B';
            refuses([&] { cr::base64_decode(encoded, 91); },
                    "Noncanonical base64 padding accepted");
            std::cout << "PASS System.Text.Json escaping, CyRevision canonical records and "
                         "metadata refusal cases\n";
            return 0;
        }
        if (std::string(argv[2]) != "crypto")
            throw std::runtime_error("Unknown test selection");
        scratch = fs::temp_directory_path() / ("cysynck-cyrevision-codec-" + random_id());
        require(fs::create_directory(scratch), "Cannot reserve crypto test directory");
        auto inventory_document = inventory_vector.at("document");
        require(cr::verify_inventory(inventory_document,
                                     inventory_document.at("inventory")
                                         .at("device")
                                         .at("signingPublicKey")
                                         .get<std::string>(),
                                     scratch),
                "Real .NET signed LFS inventory did not verify");
        for (auto &entry : vectors) {
            auto document = entry.at("document");
            auto payload = cr::canonical_bundle(document.at("manifest"), true);
            auto key =
                document.at("manifest").at("author").at("signingPublicKey").get<std::string>();
            auto signature = document.at("signature").get<std::string>();
            require(cr::verify(key, payload, signature, scratch),
                    "Real .NET P1363 signature did not verify");
            require(cr::verify_bundle(document, key, scratch),
                    "Legacy Windows publication did not verify through compatibility reader");
            require(!cr::verify(key, payload + " ", signature, scratch),
                    "Changed publication payload verified");
            auto raw = cr::base64_decode(signature, 64);
            raw[0] ^= 1;
            require(!cr::verify(key, payload, cr::base64_encode(raw), scratch),
                    "Altered ECDSA signature verified");
        }
        auto private_key = scratch / "signing.pem";
        cr::generate_key(private_key);
        auto key_hash = sha256_file(private_key);
        refuses([&] { cr::generate_key(private_key); }, "Existing signing identity overwritten");
        require(sha256_file(private_key) == key_hash, "Refused key replacement changed its bytes");
        auto key = cr::public_key(private_key);
        require(cr::fingerprint(key).size() == 64, "Invalid public key fingerprint");
        std::string payload = "Native UTF-8 payload: \xc3\xa9";
        auto signature = cr::sign(private_key, payload);
        require(cr::verify(key, payload, signature, scratch),
                "Native ECDSA signature did not verify");
        cr::generate_key(scratch / "other.pem");
        require(!cr::verify(cr::public_key(scratch / "other.pem"), payload, signature, scratch),
                "Different public key verified a signature");
        auto manifest = vectors[0].at("document").at("manifest");
        manifest["author"]["signingPublicKey"] = key;
        auto envelope = cr::parse(cr::signed_bundle(manifest, private_key));
        require(cr::verify(key, cr::canonical_bundle(envelope.at("manifest")),
                           envelope.at("signature"), scratch),
                "Native signed envelope is inconsistent");
        require(cr::verify_bundle(envelope, key, scratch),
                "Canonical LF publication did not verify");
        auto windows = cr::parse(cr::signed_bundle(manifest, private_key, true));
        require(cr::verify_bundle(windows, key, scratch),
                "Legacy-target publication did not verify");
        require(!cr::verify_bundle(envelope, cr::public_key(scratch / "other.pem"), scratch),
                "Unpinned publication key accepted");
        manifest["author"]["signingPublicKey"] = cr::public_key(scratch / "other.pem");
        refuses([&] { cr::signed_bundle(manifest, private_key); },
                "An author/key mismatch was signed");
        for (auto &entry : fs::directory_iterator(scratch))
            require(path_string(entry.path().filename()).find(".cr-") != 0,
                    "Crypto scratch files leaked");
        fs::remove_all(scratch);
        scratch.clear();
        std::cout << "PASS real .NET ECDSA signatures, native signing, pinned-key rejection and "
                     "private-key preservation\n";
        return 0;
    } catch (const std::exception &error) {
        if (!scratch.empty()) {
            std::error_code ignored;
            fs::remove_all(scratch, ignored);
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
