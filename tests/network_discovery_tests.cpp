#include "network_discovery.hpp"
#include "cyrevision_codec.hpp"
#include "router_lease.hpp"
#include <ctime>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace cysynck;
namespace crypto = cysynck::cyrevision;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void refuses(const std::function<void()> &fn, const char *message) {
    try {
        fn();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(message);
}
void environment(const char *name, const std::string &value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}
Json public_profile(const std::string &id, char key, const std::string &address,
                    const std::string &signer) {
    return {{"version", 1},
            {"peer_id", id},
            {"name", id},
            {"address", address},
            {"public_key", std::string(42, key) + "A="},
            {"endpoint", ""},
            {"listen_port", 51821},
            {"mesh_port", 8485},
            {"device_id", std::string(56, 'A')},
            {"signing_key", signer}};
}
void project(Core &core, const fs::path &root, const std::string &id) {
    fs::create_directories(root);
    core.request("POST", "/api/projects",
                 {{"id", id},
                  {"name", id},
                  {"root", path_string(root)},
                  {"mode", "backup"},
                  {"backup_interval_minutes", 0}});
}
int model(int argc, char **argv) {
    auto file = fs::u8path(env("CYSYNCK_NETWORK_MODEL"));
    auto world = Json::parse(read_file(file));
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    world["calls"].push_back(args);
    atomic_write(file, world.dump());
    if (args.at(0) == "pubkey") {
        std::cout << world.at("private_public").get<std::string>();
        return 0;
    }
    if (args.at(0) == "show") {
        if (world.value("fail_inspect", false))
            return 1;
        if (args.at(1) == "interfaces")
            std::cout << world.at("interface").get<std::string>();
        else {
            if (args.at(1) != world.at("interface").get<std::string>())
                return 1;
            if (args.at(2) == "public-key")
                std::cout << world.at("public_key").get<std::string>();
            else if (args.at(2) == "listen-port")
                std::cout << world.value("listen_port", 51821);
            else if (args.at(2) != "latest-handshakes")
                return 1;
        }
        return 0;
    }
    if (args.at(0) == "set")
        return world.value("fail_set", false) || args.at(1) != world.at("interface").get<std::string>() ? 1 : 0;
    return 1;
}
} // namespace
int main(int argc, char **argv) {
    if (argc > 1) {
        try {
            return model(argc, argv);
        } catch (...) {
            return 22;
        }
    }
    auto root = fs::current_path() / "network-discovery-tests" / random_id();
    try {
        check(fs::create_directories(root), "Cannot reserve network fixture");
        crypto::generate_key(root / "remote.pem");
        auto signer = crypto::public_key(root / "remote.pem");
        auto remote = public_profile("remote", 'B', "10.77.0.2", signer);
        Json connection = {
            {"connected", true}, {"type", "tcp-client"}, {"address", "192.168.7.8:22000"}};
        check(direct_network_endpoint(remote, connection) == "192.168.7.8:51821",
              "Direct LAN candidate copied the Syncthing transport port");
        for (auto type : {"tcp-server", "quic-client", "quic-server"}) {
            connection["type"] = type;
            check(!direct_network_endpoint(remote, connection).empty(),
                  "A direct transport was not recognized");
        }
        for (auto type : {"relay-client", "relay-server", "unknown", ""}) {
            connection["type"] = type;
            check(direct_network_endpoint(remote, connection).empty(),
                  "A relay became a WireGuard endpoint");
        }
        connection["type"] = "tcp-client";
        for (auto host : {"203.0.113.8:22000", "100.64.0.4:22000", "127.0.0.1:22000",
                          "169.254.1.2:22000", "224.0.0.1:22000", "host.example:22000",
                          "192.168.1.2:0", "192.168.1.2:70000", "[::1]:22000", "[fe80::1]:22000",
                          "[::ffff:192.168.1.2]:22000", "[fe80::1%eth0]:22000"}) {
            connection["address"] = host;
            check(direct_network_endpoint(remote, connection).empty(),
                  "An unsafe/ambiguous direct address was accepted");
        }
        connection["address"] = "[2001:db8::7]:22000";
        check(direct_network_endpoint(remote, connection) == "[2001:db8::7]:51821",
              "IPv6 listener selection failed");
        connection["address"] = "[fd00::7]:22000";
        check(direct_network_endpoint(remote, connection) == "[fd00::7]:51821",
              "Direct ULA selection failed");
        connection["connected"] = false;
        check(direct_network_endpoint(remote, connection).empty(),
              "A disconnected device was selected");
        connection["connected"] = true;
        auto anonymous = remote;
        anonymous["device_id"] = "";
        check(direct_network_endpoint(anonymous, connection).empty(),
              "An unpinned device connection was used");

        auto now = int64_t(std::time(nullptr));
        auto identity = network_endpoint_identity(remote, "team");
        auto record = [&](int64_t sequence, const std::string &endpoint, int64_t expires) {
            return sign_endpoint_record(identity, endpoint, "pcp", sequence, now - 1, expires,
                                        root / "remote.pem");
        };
        Json checkpoint;
        auto first = record(1, "198.18.0.8:45000", now + 60);
        accept_network_endpoint(first, identity, checkpoint, now, root);
        auto same = checkpoint;
        accept_network_endpoint(first, identity, checkpoint, now, root);
        check(same == checkpoint, "Identical valid publication was not idempotent");
        refuses(
            [&] {
                accept_network_endpoint(record(1, "198.18.0.9:45000", now + 60), identity,
                                        checkpoint, now, root);
            },
            "Same-sequence endpoint substitution succeeded");
        auto second = record(2, "198.18.0.8:45001", now + 120);
        accept_network_endpoint(second, identity, checkpoint, now, root);
        refuses([&] { accept_network_endpoint(first, identity, checkpoint, now, root); },
                "Older signed endpoint replay succeeded");
        auto selected =
            resolved_network_endpoint(remote, Json{{"team", checkpoint}}, connection, now);
        check(selected.at("endpoint") == "198.18.0.8:45001" &&
                  selected.at("source") == "signed-pcp",
              "Verified assigned port did not take precedence over a LAN candidate");
        auto manual = remote;
        manual["endpoint"] = "manual.example:1234";
        check(resolved_network_endpoint(manual, Json{{"team", checkpoint}}, connection, now)
                      .at("source") == "manual",
              "Automatic endpoint replaced an explicitly pinned endpoint");
        connection["connected"] = false;
        check(resolved_network_endpoint(remote, Json{{"team", checkpoint}}, connection, now + 120)
                      .at("endpoint") == "",
              "An expired endpoint remained eligible");

        Core core(root / "state");
        project(core, root / "project", "team");
        project(core, root / "extra-project", "extra");
        auto registry = Json::parse(read_file(core.state() / "registry.json"));
        auto local = public_profile(registry.at("peer_id"), 'A', "10.77.0.1", signer);
        registry["network"] = {{"profile", local}, {"peers", Json::object()}};
        atomic_write(core.state() / "registry.json", registry.dump());
        auto tool = path_string(fs::absolute(fs::u8path(argv[0])));
        auto world_file = root / "world.json";
        auto logical = "cs-" + sha256(local.at("peer_id").get<std::string>()).substr(0, 12);
        auto mappings = root / "mappings";
        fs::create_directories(mappings);
        atomic_write(mappings / (logical + ".name"), "utun42\n");
        environment("CYSYNCK_WG_RUNTIME_DIRECTORY", path_string(mappings));
        Json world = {
            {"interface", "utun42"},
            {"public_key", local.at("public_key")},
            {"private_public", local.at("public_key")},
            {"calls", Json::array()}};
        atomic_write(world_file, world.dump());
        environment("CYSYNCK_NETWORK_MODEL", path_string(world_file));
        environment("CYSYNCK_WG", tool);
        environment("CYSYNCK_WIREGUARD", tool);
        environment("CYSYNCK_WG_QUICK", tool);
        Json trust = {
            {"project", "team"}, {"profile", remote}, {"expected_key", remote.at("public_key")}};
        refuses([&] { core.request("POST", "/api/network/trust", trust); },
                "Signer pairing did not require independent fingerprint confirmation");
        trust["expected_fingerprint"] = crypto::fingerprint(signer);
        core.request("POST", "/api/network/trust", trust);
        auto changed = trust;
        changed["profile"]["address"] = "10.77.0.3";
        refuses([&] { core.request("POST", "/api/network/trust", changed); },
                "Pairing silently changed a pinned host address");
        trust["project"] = "extra";
        core.request("POST", "/api/network/trust", trust);
        atomic_write(core.state() / "vpn/private.key", std::string(43, 'A') + "=\n", true);
        auto written = core.request("POST", "/api/network/write-config");
        auto config = fs::u8path(written.at("path").get<std::string>());
        auto exchange = root / "project/.cysynck-network/peers";
        fs::create_directories(exchange);
        atomic_write(exchange / "remote.endpoint.json", second);
        auto refreshed = core.request("POST", "/api/network/refresh", {{"project", "team"}});
        check(refreshed.at("errors").empty(), "Signed endpoint refresh failed");
        check(core.request("POST", "/api/network/preview")
                      .at("config")
                      .get<std::string>()
                      .find("Endpoint = 198.18.0.8:45001") != std::string::npos,
              "Verified endpoint did not reach the tunnel configuration");
        world = Json::parse(read_file(world_file));
        bool applied = false;
        for (const auto &call : world.at("calls"))
            if (call.at(0) == "set" && call.at(1) == "utun42" &&
                call.dump().find("198.18.0.8:45001") != std::string::npos)
                applied = true;
        check(applied, "Refresh did not update the owned live tunnel through wg set");
        check(read_file(config).find("Endpoint = 198.18.0.8:45001") != std::string::npos,
              "Live endpoint update was not preserved for service restart");
        auto third = record(3, "198.18.0.8:45002", now + 180);
        atomic_write(exchange / "remote.endpoint.json", third);
        world["public_key"] = std::string(42, 'C') + "A=";
        world["calls"] = Json::array();
        atomic_write(world_file, world.dump());
        refreshed = core.request("POST", "/api/network/refresh", {{"project", "team"}});
        world = Json::parse(read_file(world_file));
        bool mutated_foreign = false;
        for (auto &call : world.at("calls"))
            if (call.at(0) == "set")
                mutated_foreign = true;
        check(!refreshed.at("errors").empty() && !mutated_foreign &&
                  read_file(config).find("198.18.0.8:45002") == std::string::npos,
              "A foreign tunnel was mutated or its private configuration overwritten");
        world["public_key"] = local.at("public_key");
        atomic_write(world_file, world.dump());
        refreshed = core.request("POST", "/api/network/refresh", {{"project", "team"}});
        check(refreshed.at("errors").empty() &&
                  read_file(config).find("198.18.0.8:45002") != std::string::npos,
              "An owned tunnel did not recover after an ownership refusal");
        atomic_write(exchange / "remote.endpoint.json", first);
        Core restarted(core.state());
        auto replay = restarted.request("POST", "/api/network/refresh", {{"project", "team"}});
        check(!replay.at("errors").empty() &&
                  replay.at("peers").at(0).at("resolved").at("endpoint") == "198.18.0.8:45002",
              "Restart lost the private endpoint replay checkpoint");
        auto revoked = restarted.request("POST", "/api/network/revoke",
                                         {{"project", "team"}, {"peer", "remote"}});
        check(!revoked.at("tunnel_peer_removed").get<bool>(),
              "Project revocation removed a peer still used by another project");
        auto after = restarted.request("GET", "/api/network/status");
        check(after.at("peers").at("remote").at("projects") == Json::array({"extra"}) &&
                  after.at("peers").at("remote").at("resolved_endpoint").at("endpoint") == "",
              "Revocation retained another project's signed endpoint");
        check(read_file(config).find("198.18.0.8:45002") == std::string::npos,
              "A revoked project's endpoint could reappear after service restart");
        world["fail_set"] = true;
        atomic_write(world_file, world.dump());
        revoked = restarted.request("POST", "/api/network/revoke",
                                    {{"project", "extra"}, {"peer", "remote"}});
        check(revoked.at("requires_restart") == true &&
                  restarted.request("GET", "/api/network/status").at("peers").empty(),
              "Failed live revocation concealed the restart requirement or retained application "
              "trust");
        check(read_file(config).find(remote.at("public_key").get<std::string>()) ==
                  std::string::npos,
              "A revoked peer could return through the persisted tunnel configuration");
        registry = Json::parse(read_file(core.state() / "registry.json"));
        check(registry.at("network")
                  .at("pending_removals")
                  .contains(remote.at("public_key").get<std::string>()),
              "Failed revocation lost its durable removal intent");
        auto pending_status = restarted.request("GET", "/api/network/status");
        check(pending_status.at("requires_restart") == true && pending_status.at("pending_removals") == 1,
              "Status concealed an incomplete durable tunnel revocation");
        world["fail_set"] = false;
        atomic_write(world_file, world.dump());
        refreshed = restarted.request("POST", "/api/network/refresh");
        check(refreshed.at("requires_restart") == false,
              "Retry did not complete the owned tunnel revocation");
        auto calls_before = Json::parse(read_file(world_file)).at("calls").size();
        restarted.tick();
        check(Json::parse(read_file(world_file)).at("calls").size() == calls_before,
              "Default maintenance performed VPN changes without local opt-in");
        restarted.request("POST", "/api/network/automatic", {{"enabled", true}});
        restarted.tick();
        check(restarted.request("GET", "/api/network/status")
                      .at("endpoint_refresh")
                      .at("checked_at")
                      .get<int64_t>() >= now,
              "Opt-in automatic endpoint refresh did not run from Core::tick");
        restarted.request("POST", "/api/network/automatic", {{"enabled", false}});
        refuses([&] { restarted.request("GET", "/api/network/router", {{"action", "disable"}}); },
                "A read request mutated router mapping consent");
        // The production publisher must use the granted external port and lease
        // deadline, with an independently inspected live WG listener. The private
        // controller is injected here; UDP packet behavior has a separate fixture.
        registry = Json::parse(read_file(core.state() / "registry.json"));
        RouterLeaseContext context{local.at("peer_id").get<std::string>() + ":" +
                                       local.at("public_key").get<std::string>(), "127.0.0.1", 51821};
        Json lease = Json::object();
        enable_router_lease(lease, {{"gateway", "127.0.0.1"}, {"protocol", "pcp"}}, context, now, [] {});
        renew_router_lease(lease, context, now, true,
            [&](const std::string &, uint16_t port, uint16_t, uint32_t, const std::string &,
                const std::string &nonce, const std::string &, const std::string &) {
                return GatewayMapping{"pcp", "198.18.0.20", "127.0.0.1", port, 45019, 120, 100, nonce};
            }, [] {});
        registry["network"]["router_mapping"] = lease;
        atomic_write(core.state() / "registry.json", registry.dump());
        atomic_write(core.state() / "vpn/endpoint-signing.pem", read_file(root / "remote.pem"), true);
        auto mapped = restarted.request("POST", "/api/network/publish", {{"project", "team"}});
        auto mapped_path = fs::u8path(mapped.at("endpoint_path").get<std::string>());
        auto mapped_bytes = read_file(mapped_path);
        auto mapped_record = verify_endpoint_record(mapped_bytes, network_endpoint_identity(local, "team"), now, root);
        check(mapped_record.at("metadata").at("endpoint") == "198.18.0.20:45019" &&
                  mapped_record.at("metadata").at("source") == "pcp" &&
                  mapped_record.at("metadata").at("expires_at") <= now + 120 &&
                  mapped_bytes.find(lease.at("lease").at("nonce").get<std::string>()) == std::string::npos &&
                  restarted.request("GET", "/api/network/status").dump().find(
                      lease.at("lease").at("nonce").get<std::string>()) == std::string::npos,
              "Mapped publication lost its assigned port/deadline or exposed the private PCP nonce");
        world = Json::parse(read_file(world_file));
        world["listen_port"] = 51999;
        atomic_write(world_file, world.dump());
        refuses([&] { restarted.request("POST", "/api/network/publish", {{"project", "team"}}); },
                "A desired configuration port was used instead of the live WireGuard UDP port");
        check(read_file(mapped_path) == mapped_bytes, "A mismatched listener replaced a valid signed publication");
        world["listen_port"] = 51821;
        atomic_write(world_file, world.dump());
        registry = Json::parse(read_file(core.state() / "registry.json"));
        registry["network"]["router_mapping"]["lease"]["expires_at"] = now;
        registry["network"]["router_mapping"]["lease"]["uncertain_until"] = now;
        atomic_write(core.state() / "registry.json", registry.dump());
        restarted.request("POST", "/api/network/router", {{"action", "disable"}});
        check(!fs::exists(mapped_path), "Disabling an expired mapping left its local signed publication behind");
        // Exercise production publication, not just the signed-record primitive.
        registry = Json::parse(read_file(core.state() / "registry.json"));
        local["endpoint"] = "publisher.example:51821";
        registry["network"]["profile"] = local;
        atomic_write(core.state() / "registry.json", registry.dump());
        atomic_write(core.state() / "vpn/endpoint-signing.pem", read_file(root / "remote.pem"), true);
        auto published = restarted.request("POST", "/api/network/publish", {{"project", "team"}});
        auto published_bytes = read_file(fs::u8path(published.at("endpoint_path").get<std::string>()));
        auto verified = verify_endpoint_record(published_bytes, network_endpoint_identity(local, "team"),
                                              std::time(nullptr), root);
        check(verified.at("metadata").at("endpoint") == "publisher.example:51821" &&
                  verified.at("metadata").at("source") == "manual" &&
                  published_bytes.find("PRIVATE KEY") == std::string::npos,
              "Production endpoint publication leaked private material or failed signature checks");
        auto before_sequence = verified.at("metadata").at("sequence").get<int64_t>();
        Core publisher_restarted(core.state());
        published = publisher_restarted.request("POST", "/api/network/publish", {{"project", "team"}});
        verified = verify_endpoint_record(read_file(fs::u8path(published.at("endpoint_path").get<std::string>())),
                                          network_endpoint_identity(local, "team"), std::time(nullptr), root);
        check(verified.at("metadata").at("sequence").get<int64_t>() == before_sequence + 1,
              "Publisher restart reused an endpoint signature sequence");
        fs::remove_all(root);
        std::cout << "PASS direct IP/UDP separation, signed discovery, replay persistence, owned "
                     "live update and project-scoped revocation\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\nOwned fixture preserved: " << path_string(root) << '\n';
        return 1;
    }
}
