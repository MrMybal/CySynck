#include "network_discovery.hpp"
#include <array>
#include <regex>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif
namespace cysynck {
EndpointIdentity network_endpoint_identity(const Json &profile, const std::string &project) {
    return {project,
            profile.at("peer_id"),
            profile.at("public_key"),
            profile.at("signing_key"),
            profile.at("device_id"),
            profile.at("listen_port")};
}
Json accept_network_endpoint(const std::string &bytes, const EndpointIdentity &identity,
                             Json &checkpoint, int64_t now, const fs::path &scratch) {
    auto verified = verify_endpoint_record(bytes, identity, now, scratch);
    const auto &body = verified.at("metadata");
    if (!checkpoint.is_null()) {
        auto sequence = checkpoint.at("metadata").at("sequence").get<int64_t>();
        if (body.at("sequence").get<int64_t>() < sequence ||
            (body.at("sequence") == sequence &&
             verified.at("canonical_hash") != checkpoint.at("canonical_hash")))
            throw std::runtime_error("Endpoint publication rolls back or reuses a signed sequence");
    }
    checkpoint = verified;
    return body;
}
std::string direct_network_endpoint(const Json &profile, const Json &connection) {
    auto type = connection.value("type", std::string{});
    if (!connection.value("connected", false) ||
        (type != "tcp-client" && type != "tcp-server" && type != "quic-client" &&
         type != "quic-server") ||
        profile.value("device_id", std::string{}).empty())
        return {};
    std::smatch match;
    auto address = connection.value("address", std::string{});
    if (address.size() > 128 ||
        !std::regex_match(address, match,
                          std::regex("([0-9.]+|\\[[a-fA-F0-9:]+\\]):([0-9]{1,5})")) ||
        std::stoi(match[2]) < 1 || std::stoi(match[2]) > 65535)
        return {};
    auto host = match[1].str();
    auto port = profile.at("listen_port").get<int>();
    if (port < 1 || port > 65535)
        throw std::runtime_error("Invalid pinned UDP listener");
    if (host.front() == '[') {
        auto literal = host.substr(1, host.size() - 2);
        std::array<unsigned char, 16> ip{};
        if (inet_pton(AF_INET6, literal.c_str(), ip.data()) != 1 ||
            !((ip[0] & 0xe0) == 0x20 || (ip[0] & 0xfe) == 0xfc))
            return {};
    } else {
        std::array<unsigned char, 4> ip{};
        if (inet_pton(AF_INET, host.c_str(), ip.data()) != 1 ||
            !(ip[0] == 10 || (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) ||
              (ip[0] == 192 && ip[1] == 168)))
            return {};
    }
    return host + ":" + std::to_string(port);
}
Json resolved_network_endpoint(const Json &profile, const Json &checkpoints, const Json &connection,
                               int64_t now) {
    auto manual = profile.at("endpoint").get<std::string>();
    if (!manual.empty())
        return {{"endpoint", manual}, {"source", "manual"}, {"expires_at", 0}};
    Json best = {{"endpoint", ""}, {"source", "unavailable"}, {"expires_at", 0}};
    if (profile.contains("signing_key"))
        for (const auto &record : checkpoints) {
            if (!record.is_object() || !record.contains("metadata"))
                continue;
            const auto &body = record.at("metadata");
            if (body.at("expires_at").get<int64_t>() <= now ||
                body.at("expires_at").get<int64_t>() <= best.at("expires_at").get<int64_t>())
                continue;
            auto expected = network_endpoint_identity(profile, body.at("project"));
            if (body.at("peer") != expected.peer ||
                body.at("wireguard_key") != expected.wireguard_key ||
                body.at("signing_key") != expected.signing_key ||
                body.at("device_id") != expected.device_id ||
                body.at("listen_port") != expected.listen_port)
                continue;
            best = {{"endpoint", body.at("endpoint")},
                    {"source", "signed-" + body.at("source").get<std::string>()},
                    {"expires_at", body.at("expires_at")},
                    {"project", body.at("project")}};
        }
    if (!best.at("endpoint").get<std::string>().empty())
        return best;
    auto direct = direct_network_endpoint(profile, connection);
    if (!direct.empty())
        return {{"endpoint", direct}, {"source", "syncthing-direct-ip"}, {"expires_at", now + 90}};
    return best;
}
std::string effective_network_endpoint(const Json &peer, int64_t now) {
    auto manual = peer.at("profile").at("endpoint").get<std::string>();
    if (!manual.empty())
        return manual;
    auto resolved = peer.value("resolved_endpoint", Json::object());
    return resolved.value("expires_at", int64_t(0)) > now
               ? resolved.value("endpoint", std::string{})
               : "";
}
} // namespace cysynck
