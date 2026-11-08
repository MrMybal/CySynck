#pragma once
#include <cstdint>
#include <string>
namespace cysynck {
struct NatPmpMapping {
    std::string external_address;
    std::string local_address;
    uint16_t internal_port = 0, external_port = 0;
    uint32_t lifetime_seconds = 0, gateway_epoch = 0;
};
// RFC 6886. The caller opts in to a specific gateway and owns this UDP mapping.
// router_port is injectable for isolated loopback tests; production uses UDP 5351.
NatPmpMapping nat_pmp_map(const std::string &gateway_ipv4, uint16_t internal_port,
                          uint16_t preferred_external_port, uint32_t lifetime_seconds,
                          uint16_t router_port = 5351);
} // namespace cysynck
