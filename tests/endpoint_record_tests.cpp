#include "endpoint_record.hpp"
#include "cyrevision_codec.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace cysynck;
namespace crypto = cysynck::cyrevision;
void check(bool value, const char *message) {
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
    check(refused, message);
}
int main() {
    auto root = fs::temp_directory_path() / fs::u8path("cysynck-réseau-世界-" + random_id());
    try {
        check(fs::create_directory(root), "Cannot reserve endpoint-signature fixture");
        auto key = root / "signing.pem";
        crypto::generate_key(key);
        crypto::generate_key(root / "other.pem");
        auto other_public = crypto::public_key(root / "other.pem");
        EndpointIdentity identity{
            "test-project",          "test-peer",          std::string(43, 'A') + "=",
            crypto::public_key(key), std::string(56, 'A'), 51820};
        int64_t now = 1700000000;
        auto signed_record =
            sign_endpoint_record(identity, "198.18.0.10:45000", "pcp", 1, now - 1, now + 60, key);
        auto verified = verify_endpoint_record(signed_record, identity, now, root);
        check(verified.at("metadata").at("endpoint") == "198.18.0.10:45000" &&
                  verified.at("canonical_hash").get<std::string>().size() == 64,
              "Endpoint record did not preserve its assigned external UDP endpoint");
        for (auto field :
             {"project", "peer", "wireguard_key", "signing_key", "device_id", "listen_port",
              "endpoint", "source", "sequence", "issued_at", "expires_at"}) {
            auto changed = Json::parse(signed_record);
            if (std::string(field) == "listen_port")
                changed["metadata"][field] = 51821;
            else if (std::string(field) == "sequence")
                changed["metadata"][field] = 2;
            else if (std::string(field) == "issued_at")
                changed["metadata"][field] = now;
            else if (std::string(field) == "expires_at")
                changed["metadata"][field] = now + 59;
            else if (std::string(field) == "endpoint")
                changed["metadata"][field] = "198.18.0.99:45000";
            else if (std::string(field) == "source")
                changed["metadata"][field] = "nat-pmp";
            else if (std::string(field) == "wireguard_key")
                changed["metadata"][field] = std::string(42, 'B') + "A=";
            else if (std::string(field) == "signing_key")
                changed["metadata"][field] = other_public;
            else
                changed["metadata"][field] = "altered";
            refuses([&] { verify_endpoint_record(changed.dump(), identity, now, root); },
                    "An altered endpoint field verified");
        }
        auto wrong = identity;
        wrong.project = "another-project";
        refuses([&] { verify_endpoint_record(signed_record, wrong, now, root); },
                "A publication from another project was accepted");
        wrong = identity;
        wrong.peer = "another-peer";
        refuses([&] { verify_endpoint_record(signed_record, wrong, now, root); },
                "A publication from another device was accepted");
        wrong = identity;
        wrong.signing_key = other_public;
        refuses([&] { verify_endpoint_record(signed_record, wrong, now, root); },
                "An independently unpinned signing key was accepted");
        refuses(
            [&] {
                sign_endpoint_record(identity, "198.18.0.10:45000", "pcp", 1, now, now + 60,
                                     root / "other.pem");
            },
            "A wrong publishing private key was used");
        refuses([&] { verify_endpoint_record(signed_record, identity, now + 60, root); },
                "An expired endpoint was accepted");
        auto future = sign_endpoint_record(identity, "198.18.0.10:45000", "nat-pmp", 2, now + 121,
                                           now + 180, key);
        refuses([&] { verify_endpoint_record(future, identity, now, root); },
                "An endpoint published too far in the future was accepted");
        refuses(
            [&] {
                sign_endpoint_record(identity, "198.18.0.10:45000", "manual", 0, now, now + 60,
                                     key);
            },
            "A zero publication sequence was signed");
        refuses(
            [&] {
                sign_endpoint_record(identity, "198.18.0.10:45000", "manual", 1, now, now + 3601,
                                     key);
            },
            "An unbounded endpoint lifetime was signed");
        refuses(
            [&] {
                sign_endpoint_record(identity, "host:45000\nPostUp = bad", "manual", 1, now,
                                     now + 60, key);
            },
            "A configuration-injecting endpoint was signed");
        auto changed = Json::parse(signed_record);
        changed["metadata"]["private_nonce"] = "must-not-be-public";
        refuses([&] { verify_endpoint_record(changed.dump(), identity, now, root); },
                "Unknown private/unsigned fields were accepted");
        changed = Json::parse(signed_record);
        changed["signature"] = crypto::sign(key, changed.at("metadata").dump(-1, ' ', true));
        refuses([&] { verify_endpoint_record(changed.dump(), identity, now, root); },
                "A signature without the endpoint protocol domain was accepted");
        auto duplicate = signed_record.substr(0, signed_record.size() - 1) +
                         ",\"signature\":" + Json::parse(signed_record).at("signature").dump() +
                         "}";
        refuses([&] { verify_endpoint_record(duplicate, identity, now, root); },
                "A duplicate signature property was accepted");
        fs::remove_all(root);
        std::cout << "PASS endpoint signatures, assigned UDP ports, project/device/key binding, "
                     "expiry, clock and protocol-domain checks on a Unicode profile path\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\nOwned fixture preserved: " << path_string(root) << '\n';
        return 1;
    }
}
