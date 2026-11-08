#include "cyrevision_codec.hpp"
#include "backup.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace cysynck::cyrevision {
namespace {
constexpr std::string_view alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::array<unsigned char, 27> p256_spki_prefix = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06,
    0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04};
void fields(const Document &value, std::initializer_list<const char *> required,
            std::initializer_list<const char *> optional = {}) {
    if (!value.is_object())
        throw std::runtime_error("CyRevision metadata must be an object");
    std::set<std::string> allowed;
    for (auto name : required) {
        if (!value.contains(name))
            throw std::runtime_error("Incomplete CyRevision metadata");
        allowed.insert(name);
    }
    for (auto name : optional)
        allowed.insert(name);
    for (auto &[name, item] : value.items())
        if (!allowed.contains(name))
            throw std::runtime_error("Unsupported CyRevision metadata field");
}
std::string text(const Document &value, size_t maximum, bool empty = false) {
    auto result = value.get<std::string>();
    if ((!empty && result.empty()) || result.size() > maximum ||
        result.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid CyRevision metadata string");
    return result;
}
std::string guid(const Document &value) {
    auto result = text(value, 36);
    if (!std::regex_match(
            result,
            std::regex(
                "[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}")) ||
        result == "00000000-0000-0000-0000-000000000000")
        throw std::runtime_error("Invalid CyRevision GUID");
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}
std::string date(const Document &value) {
    auto result = text(value, 40);
    std::smatch m;
    if (!std::regex_match(result, m,
                          std::regex("([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]"
                                     "{2})(\\.[0-9]{1,7})?(Z|[+-][0-9]{2}:[0-9]{2})")))
        throw std::runtime_error("Invalid CyRevision publication date");
    int year = std::stoi(m[1]), month = std::stoi(m[2]), day = std::stoi(m[3]);
    auto calendar = std::chrono::year(year) / std::chrono::month(month) / std::chrono::day(day);
    int hour = std::stoi(m[4]), minute = std::stoi(m[5]), second = std::stoi(m[6]);
    auto zone = m[8].str();
    int offset_hour = zone == "Z" ? 0 : std::stoi(zone.substr(1, 2));
    int offset_minute = zone == "Z" ? 0 : std::stoi(zone.substr(4, 2));
    if (year < 1 || !calendar.ok() || hour > 23 || minute > 59 || second > 59 || offset_hour > 14 ||
        offset_minute > 59 || (offset_hour == 14 && offset_minute))
        throw std::runtime_error("Invalid CyRevision publication date");
    int offset = (offset_hour * 60 + offset_minute) * (zone.front() == '-' ? -1 : 1);
    auto utc = std::chrono::sys_days(calendar) + std::chrono::hours(hour) +
               std::chrono::minutes(minute - offset) + std::chrono::seconds(second);
    auto utc_day = std::chrono::year_month_day(std::chrono::floor<std::chrono::days>(utc));
    if (int(utc_day.year()) < 1 || int(utc_day.year()) > 9999)
        throw std::runtime_error("CyRevision publication date is outside the supported range");
    auto fraction = m[7].str();
    while (!fraction.empty() && fraction.back() == '0')
        fraction.pop_back();
    if (fraction == ".")
        fraction.clear();
    return result.substr(0, 19) + fraction + (offset == 0 ? "+00:00" : zone);
}
std::string key_bytes(const std::string &key) {
    auto raw = base64_decode(key, 91);
    if (!std::equal(p256_spki_prefix.begin(), p256_spki_prefix.end(),
                    reinterpret_cast<const unsigned char *>(raw.data())))
        throw std::runtime_error("CyRevision requires a P-256 SPKI public key");
    return raw;
}
std::string render(const Document &value, int indent = 0, const std::string &path = "") {
    if (value.is_string()) {
        if (path == "/createdAt")
            return "\"" + date(value) + "\"";
        return encode_string(value.get<std::string>());
    }
    if (value.is_object() || value.is_array()) {
        bool object = value.is_object();
        if (value.empty())
            return object ? "{}" : "[]";
        std::string result = object ? "{\n" : "[\n";
        size_t index = 0;
        for (auto it = value.begin(); it != value.end(); ++it, ++index) {
            result += std::string(indent + 2, ' ');
            if (object)
                result += encode_string(it.key()) + ": ";
            auto child = path + "/" + (object ? it.key() : std::to_string(index));
            result += render(it.value(), indent + 2, child);
            result += index + 1 == value.size() ? "\n" : ",\n";
        }
        return result + std::string(indent, ' ') + (object ? "}" : "]");
    }
    if (value.is_number_integer() || value.is_boolean())
        return value.dump();
    throw std::runtime_error("Unsupported CyRevision canonical value");
}
class Scratch {
    fs::path directory_;

  public:
    explicit Scratch(const fs::path &parent) {
        plain_directory(parent);
        // Leave room for atomic_write's private temporary suffix on Windows.
        directory_ = parent / (".cr-" + random_id().substr(0, 16));
        if (!fs::create_directory(directory_))
            throw std::runtime_error("Cannot reserve crypto scratch directory");
#ifndef _WIN32
        fs::permissions(directory_, fs::perms::owner_all, fs::perm_options::replace);
#endif
    }
    fs::path file(const char *name) const { return directory_ / name; }
    ~Scratch() {
        std::error_code error;
        for (auto name : {"private.pem", "public.pem", "signature.der"})
            fs::remove(file(name), error);
        // Preserve any unknown file; never recursively remove a supplied path.
        fs::remove(directory_, error);
    }
};
std::string p1363(const std::string &der) {
    if (der.size() < 8 || der.size() > 72 || static_cast<unsigned char>(der[0]) != 0x30 ||
        static_cast<unsigned char>(der[1]) != der.size() - 2)
        throw std::runtime_error("Invalid ECDSA DER signature");
    size_t cursor = 2;
    std::string result;
    for (int index = 0; index != 2; ++index) {
        if (cursor + 2 > der.size() || der[cursor++] != 0x02)
            throw std::runtime_error("Invalid ECDSA DER integer");
        size_t size = static_cast<unsigned char>(der[cursor++]);
        if (!size || size > 33 || cursor + size > der.size() ||
            (static_cast<unsigned char>(der[cursor]) & 0x80))
            throw std::runtime_error("Invalid ECDSA DER integer");
        if (size > 1 && der[cursor] == 0) {
            if (!(static_cast<unsigned char>(der[cursor + 1]) & 0x80))
                throw std::runtime_error("Noncanonical ECDSA DER integer");
            ++cursor;
            --size;
        }
        if (size > 32)
            throw std::runtime_error("ECDSA scalar exceeds P-256 bounds");
        result.append(32 - size, '\0');
        result.append(der, cursor, size);
        cursor += size;
    }
    if (cursor != der.size())
        throw std::runtime_error("Trailing ECDSA signature data");
    return result;
}
std::string der_signature(const std::string &raw) {
    std::string body;
    for (size_t start : {size_t(0), size_t(32)}) {
        size_t first = start;
        while (first + 1 < start + 32 && raw[first] == 0)
            ++first;
        std::string scalar = raw.substr(first, start + 32 - first);
        if (static_cast<unsigned char>(scalar[0]) & 0x80)
            scalar.insert(scalar.begin(), '\0');
        body += '\x02';
        body += static_cast<char>(scalar.size());
        body += scalar;
    }
    return std::string("\x30", 1) + static_cast<char>(body.size()) + body;
}
} // namespace

