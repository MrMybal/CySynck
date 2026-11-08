#include "router_lease.hpp"
#include <iostream>
#include <stdexcept>
using namespace cysynck;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <typename F> void refuses(F action, const char *message) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        constexpr int64_t start = 1700000000;
        Json state = Json::object(), durable;
        auto save = [&] { durable = Json::parse(state.dump()); };
        RouterLeaseContext owned{"peer:pinned-key", "192.168.7.2", 51821};
        Json options = {{"gateway", "192.168.7.1"}, {"lifetime_seconds", 600}};
        size_t calls = 0;
        bool fail = false;
        uint32_t epoch = 100, grant = 300;
        uint16_t assigned = 45000;
        std::string actual_protocol = "pcp", used_nonce;
        RouterMapper mapper = [&](const std::string &gateway, uint16_t internal, uint16_t preferred,
                                  uint32_t lifetime, const std::string &protocol,
                                  const std::string &nonce, const std::string &external,
                                  const std::string &source) {
            ++calls;
            check(source == owned.local_address,
                  "Gateway requests were not bound to the consented local address");
            check(gateway == "192.168.7.1" && internal == owned.listen_port && internal != 51820,
                  "Mapping did not use the explicitly owned live UDP listener");
            check(durable.at("lease").at("nonce") == nonce && durable.at("retry_at") > 0,
                  "The ownership nonce or retry checkpoint was not durable before the datagram");
            if (lifetime)
                check(durable.at("enabled") == true, "Mapping preceded local consent");
            else
                check(durable.at("enabled") == false && protocol != "auto",
                      "Deletion preceded durable disable or used unrecorded negotiation");
            used_nonce = nonce;
            if (fail)
                throw std::runtime_error("fixture timeout " + nonce);
            return GatewayMapping{
                actual_protocol, lifetime ? "198.18.0.9" : "",         owned.local_address,
                internal,        uint16_t(lifetime ? assigned : 0),    lifetime ? grant : 0,
                epoch,           actual_protocol == "pcp" ? nonce : ""};
        };
        check(!router_lease_due(state, start), "An unconfigured controller opened router ports");
        refuses([&] { renew_router_lease(state, owned, start, true, mapper, save); },
                "Renewal did not require explicit consent");
        refuses(
            [&] {
                enable_router_lease(state, {{"gateway", "router.example"}}, owned, start, save);
            },
            "A hostname became a privileged gateway");
        refuses(
            [&] { enable_router_lease(state, {{"gateway", "192.168.007.1"}}, owned, start, save); },
            "Ambiguous gateway notation was accepted");
        refuses(
            [&] {
                enable_router_lease(state, options, {owned.owner, owned.local_address, 0}, start,
                                    save);
            },
            "The controller accepted internal port zero/delete-all");
        enable_router_lease(state, options, owned, start, save);
        check(calls == 0, "Configuration sent an uncheckpointed request");
        auto status = renew_router_lease(state, owned, start, false, mapper, save);
        check(status.at("active") == true && status.at("external_port") == assigned && calls == 1,
              "The actual assigned port did not become active");
        auto nonce = used_nonce;
        check(status.dump().find(nonce) == std::string::npos && !status.contains("lease"),
              "Public router status disclosed the private ownership token");
        auto endpoint = router_lease_endpoint(state, owned, start + 1);
        check(endpoint.at("endpoint") == "198.18.0.9:45000" &&
                  endpoint.at("expires_at") == start + 300,
              "Signed publication could exceed the granted lease or copied the internal port");
        renew_router_lease(state, owned, start + 30, false, mapper, save);
        check(calls == 1, "Maintenance ignored the lease renewal schedule");
        auto renewed_at = state.at("lease").at("renew_at").get<int64_t>();
        check(renewed_at >= start + 150 && renewed_at <= start + 187,
              "PCP renewal did not use the half-lifetime jitter interval");
        assigned = 45001;
        epoch += uint32_t(renewed_at - start);
        state = Json::parse(durable.dump()); // Real persisted JSON restart representation.
        status = renew_router_lease(state, owned, renewed_at, false, mapper, save);
        check(calls == 2 && used_nonce == nonce && status.at("external_port") == 45001 &&
                  status.at("gateway_restarted") == false,
              "Restart lost ownership or renewal failed to accept the router's remapped port");
        fail = true;
        auto expiry = state.at("lease").at("expires_at").get<int64_t>();
        status = renew_router_lease(state, owned, renewed_at + 1, true, mapper, save);
        check(status.at("active") == true &&
                  status.at("error").get<std::string>().find(nonce) == std::string::npos,
              "A failed renewal hid the prior valid lease or leaked its nonce");
        check(router_lease_endpoint(state, owned, expiry).is_null(),
              "An expired endpoint remained publishable after renewal failed");
        auto count = calls;
        renew_router_lease(state, owned, renewed_at + 2, false, mapper, save);
        check(calls == count, "A gateway timeout caused an unbounded maintenance retry loop");
        fail = false;
        epoch = 3;
        status = renew_router_lease(state, owned, renewed_at + 50, true, mapper, save);
        check(status.at("gateway_restarted") == true && used_nonce == nonce,
              "A gateway reboot was not recognized using the durable epoch checkpoint");
        refuses(
            [&] { enable_router_lease(state, {{"gateway", "192.168.8.1"}}, owned, start, save); },
            "An active mapping changed gateway without first disabling it");
        auto changed = owned;
        changed.local_address = "192.168.8.2";
        count = calls;
        status = renew_router_lease(state, changed, renewed_at + 51, true, mapper, save);
        check(status.at("enabled") == false && status.at("phase") == "network-changed" &&
                  calls == count &&
                  router_lease_endpoint(state, changed, renewed_at + 51).is_null(),
              "A new network silently received consent or inherited a signed endpoint");
        status = disable_router_lease(state, changed, renewed_at + 52, mapper, save);
        check(calls == count && status.at("phase") == "awaiting-expiry" &&
                  status.at("abandoned_until") > 0,
              "Cleanup sent deletion through the wrong local network");

        // Ambiguous timeout/crash before first response keeps the same PCP nonce.
        state = Json::object();
        enable_router_lease(state, {{"gateway", "192.168.7.1"}, {"protocol", "pcp"}}, owned, start,
                            save);
        fail = true;
        renew_router_lease(state, owned, start, true, mapper, save);
        auto reserved = used_nonce;
        state = Json::parse(durable.dump());
        fail = false;
        renew_router_lease(state, owned, start + 46, false, mapper, save);
        check(used_nonce == reserved, "A crash after MAP lost the private PCP reservation");
        fail = true;
        status = disable_router_lease(state, owned, start + 47, mapper, save);
        check(status.at("phase") == "cleanup-pending" && status.at("enabled") == false &&
                  router_lease_endpoint(state, owned, start + 47).is_null(),
              "Failed deletion restarted renewal or continued publishing the lease");
        state = Json::parse(durable.dump());
        fail = false;
        check(router_lease_due(state, start + 93), "Failed deletion lost its durable retry intent");
        status = disable_router_lease(state, owned, start + 93, mapper, save);
        check(status.at("phase") == "disabled" && !state.contains("lease"),
              "An owned PCP mapping was not deleted idempotently after restart");
        count = calls;
        disable_router_lease(state, owned, start + 94, mapper, save);
        check(calls == count, "Repeated disable sent an unrelated mapping deletion");

        state = Json::object();
        enable_router_lease(state, options, owned, start, save);
        renew_router_lease(state, owned, start, true, mapper, save);
        count = calls;
        check(router_lease_endpoint(state, owned, start - 1).is_null(),
              "A clock rewind kept a lease acquired in the future publishable");
        disable_router_lease(state, owned, start - 1, mapper, save);
        check(calls == count, "A clock rewind deleted a mapping with an uncertain expiry");

        // Legacy lacks a nonce: delete only a confirmed, unexpired mapping on its
        // original source/listener, never an uncertain auto request or expired port.
        state = Json::object();
        actual_protocol = "nat-pmp";
        enable_router_lease(state, options, owned, start, save);
        fail = true;
        renew_router_lease(state, owned, start, true, mapper, save);
        count = calls;
        status = disable_router_lease(state, owned, start + 1, mapper, save);
        check(calls == count && status.at("phase") == "awaiting-expiry",
              "An uncertain legacy negotiation deleted a possibly unrelated mapping");
        state = Json::object();
        fail = false;
        enable_router_lease(state, options, owned, start, save);
        renew_router_lease(state, owned, start, true, mapper, save);
        status = disable_router_lease(state, owned, start + 1, mapper, save);
        check(status.at("phase") == "disabled",
              "Confirmed legacy ownership could not be cleaned up");
        state = Json::object();
        enable_router_lease(state, options, owned, start, save);
        renew_router_lease(state, owned, start, true, mapper, save);
        count = calls;
        disable_router_lease(state, owned, start + 601, mapper, save);
        check(calls == count, "An expired legacy mapping could delete a new application's port");
        std::cout << "PASS consent, durable reservation, actual UDP port, restart/renew/remap, "
                     "expiry, reboot, network change and owned cleanup\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
