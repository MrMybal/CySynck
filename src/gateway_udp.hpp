#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace cysynck {
using GatewayPacket = std::vector<unsigned char>;
// Private transport shared by the explicit PCP/NAT-PMP gateway providers.
class GatewayUdp {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    GatewayUdp(const std::string &gateway_ipv4, uint16_t port,
                const std::string &expected_local_address = "");
    ~GatewayUdp();
    GatewayUdp(const GatewayUdp &) = delete;
    GatewayUdp &operator=(const GatewayUdp &) = delete;
    std::string local_address() const;
    GatewayPacket request(const GatewayPacket &packet,
                          const std::function<bool(const GatewayPacket &)> &accept,
                          const std::vector<int> &retry_intervals_ms = {250, 500, 1000});
};
} // namespace cysynck
