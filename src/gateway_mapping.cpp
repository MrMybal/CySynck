#include "gateway_mapping.hpp"
#include "gateway_udp.hpp"
#include "nat_pmp.hpp"
#include <cysynck/core.hpp>
#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <sstream>

namespace cysynck {
namespace {
using Bytes = GatewayPacket;
uint16_t number16(const Bytes &data, size_t at) {
    return (uint16_t(data.at(at)) << 8) | data.at(at + 1);
}
uint32_t number32(const Bytes &data, size_t at) {
    return (uint32_t(number16(data, at)) << 16) | number16(data, at + 2);
}
void append16(Bytes &data, uint16_t value) {
    data.push_back(value >> 8);
    data.push_back(value & 255);
}
void append32(Bytes &data, uint32_t value) {
    append16(data, value >> 16);
    append16(data, value & 65535);
}
Bytes ipv4(const std::string &value) {
    std::istringstream input(value);
    std::string part;
    Bytes bytes;
    while (std::getline(input, part, '.')) {
        unsigned number = 0;
        auto parsed = std::from_chars(part.data(), part.data() + part.size(), number);
        if (part.empty() || part.size() > 3 || parsed.ec != std::errc{} ||
            parsed.ptr != part.data() + part.size() || number > 255 ||
            part != std::to_string(number))
            throw std::runtime_error("Port mapping requires a canonical IPv4 address");
        bytes.push_back(static_cast<unsigned char>(number));
    }
    if (bytes.size() != 4 || value.back() == '.')
        throw std::runtime_error("Invalid port-mapping IPv4 address");
    return bytes;
}
Bytes mapped_address(const std::string &value) {
    Bytes result(10, 0);
    result.push_back(255);
    result.push_back(255);
    auto address = ipv4(value);
    result.insert(result.end(), address.begin(), address.end());
    return result;
}
Bytes nonce_bytes(const std::string &value) {
    if (value.size() != 24 || value.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("PCP needs the reserved private 96-bit mapping nonce");
    Bytes output;
    for (size_t i = 0; i < value.size(); i += 2) {
        unsigned byte = 0;
        auto parsed = std::from_chars(value.data() + i, value.data() + i + 2, byte, 16);
        if (parsed.ec != std::errc{})
            throw std::runtime_error("Invalid PCP nonce");
        output.push_back(static_cast<unsigned char>(byte));
    }
    return output;
}
class LegacyOnly : public std::runtime_error {
  public:
    LegacyOnly()
        : std::runtime_error("The selected gateway supports legacy NAT-PMP instead of PCP") {}
};
GatewayMapping pcp_map(const std::string &gateway, uint16_t internal, uint16_t preferred,
                       uint32_t lifetime, const std::string &nonce_hex,
                       const std::string &preferred_ip, uint16_t port, const std::string &source) {
    auto nonce = nonce_bytes(nonce_hex);
    GatewayUdp client(gateway, port, source);
    Bytes packet{2, 1, 0, 0};
    append32(packet, lifetime);
    auto local = mapped_address(client.local_address());
    packet.insert(packet.end(), local.begin(), local.end());
    packet.insert(packet.end(), nonce.begin(), nonce.end());
    packet.insert(packet.end(), {17, 0, 0, 0});
    append16(packet, internal);
    append16(packet, preferred);
    auto external = mapped_address(preferred_ip.empty() ? "0.0.0.0" : preferred_ip);
    packet.insert(packet.end(), external.begin(), external.end());
    // PCP's initial timeout is 3 s with +/-10% randomization. The interactive operation
    // stops at that deadline; maintenance may retry the same privately reserved nonce.
    auto random = new_gateway_nonce();
    unsigned jitter = 0;
    std::from_chars(random.data(), random.data() + 4, jitter, 16);
    auto response = client.request(
        packet,
        [&](const Bytes &value) {
            if (value.size() < 4 || value[1] != 129)
                return false;
            if (value[0] == 0 && value.size() >= 8 && number16(value, 2) == 1)
                throw LegacyOnly();
            if (value[0] != 2)
                throw std::runtime_error("Unsupported PCP response version");
            if (value.size() < 24)
                throw std::runtime_error("Truncated PCP response header");
            if (value[3])
                throw std::runtime_error("PCP gateway refused the request (result " +
                                         std::to_string(value[3]) + ")");
            if (value.size() < 60)
                throw std::runtime_error("Truncated PCP MAP response");
            if (!std::equal(nonce.begin(), nonce.end(), value.begin() + 24) || value[36] != 17 ||
                number16(value, 40) != internal)
                return false;
            for (size_t at = 60; at < value.size();) {
                if (value.size() - at < 4)
                    throw std::runtime_error("Truncated PCP response option");
                auto size = number16(value, at + 2);
                auto padded = (size_t(size) + 3) & ~size_t(3);
                if (padded > value.size() - at - 4)
                    throw std::runtime_error("Invalid PCP response option length");
                if (value[at] < 128)
                    throw std::runtime_error("Unsupported mandatory PCP response option");
                at += 4 + padded;
            }
            return true;
        },
        {2700 + static_cast<int>(jitter % 601)});
    if ((lifetime && (!number32(response, 4) || !number16(response, 42))) ||
        (!lifetime && number32(response, 4)))
        throw std::runtime_error("PCP mapping lifetime does not match the requested operation");
    Bytes prefix(10, 0);
    prefix.insert(prefix.end(), {255, 255});
    if (lifetime && !std::equal(prefix.begin(), prefix.end(), response.begin() + 44))
        throw std::runtime_error("This gateway provider requires an IPv4-mapped external address");
    auto first = response[56];
    if (lifetime && (!first || first == 127 || first >= 224))
        throw std::runtime_error("PCP returned an unusable external IPv4 address");
    auto address = std::to_string(response[56]) + "." + std::to_string(response[57]) + "." +
                   std::to_string(response[58]) + "." + std::to_string(response[59]);
    return {"pcp",
            lifetime ? address : "",
            client.local_address(),
            internal,
            number16(response, 42),
            number32(response, 4),
            number32(response, 8),
            nonce_hex};
}
} // namespace
std::string new_gateway_nonce() { return random_id().substr(0, 24); }
GatewayMapping map_gateway(const std::string &gateway, uint16_t internal, uint16_t preferred,
                           uint32_t lifetime, const std::string &protocol, const std::string &nonce,
                           const std::string &preferred_ip, uint16_t port, const std::string &source) {
    if (!internal || lifetime > 3600)
        throw std::runtime_error(
            "Map only a nonzero WireGuard UDP port for at most 3600 requested seconds");
    if (protocol != "auto" && protocol != "pcp" && protocol != "nat-pmp")
        throw std::runtime_error("Choose port-mapping protocol auto, pcp or nat-pmp");
    if (!lifetime && protocol == "auto")
        throw std::runtime_error(
            "Mapping deletion must use its recorded protocol and ownership token");
    if (protocol != "nat-pmp") {
        try {
            return pcp_map(gateway, internal, preferred, lifetime, nonce, preferred_ip, port, source);
        } catch (const LegacyOnly &) {
            if (protocol != "auto")
                throw;
        }
    }
    auto value = nat_pmp_map(gateway, internal, preferred, lifetime, port, source);
    return {"nat-pmp",           value.external_address, value.local_address, value.internal_port,
            value.external_port, value.lifetime_seconds, value.gateway_epoch, ""};
}
} // namespace cysynck
