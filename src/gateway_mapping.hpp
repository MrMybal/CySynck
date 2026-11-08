#pragma once
#include <cstdint>
#include <string>
namespace cysynck {
struct GatewayMapping {
    std::string protocol, external_address, local_address;
    uint16_t internal_port = 0, external_port = 0;
    uint32_t lifetime_seconds = 0, gateway_epoch = 0;
    // Private ownership token. Never include it in a public profile or diagnostic.
    std::string private_nonce;
};
std::string new_gateway_nonce();
// Reserve/persist the nonce before the first PCP request; reuse it for renewal/deletion.
// Auto falls back only on an explicit legacy Unsupported Version response.
GatewayMapping map_gateway(const std::string &gateway_ipv4, uint16_t internal_udp_port,
                           uint16_t preferred_external_port, uint32_t lifetime_seconds,
                           const std::string &protocol, const std::string &private_nonce,
                           const std::string &preferred_external_ipv4 = "",
                           uint16_t router_port = 5351,
                           const std::string &expected_local_address = "");
} // namespace cysynck
