#include "endpoint_record.hpp"
#include "cyrevision_codec.hpp"
#include <regex>
#include <set>
#include <stdexcept>

namespace cysynck {
namespace {
namespace crypto = cyrevision; // Reuse the existing independently tested P-256 primitives.
constexpr const char *domain = "CySynck/WireGuardEndpoint/1\n";
void fields(const Json &value, const std::set<std::string> &names) {
    if (!value.is_object() || value.size() != names.size())
        throw std::runtime_error("Invalid endpoint record fields");
    for (auto name : names)
        if (!value.contains(name))
            throw std::runtime_error("Incomplete endpoint record");
}
std::string string(const Json &value, size_t maximum, bool empty = false) {
    auto result = value.get<std::string>();
    if ((!empty && result.empty()) || result.size() > maximum ||
        result.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid endpoint record string");
    return result;
}
int64_t positive(const Json &value) {
    if (!value.is_number_integer() ||
        (value.is_number_unsigned() && value.get<uint64_t>() > uint64_t(INT64_MAX)) ||
        value.get<int64_t>() < 1)
        throw std::runtime_error(
            "Endpoint counters and times must be positive signed 64-bit integers");
    return value.get<int64_t>();
}
void identity_valid(const EndpointIdentity &identity) {
    for (const auto &id : {identity.project, identity.peer})
        if (!std::regex_match(id, std::regex("[A-Za-z0-9][A-Za-z0-9_-]{0,79}")))
            throw std::runtime_error("Invalid endpoint project/peer identity");
    if (!std::regex_match(identity.wireguard_key,
                          std::regex("[A-Za-z0-9+/]{42}[AEIMQUYcgkosw048]=")) ||
        identity.device_id.size() > 128 || identity.device_id.find('\0') != std::string::npos ||
        identity.listen_port < 1 || identity.listen_port > 65535)
        throw std::runtime_error("Invalid pinned endpoint identity");
    (void)crypto::fingerprint(identity.signing_key);
}
Json metadata(const Json &value) {
    fields(value, {"version", "project", "peer", "wireguard_key", "signing_key", "device_id",
                   "listen_port", "endpoint", "source", "sequence", "issued_at", "expires_at"});
    if (!value.at("version").is_number_integer() || value.at("version") != 1)
        throw std::runtime_error("Unsupported endpoint record version");
    EndpointIdentity identity{
        string(value.at("project"), 80),          string(value.at("peer"), 80),
        string(value.at("wireguard_key"), 44),    string(value.at("signing_key"), 124),
        string(value.at("device_id"), 128, true), 0};
    auto port = positive(value.at("listen_port"));
    if (port > 65535)
        throw std::runtime_error("Invalid endpoint listener port");
    identity.listen_port = static_cast<int>(port);
    identity_valid(identity);
    auto endpoint = string(value.at("endpoint"), 512), source = string(value.at("source"), 32);
    std::smatch match;
    if (!std::regex_match(endpoint, match,
                          std::regex("([A-Za-z0-9.-]+|\\[[A-Fa-f0-9:]+\\]):([0-9]{1,5})")) ||
        std::stoi(match[2]) < 1 || std::stoi(match[2]) > 65535)
        throw std::runtime_error("Invalid signed WireGuard endpoint");
    if (source != "manual" && source != "pcp" && source != "nat-pmp")
        throw std::runtime_error("Unsupported signed endpoint source");
    auto sequence = positive(value.at("sequence")), issued = positive(value.at("issued_at")),
         expires = positive(value.at("expires_at"));
    if (expires <= issued || expires - issued > 3600)
        throw std::runtime_error("Endpoint publication lifetime must be at most one hour");
    return {{"version", 1},
            {"project", identity.project},
            {"peer", identity.peer},
            {"wireguard_key", identity.wireguard_key},
            {"signing_key", identity.signing_key},
            {"device_id", identity.device_id},
            {"listen_port", identity.listen_port},
            {"endpoint", endpoint},
            {"source", source},
            {"sequence", sequence},
            {"issued_at", issued},
            {"expires_at", expires}};
}
std::string canonical(const Json &value) { return std::string(domain) + value.dump(-1, ' ', true); }
} // namespace
std::string sign_endpoint_record(const EndpointIdentity &identity, const std::string &endpoint,
                                 const std::string &source, int64_t sequence, int64_t issued,
                                 int64_t expires, const fs::path &key) {
    identity_valid(identity);
    auto value = metadata({{"version", 1},
                           {"project", identity.project},
                           {"peer", identity.peer},
                           {"wireguard_key", identity.wireguard_key},
                           {"signing_key", identity.signing_key},
                           {"device_id", identity.device_id},
                           {"listen_port", identity.listen_port},
                           {"endpoint", endpoint},
                           {"source", source},
                           {"sequence", sequence},
                           {"issued_at", issued},
                           {"expires_at", expires}});
    if (crypto::public_key(key) != identity.signing_key)
        throw std::runtime_error("Endpoint publisher differs from its private signing key");
    return Json{{"metadata", value}, {"signature", crypto::sign(key, canonical(value))}}.dump(2);
}
Json verify_endpoint_record(const std::string &bytes, const EndpointIdentity &expected, int64_t now,
                            const fs::path &scratch) {
    if (bytes.size() > 16384 || now < 1 || now > INT64_MAX - 120)
        throw std::runtime_error("Invalid endpoint record size or verification time");
    identity_valid(expected);
    auto value = Json::parse(crypto::parse(bytes).dump());
    fields(value, {"metadata", "signature"});
    auto body = metadata(value.at("metadata"));
    if (body.at("project") != expected.project || body.at("peer") != expected.peer ||
        body.at("wireguard_key") != expected.wireguard_key ||
        body.at("signing_key") != expected.signing_key ||
        body.at("device_id") != expected.device_id ||
        body.at("listen_port") != expected.listen_port)
        throw std::runtime_error(
            "Signed endpoint does not match the pinned project/device identity");
    if (body.at("issued_at").get<int64_t>() > now + 120 ||
        body.at("expires_at").get<int64_t>() <= now)
        throw std::runtime_error(
            "Signed endpoint is expired or its publication clock is too far ahead");
    auto signature = string(value.at("signature"), 88);
    auto payload = canonical(body);
    if (!crypto::verify(expected.signing_key, payload, signature, scratch))
        throw std::runtime_error("Endpoint publication signature is invalid");
    return {{"metadata", body}, {"canonical_hash", sha256(payload)}};
}
} // namespace cysynck
