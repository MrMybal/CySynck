#pragma once
#include "gateway_mapping.hpp"
#include <cysynck/core.hpp>
#include <functional>

namespace cysynck {
// Private controller inputs, obtained from the owned live WireGuard interface and
// the OS-selected route to the explicitly configured gateway, never shared files.
struct RouterLeaseContext {
    std::string owner, local_address;
    uint16_t listen_port = 0;
};
using RouterMapper = std::function<GatewayMapping(const std::string &, uint16_t, uint16_t, uint32_t,
                                                  const std::string &, const std::string &,
                                                  const std::string &, const std::string &)>;
using RouterLeaseSave = std::function<void()>;
void enable_router_lease(Json &state, const Json &options, const RouterLeaseContext &context,
                         int64_t now, const RouterLeaseSave &save);
Json renew_router_lease(Json &state, const RouterLeaseContext &context, int64_t now, bool force,
                        const RouterMapper &map, const RouterLeaseSave &save);
Json disable_router_lease(Json &state, const RouterLeaseContext &context, int64_t now,
                          const RouterMapper &map, const RouterLeaseSave &save);
bool router_lease_due(const Json &state, int64_t now);
Json router_lease_status(const Json &state, int64_t now);
// Empty if disabled, expired, uncertain, or bound to a different live context.
Json router_lease_endpoint(const Json &state, const RouterLeaseContext &context, int64_t now);
} // namespace cysynck
