#include "nat_pmp.hpp"
#include "gateway_udp.hpp"
#include <stdexcept>
namespace cysynck {
namespace {
using Bytes = std::vector<unsigned char>;
uint16_t number16(const Bytes &data, size_t at) {
    return (uint16_t(data.at(at)) << 8) | data.at(at + 1);
}
uint32_t number32(const Bytes &data, size_t at) {
    return (uint32_t(data.at(at)) << 24) | (uint32_t(data.at(at + 1)) << 16) |
           (uint32_t(data.at(at + 2)) << 8) | data.at(at + 3);
}
void append16(Bytes &data, uint16_t value) {
    data.push_back(value >> 8);
    data.push_back(value & 255);
}
void append32(Bytes &data, uint32_t value) {
    data.push_back(value >> 24);
    data.push_back((value >> 16) & 255);
    data.push_back((value >> 8) & 255);
    data.push_back(value & 255);
}
class Client {
    GatewayUdp transport_;

  public:
    Client(const std::string &gateway, uint16_t port, const std::string &source)
        : transport_(gateway, port, source) {}
    std::string local_address() const { return transport_.local_address(); }
    Bytes request(const Bytes &packet, unsigned char expected_opcode, size_t expected_length) {
        return transport_.request(packet, [&](const GatewayPacket &response) {
            if (response.size() < 8 || response[1] != expected_opcode)
                return false;
            if (response[0] != 0)
                throw std::runtime_error("Unsupported NAT-PMP response version");
            auto code = number16(response, 2);
            if (code)
                throw std::runtime_error("NAT-PMP gateway refused the request (result " +
                                         std::to_string(code) + ")");
            if (response.size() != expected_length)
                throw std::runtime_error("Malformed NAT-PMP response length");
            return true;
        });
    }
};
} // namespace
NatPmpMapping nat_pmp_map(const std::string &gateway, uint16_t internal_port,
                          uint16_t preferred_port, uint32_t lifetime, uint16_t router_port,
                          const std::string &expected_source) {
    if (!internal_port || lifetime > 3600)
        throw std::runtime_error(
            "NAT-PMP needs a nonzero WireGuard port and a lifetime of at most 3600 seconds");
    Client client(gateway, router_port, expected_source);
    Bytes address;
    if (lifetime) {
        address = client.request({0, 0}, 128, 12);
        auto first = address[8];
        if (!first || first == 127 || first >= 224)
            throw std::runtime_error("NAT-PMP returned an unusable external IPv4 address");
    }
    Bytes request{0, 1, 0, 0};
    append16(request, internal_port);
    append16(request, lifetime ? preferred_port : 0);
    append32(request, lifetime);
    auto response = client.request(request, 129, 16);
    if (number16(response, 8) != internal_port ||
        (lifetime && (!number16(response, 10) || !number32(response, 12))) ||
        (!lifetime && number32(response, 12)))
        throw std::runtime_error(
            "NAT-PMP mapping response does not match the requested WireGuard port/lifetime");
    // An epoch reset between these requests invalidates the external-address observation.
    if (lifetime && number32(response, 4) < number32(address, 4))
        throw std::runtime_error(
            "NAT-PMP gateway restarted during mapping; retry before publishing the endpoint");
    return {address.empty() ? ""
                            : std::to_string(address[8]) + "." + std::to_string(address[9]) + "." +
                                  std::to_string(address[10]) + "." + std::to_string(address[11]),
            client.local_address(),
            internal_port,
            number16(response, 10),
            number32(response, 12),
            number32(response, 4)};
}
} // namespace cysynck
