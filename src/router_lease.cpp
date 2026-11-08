#include "router_lease.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <regex>
#include <stdexcept>

namespace cysynck {
namespace {
void clock_value(int64_t now) {
    if (now <= 0 || now > std::numeric_limits<int64_t>::max() - UINT32_MAX - 3600)
        throw std::runtime_error("Invalid router lease clock");
}
void address(const std::string &value) {
    if (!std::regex_match(value, std::regex("[0-9]{1,3}(\\.[0-9]{1,3}){3}")))
        throw std::runtime_error("Choose a canonical IPv4 gateway address");
    size_t start = 0;
    for (int i = 0; i < 4; ++i) {
        auto end = value.find('.', start);
        auto part = value.substr(start, end - start);
        unsigned number = 0;
        std::from_chars(part.data(), part.data() + part.size(), number);
        if (number > 255 || part != std::to_string(number) ||
            (i == 0 && (!number || number >= 224)))
            throw std::runtime_error("Choose a usable canonical IPv4 gateway address");
        start = end + 1;
    }
}
void valid_context(const RouterLeaseContext &context) {
    if (context.owner.empty() || context.owner.size() > 512 || !context.listen_port)
        throw std::runtime_error("Port mapping requires an owned live WireGuard listener");
    address(context.local_address);
}
Json options(const Json &input) {
    auto gateway = input.at("gateway").get<std::string>();
    address(gateway);
    auto protocol = input.value("protocol", std::string("auto"));
    auto lifetime = input.value("lifetime_seconds", int64_t(600));
    auto external = input.value("external_port", int64_t(0));
    if (protocol != "auto" && protocol != "pcp" && protocol != "nat-pmp")
        throw std::runtime_error("Choose router protocol auto, pcp or nat-pmp");
    if (lifetime < 60 || lifetime > 3600 || external < 0 || external > 65535)
        throw std::runtime_error(
            "Choose a 60..3600 second router lease and a 0..65535 preferred port");
    return {{"gateway", gateway},
            {"protocol", protocol},
            {"lifetime_seconds", lifetime},
            {"external_port", external}};
}
bool same_context(const Json &lease, const RouterLeaseContext &context,
                  const std::string &gateway) {
    return lease.is_object() && lease.value("owner", "") == context.owner &&
           lease.value("local_address", "") == context.local_address &&
           lease.value("internal_port", 0) == context.listen_port &&
           lease.value("gateway", "") == gateway;
}
bool active(const Json &state, int64_t now) {
    if (!state.is_object() || !state.value("enabled", false))
        return false;
    auto lease = state.value("lease", Json::object());
    return lease.value("confirmed", false) && lease.value("acquired_at", int64_t(0)) <= now &&
           lease.value("expires_at", int64_t(0)) > now;
}
void abandon(Json &state, int64_t now) {
    auto lease = state.value("lease", Json::object());
    state["abandoned_until"] =
        std::max(state.value("abandoned_until", int64_t(0)),
                 std::max(lease.value("expires_at", now), lease.value("uncertain_until", now)));
    state.erase("lease");
}
std::string error_text(const std::exception &error, const std::string &nonce) {
    std::string text = error.what();
    if (!nonce.empty())
        for (auto at = text.find(nonce); at != std::string::npos; at = text.find(nonce))
            text.replace(at, nonce.size(), "<private>");
    return text.substr(0, 512);
}
bool valid_until(const Json &lease, int64_t now) {
    return std::max(lease.value("expires_at", int64_t(0)),
                    lease.value("uncertain_until", int64_t(0))) > now;
}
} // namespace

Json router_lease_status(const Json &state, int64_t now) {
    Json result = {{"enabled", state.is_object() && state.value("enabled", false)},
                   {"active", active(state, now)},
                   {"phase", state.value("phase", "disabled")},
                   {"error", state.value("error", "")},
                   {"abandoned_until", state.value("abandoned_until", int64_t(0))}};
    if (state.contains("options"))
        result["options"] = options(state.at("options"));
    if (state.contains("lease")) {
        const auto &lease = state.at("lease");
        for (auto field :
             {"gateway", "local_address", "internal_port", "external_address", "external_port",
              "protocol", "acquired_at", "expires_at", "renew_at", "gateway_restarted"})
            if (lease.contains(field))
                result[field] = lease.at(field);
    }
    return result;
}
void enable_router_lease(Json &state, const Json &input, const RouterLeaseContext &context,
                         int64_t now, const RouterLeaseSave &save) {
    clock_value(now);
    valid_context(context);
    auto selected = options(input);
    if (!state.is_object())
        state = Json::object();
    if (state.value("enabled", false) && state.at("options") != selected)
        throw std::runtime_error(
            "Disable the existing router mapping before changing its gateway or options");
    if (state.contains("lease") && state.contains("options") && state.at("options") != selected &&
        same_context(state.at("lease"), context, selected.at("gateway")) &&
        valid_until(state.at("lease"), now))
        throw std::runtime_error(
            "Finish disabling the owned router mapping before changing its options");
    if (state.contains("lease") &&
        (!same_context(state.at("lease"), context, selected.at("gateway")) ||
         !valid_until(state.at("lease"), now)))
        abandon(state, now);
    // This local enable action is the explicit consent boundary, including after
    // a network/source-address change. Maintenance never consents on its own.
    state["options"] = selected;
    state["enabled"] = true;
    state["phase"] = "enabled";
    state["error"] = "";
    state["retry_at"] = 0;
    save();
}
bool router_lease_due(const Json &state, int64_t now) {
    if (!state.is_object())
        return false;
    bool clock_rewound = now < state.value("last_attempt_at", int64_t(0));
    if (!clock_rewound && now < state.value("retry_at", int64_t(0)))
        return false;
    if (!state.value("enabled", false))
        return state.value("phase", "") == "cleanup-pending";
    auto lease = state.value("lease", Json::object());
    return !active(state, now) || now >= lease.value("renew_at", int64_t(0));
}
Json renew_router_lease(Json &state, const RouterLeaseContext &context, int64_t now, bool force,
                        const RouterMapper &map, const RouterLeaseSave &save) {
    clock_value(now);
    valid_context(context);
    if (!state.value("enabled", false))
        throw std::runtime_error("Enable router mapping locally before renewing it");
    auto selected = options(state.at("options"));
    auto gateway = selected.at("gateway").get<std::string>();
    if (state.contains("lease") && !same_context(state.at("lease"), context, gateway)) {
        state["enabled"] = false;
        state["phase"] = "network-changed";
        state["error"] = "The gateway-facing address or WireGuard listener changed; enable locally "
                         "to consent again";
        save();
        return router_lease_status(state, now);
    }
    if (!force && !router_lease_due(state, now))
        return router_lease_status(state, now);
    if (!state.contains("lease")) {
        state["lease"] = {{"owner", context.owner},
                          {"local_address", context.local_address},
                          {"internal_port", context.listen_port},
                          {"gateway", gateway},
                          {"protocol", selected.at("protocol")},
                          {"nonce", new_gateway_nonce()},
                          {"confirmed", false}};
    }
    auto &lease = state["lease"];
    auto nonce = lease.at("nonce").get<std::string>();
    auto requested_protocol = lease.at("protocol").get<std::string>();
    auto previous_time = lease.value("acquired_at", now);
    auto previous_epoch = lease.value("gateway_epoch", uint32_t(0));
    bool previous_confirmed = lease.value("confirmed", false);
    lease["uncertain_until"] = std::max(lease.value("uncertain_until", int64_t(0)),
                                        now + selected.at("lifetime_seconds").get<int64_t>());
    state["retry_at"] = now + 45;
    state["last_attempt_at"] = now;
    // Persist ownership BEFORE sending. A restart reuses the reserved PCP nonce.
    save();
    try {
        auto result =
            map(gateway, context.listen_port,
                lease.value("external_port", selected.at("external_port").get<uint16_t>()),
                selected.at("lifetime_seconds"), requested_protocol, nonce,
                lease.value("external_address", ""), context.local_address);
        if ((result.protocol != "pcp" && result.protocol != "nat-pmp") ||
            (requested_protocol != "auto" && result.protocol != requested_protocol) ||
            result.local_address != context.local_address ||
            result.internal_port != context.listen_port || !result.external_port ||
            !result.lifetime_seconds || (result.protocol == "pcp" && result.private_nonce != nonce))
            throw std::runtime_error(
                "Router mapping does not match the reserved listener and ownership context");
        address(result.external_address);
        auto elapsed = now >= previous_time ? uint64_t(now - previous_time) : uint64_t(0);
        auto epoch_delta = uint32_t(result.gateway_epoch - previous_epoch);
        bool rebooted = previous_confirmed && (uint64_t(epoch_delta) > elapsed + 2 + elapsed / 8 ||
                                               uint64_t(epoch_delta) + 2 + elapsed / 8 < elapsed);
        lease["protocol"] = result.protocol;
        lease["external_address"] = result.external_address;
        lease["external_port"] = result.external_port;
        lease["gateway_epoch"] = result.gateway_epoch;
        lease["gateway_restarted"] = rebooted;
        lease["acquired_at"] = now;
        lease["expires_at"] = now + result.lifetime_seconds;
        // Renew at half the grant (PCP adds up to 1/8 jitter), capped to maintain
        // at least the hourly signed publication window even for long grants.
        unsigned jitter = 0;
        std::from_chars(nonce.data(), nonce.data() + 4, jitter, 16);
        auto delay = uint64_t(result.lifetime_seconds) / 2;
        if (result.protocol == "pcp")
            delay += jitter % (result.lifetime_seconds / 8 + 1);
        lease["renew_at"] = now + std::max<uint64_t>(1, std::min<uint64_t>(1800, delay));
        lease["confirmed"] = true;
        state["phase"] = "active";
        state["error"] = "";
        state["retry_at"] = 0;
        save();
    } catch (const std::exception &error) {
        state["phase"] = "retry-pending";
        state["error"] = error_text(error, nonce);
        save();
    }
    return router_lease_status(state, now);
}
Json disable_router_lease(Json &state, const RouterLeaseContext &context, int64_t now,
                          const RouterMapper &map, const RouterLeaseSave &save) {
    clock_value(now);
    if (!state.is_object())
        state = Json::object();
    state["enabled"] = false;
    state["error"] = "";
    state["phase"] = "disabled";
    save(); // Stop publication/renewal even if deletion fails or the process dies.
    if (!state.contains("lease"))
        return router_lease_status(state, now);
    auto lease = state.at("lease");
    auto gateway = lease.at("gateway").get<std::string>();
    if (!valid_until(lease, now)) {
        state.erase("lease");
        save();
        return router_lease_status(state, now);
    }
    if (!same_context(lease, context, gateway) || now < lease.value("acquired_at", now) ||
        (!lease.value("confirmed", false) && lease.at("protocol") != "pcp")) {
        // NAT-PMP has no nonce. After an ambiguous auto/legacy request, expiry is
        // safer than deleting a possibly unrelated mapping. Never send via a new LAN.
        abandon(state, now);
        state["phase"] = "awaiting-expiry";
        save();
        return router_lease_status(state, now);
    }
    auto nonce = lease.at("nonce").get<std::string>();
    state["phase"] = "cleanup-pending";
    state["retry_at"] = now + 45;
    state["last_attempt_at"] = now;
    save();
    try {
        auto result = map(gateway, context.listen_port, lease.value("external_port", uint16_t(0)),
                          0, lease.at("protocol"), nonce, lease.value("external_address", ""),
                          context.local_address);
        if (result.lifetime_seconds || result.local_address != context.local_address ||
            result.internal_port != context.listen_port ||
            result.protocol != lease.at("protocol").get<std::string>() ||
            (result.protocol == "pcp" && result.private_nonce != nonce))
            throw std::runtime_error("Router did not confirm the owned mapping deletion");
        state.erase("lease");
        state["phase"] = "disabled";
        state["retry_at"] = 0;
        save();
    } catch (const std::exception &error) {
        state["error"] = error_text(error, nonce);
        save();
    }
    return router_lease_status(state, now);
}
Json router_lease_endpoint(const Json &state, const RouterLeaseContext &context, int64_t now) {
    if (!active(state, now))
        return Json();
    const auto &lease = state.at("lease");
    if (!same_context(lease, context, state.at("options").at("gateway")))
        return Json();
    auto expiry = std::min(lease.at("expires_at").get<int64_t>(), now + 3600);
    if (expiry <= now + 5)
        return Json();
    return {{"endpoint", lease.at("external_address").get<std::string>() + ":" +
                             std::to_string(lease.at("external_port").get<unsigned>())},
            {"source", lease.at("protocol")},
            {"expires_at", expiry}};
}
} // namespace cysynck