Document parse(const std::string &bytes) {
    if (bytes.size() > 1024 * 1024)
        throw std::runtime_error("CyRevision metadata exceeds 1 MiB");
    std::vector<std::set<std::string>> keys;
    return Document::parse(bytes, [&](int depth, Document::parse_event_t event, Document &value) {
        if (depth > 32)
            throw std::runtime_error("CyRevision metadata nesting exceeds its limit");
        if (event == Document::parse_event_t::object_start)
            keys.emplace_back();
        if (event == Document::parse_event_t::key &&
            !keys.back().insert(value.get<std::string>()).second)
            throw std::runtime_error("Duplicate CyRevision JSON property");
        if (event == Document::parse_event_t::object_end)
            keys.pop_back();
        return true;
    });
}
std::string normalize_guid(const std::string &value) { return guid(value); }
std::string encode_string(const std::string &value) {
    auto encoded = Document(value).dump(-1, ' ', true);
    std::string result = "\"";
    constexpr char hex[] = "0123456789ABCDEF";
    for (size_t i = 1; i + 1 < encoded.size(); ++i) {
        unsigned char ch = encoded[i];
        if (ch == '\\') {
            char escaped = encoded[++i];
            if (escaped == '"')
                result += "\\u0022";
            else if (escaped == 'u') {
                result += "\\u";
                for (int digit = 0; digit != 4; ++digit)
                    result +=
                        static_cast<char>(std::toupper(static_cast<unsigned char>(encoded[++i])));
            } else {
                result += '\\';
                result += escaped;
            }
        } else if (ch == '&' || ch == '\'' || ch == '+' || ch == '<' || ch == '>' || ch == '`' ||
                   ch == 127) {
            result += "\\u00";
            result += hex[ch >> 4];
            result += hex[ch & 15];
        } else
            result += ch;
    }
    return result + "\"";
}
std::string base64_encode(std::string_view value) {
    std::string result;
    for (size_t i = 0; i < value.size(); i += 3) {
        unsigned a = static_cast<unsigned char>(value[i]);
        unsigned b = i + 1 < value.size() ? static_cast<unsigned char>(value[i + 1]) : 0;
        unsigned c = i + 2 < value.size() ? static_cast<unsigned char>(value[i + 2]) : 0;
        result += alphabet[a >> 2];
        result += alphabet[((a & 3) << 4) | (b >> 4)];
        result += i + 1 < value.size() ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
        result += i + 2 < value.size() ? alphabet[c & 63] : '=';
    }
    return result;
}
std::string base64_decode(const std::string &value, size_t expected_size) {
    if (value.size() != ((expected_size + 2) / 3) * 4)
        throw std::runtime_error("Invalid CyRevision base64 length");
    std::string result;
    unsigned bits = 0;
    int available = 0;
    for (char ch : value) {
        if (ch == '=')
            break;
        auto digit = alphabet.find(ch);
        if (digit == std::string_view::npos)
            throw std::runtime_error("Invalid CyRevision base64 character");
        bits = (bits << 6) | static_cast<unsigned>(digit);
        available += 6;
        if (available >= 8) {
            available -= 8;
            result += static_cast<char>((bits >> available) & 255);
        }
    }
    if (result.size() != expected_size || base64_encode(result) != value)
        throw std::runtime_error("Noncanonical CyRevision base64");
    return result;
}
Document identity(const Document &value) {
    fields(value, {"userId", "deviceId", "displayName", "syncthingDeviceId", "signingPublicKey"});
    auto key = text(value.at("signingPublicKey"), 124);
    key_bytes(key);
    return {{"userId", guid(value.at("userId"))},
            {"deviceId", guid(value.at("deviceId"))},
            {"displayName", text(value.at("displayName"), 160)},
            {"syncthingDeviceId", text(value.at("syncthingDeviceId"), 128, true)},
            {"signingPublicKey", key}};
}
Document bundle_manifest(const Document &value) {
    fields(value,
           {"transactionId", "projectId", "author", "createdAt", "bundleFileName", "bundleHash",
            "branches"},
           {"sequence"});
    if (value.at("bundleFileName") != "repository.bundle")
        throw std::runtime_error("Unsupported CyRevision bundle filename");
    auto hash = text(value.at("bundleHash"), 64);
    if (!std::regex_match(hash, std::regex("[0-9a-fA-F]{64}")))
        throw std::runtime_error("Invalid CyRevision bundle hash");
    auto branches = value.at("branches");
    if (!branches.is_object() || branches.empty() || branches.size() > 1024)
        throw std::runtime_error("CyRevision bundle must have 1 to 1024 branches");
    for (auto &[name, oid] : branches.items()) {
        if (name.empty() || name.size() > 1024 || name.find('\0') != std::string::npos ||
            !std::regex_match(text(oid, 64), std::regex("([0-9a-f]{40}|[0-9a-f]{64})")))
            throw std::runtime_error("Invalid CyRevision branch entry");
    }
    Document result = {{"transactionId", guid(value.at("transactionId"))},
                       {"projectId", guid(value.at("projectId"))},
                       {"author", identity(value.at("author"))},
                       {"createdAt", date(value.at("createdAt"))},
                       {"bundleFileName", "repository.bundle"},
                       {"bundleHash", hash},
                       {"branches", branches}};
    if (value.contains("sequence")) {
        if (!value.at("sequence").is_number_integer())
            throw std::runtime_error("Invalid CyRevision sequence");
        auto number = value.at("sequence").get<int64_t>();
        if (number < 0 || (value.at("sequence").is_number_unsigned() &&
                           value.at("sequence").get<uint64_t>() > uint64_t(INT64_MAX)))
            throw std::runtime_error("Invalid CyRevision sequence");
        if (number)
            result["sequence"] = number;
    }
    return result;
}
Document content_inventory(const Document &value) {
    fields(value, {"inventoryId", "projectId", "device", "createdAt", "lfsObjects"});
    const auto &objects = value.at("lfsObjects");
    if (!objects.is_array() || objects.size() > 4096)
        throw std::runtime_error("CyRevision LFS inventory exceeds its object limit");
    Document items = Document::array();
    std::set<std::string> seen;
    for (const auto &object : objects) {
        fields(object, {"oidSha256", "size", "priority", "paths", "publishedToExchange"});
        auto oid = text(object.at("oidSha256"), 64);
        const auto &size = object.at("size"), &priority = object.at("priority");
        if (!std::regex_match(oid, std::regex("[0-9a-f]{64}")) || !seen.insert(oid).second ||
            !size.is_number_integer() ||
            (size.is_number_unsigned() && size.get<uint64_t>() > uint64_t(INT64_MAX)) ||
            size.get<int64_t>() < 0 || !priority.is_number_integer() || priority < 0 ||
            priority > 3 || !object.at("publishedToExchange").is_boolean() ||
            !object.at("paths").is_array() || object.at("paths").size() > 1024)
            throw std::runtime_error("Invalid CyRevision LFS inventory object");
        for (const auto &path : object.at("paths"))
            (void)text(path, 4096);
        items.push_back({{"oidSha256", oid},
                         {"size", size},
                         {"priority", priority},
                         {"paths", object.at("paths")},
                         {"publishedToExchange", object.at("publishedToExchange")}});
    }
    return {{"inventoryId", guid(value.at("inventoryId"))},
            {"projectId", guid(value.at("projectId"))},
            {"device", identity(value.at("device"))},
            {"createdAt", date(value.at("createdAt"))},
            {"lfsObjects", items}};
}
namespace {
std::string canonical(const Document &value, bool legacy_windows) {
    auto payload = render(value);
    if (payload.size() > 1024 * 1024)
        throw std::runtime_error("CyRevision canonical metadata exceeds 1 MiB");
    if (legacy_windows) {
        std::string windows;
        for (char ch : payload) {
            if (ch == '\n')
                windows += '\r';
            windows += ch;
        }
        return windows;
    }
    return payload;
}
} // namespace
std::string canonical_bundle(const Document &value, bool legacy_windows) {
    return canonical(bundle_manifest(value), legacy_windows);
}
std::string canonical_inventory(const Document &value, bool legacy_windows) {
    return canonical(content_inventory(value), legacy_windows);
}
std::string fingerprint(const std::string &key) { return sha256(key_bytes(key)); }
std::string openssl_tool() {
    auto configured = env("CYSYNCK_OPENSSL");
    if (!configured.empty())
        return configured;
#ifdef _WIN32
    for (auto suffix : {"/Git/mingw64/bin/openssl.exe", "/Git/usr/bin/openssl.exe"}) {
        auto candidate = fs::u8path(env("ProgramFiles", "C:/Program Files") + suffix);
        if (fs::is_regular_file(candidate))
            return path_string(candidate);
    }
#elif defined(__APPLE__)
    for (auto path :
         {"/opt/homebrew/opt/openssl@3/bin/openssl", "/usr/local/opt/openssl@3/bin/openssl"})
        if (fs::is_regular_file(path))
            return path;
#endif
    return "openssl";
}
void generate_key(const fs::path &private_key) {
    if (fs::exists(private_key) || path_is_link(private_key))
        throw std::runtime_error("Preserve the existing CyRevision signing key");
    Scratch scratch(private_key.parent_path());
    auto temporary = scratch.file("private.pem");
    auto generated = process({openssl_tool(), "genpkey", "-algorithm", "EC", "-pkeyopt",
                              "ec_paramgen_curve:prime256v1", "-out", path_string(temporary)},
                             {}, {}, 30);
    if (generated.code)
        throw std::runtime_error("P-256 key generation failed; configure CYSYNCK_OPENSSL");
    atomic_write(private_key, read_file(temporary, 16384), true);
    (void)public_key(private_key);
}
std::string public_key(const fs::path &private_key) {
    if (path_is_link(private_key) || !fs::is_regular_file(private_key))
        throw std::runtime_error("CyRevision private key is missing or linked");
    auto result = process(
        {openssl_tool(), "pkey", "-in", path_string(private_key), "-pubout", "-outform", "DER"}, {},
        {}, 30);
    if (result.code)
        throw std::runtime_error("Cannot derive CyRevision signing public key");
    auto key = base64_encode(result.out);
    key_bytes(key);
    return key;
}
std::string sign(const fs::path &private_key, const std::string &payload) {
    (void)public_key(private_key);
    auto result = process({openssl_tool(), "dgst", "-sha256", "-sign", path_string(private_key)},
                          {}, payload, 30);
    if (result.code)
        throw std::runtime_error("CyRevision publication signing failed");
    return base64_encode(p1363(result.out));
}
bool verify(const std::string &key, const std::string &payload, const std::string &signature,
            const fs::path &scratch_parent) {
    key_bytes(key);
    auto der = der_signature(base64_decode(signature, 64));
    Scratch scratch(scratch_parent);
    auto key_file = scratch.file("public.pem"), signature_file = scratch.file("signature.der");
    std::string pem = "-----BEGIN PUBLIC KEY-----\n";
    for (size_t i = 0; i < key.size(); i += 64)
        pem += key.substr(i, 64) + "\n";
    pem += "-----END PUBLIC KEY-----\n";
    atomic_write(key_file, pem);
    atomic_write(signature_file, der);
    auto result = process({openssl_tool(), "dgst", "-sha256", "-verify", path_string(key_file),
                           "-signature", path_string(signature_file)},
                          {}, payload, 30);
    return result.code == 0;
}
bool verify_bundle(const Document &document, const std::string &expected_key,
                   const fs::path &scratch_parent) {
    fields(document, {"manifest", "signature"});
    auto manifest = bundle_manifest(document.at("manifest"));
    if (manifest.at("author").at("signingPublicKey") != expected_key)
        return false;
    auto signature = text(document.at("signature"), 88);
    // CyRevision's original indented serializer uses OS-dependent line endings.
    // Both encodings bind the same typed fields; never weaken key/signature checks.
    return verify(expected_key, canonical_bundle(manifest), signature, scratch_parent) ||
           verify(expected_key, canonical_bundle(manifest, true), signature, scratch_parent);
}
std::string signed_bundle(const Document &manifest, const fs::path &private_key,
                          bool legacy_windows) {
    auto validated = bundle_manifest(manifest);
    if (validated.at("author").at("signingPublicKey") != public_key(private_key))
        throw std::runtime_error("Publication author does not match the private key");
    auto payload = canonical_bundle(validated, legacy_windows);
    return "{\n  \"manifest\": " + render(validated, 2) +
           ",\n  \"signature\": " + encode_string(sign(private_key, payload)) + "\n}";
}
bool verify_inventory(const Document &document, const std::string &expected_key,
                      const fs::path &scratch_parent) {
    fields(document, {"inventory", "signature"});
    auto inventory = content_inventory(document.at("inventory"));
    if (inventory.at("device").at("signingPublicKey") != expected_key)
        return false;
    auto signature = text(document.at("signature"), 88);
    return verify(expected_key, canonical_inventory(inventory), signature, scratch_parent) ||
           verify(expected_key, canonical_inventory(inventory, true), signature, scratch_parent);
}
std::string signed_inventory(const Document &inventory, const fs::path &private_key,
                             bool legacy_windows) {
    auto validated = content_inventory(inventory);
    if (validated.at("device").at("signingPublicKey") != public_key(private_key))
        throw std::runtime_error("Inventory device does not match the private key");
    return "{\n  \"inventory\": " + render(validated, 2) + ",\n  \"signature\": " +
           encode_string(sign(private_key, canonical_inventory(validated, legacy_windows))) + "\n}";
}
} // namespace cysynck::cyrevision
