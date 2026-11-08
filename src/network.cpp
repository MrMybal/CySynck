#include <cysynck/core.hpp>
#include "inbox.hpp"
#include "transfers.hpp"
#include "vpn_tunnel.hpp"
#include "network_discovery.hpp"
#include "cyrevision_codec.hpp"
#include "router_lease.hpp"
#include "gateway_udp.hpp"
#include <ctime>
#include <fstream>
#include <httplib.h>
#include <regex>
#include <set>
#include <stdexcept>

namespace cysynck {
static std::string valid_id(const std::string &value) {
    if (!std::regex_match(value, std::regex("[A-Za-z0-9][A-Za-z0-9_-]{0,79}")))
        throw std::runtime_error("Invalid peer/project identifier");
    return value;
}
static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
        s.pop_back();
    return s;
}
static std::string wg_tool() { return env("CYSYNCK_WG", "wg"); }
static std::string activation_tool() {
#ifdef _WIN32
    return env("CYSYNCK_WIREGUARD", "C:/Program Files/WireGuard/wireguard.exe");
#else
    return env("CYSYNCK_WG_QUICK", "wg-quick");
#endif
}
static void key(const std::string &value) {
    if (!std::regex_match(value, std::regex("[A-Za-z0-9+/]{42}[AEIMQUYcgkosw048]=")))
        throw std::runtime_error("Invalid WireGuard public key");
}
static Json profile(const Json &data) {
    if (data.at("version") != 1)
        throw std::runtime_error("Unsupported public profile");
    auto address = data.at("address").get<std::string>();
    std::smatch match;
    if (!std::regex_match(address, match, std::regex("10\\.77\\.([0-9]{1,3})\\.([0-9]{1,3})")))
        throw std::runtime_error("Choose a unique VPN address inside 10.77.0.0/16");
    int a = std::stoi(match[1]), b = std::stoi(match[2]);
    if (a > 255 || b > 255 || (a == 0 && b == 0) || (a == 255 && b == 255))
        throw std::runtime_error("Invalid VPN host address");
    if (address != "10.77." + std::to_string(a) + "." + std::to_string(b))
        throw std::runtime_error("Use canonical VPN address notation");
    auto name = data.at("name").get<std::string>();
    if (name.empty() || name.size() > 160)
        throw std::runtime_error("Network display name required");
    auto endpoint = data.value("endpoint", std::string{});
    if (!endpoint.empty()) {
        std::smatch m;
        if (!std::regex_match(endpoint, m,
                              std::regex("([a-zA-Z0-9.-]+|\\[[a-fA-F0-9:]+\\]):([0-9]{1,5})")) ||
            std::stoi(m[2]) < 1 || std::stoi(m[2]) > 65535)
            throw std::runtime_error("Endpoint must be host:UDP-port, IPv6 in brackets");
    }
    for (auto field : {"listen_port", "mesh_port"})
        if (!data.at(field).is_number_integer() || data.at(field).get<int>() < 1 ||
            data.at(field).get<int>() > 65535)
            throw std::runtime_error("Invalid VPN/mesh port");
    auto publicKey = data.at("public_key").get<std::string>();
    key(publicKey);
    auto device = data.value("device_id", std::string{});
    if (!device.empty() && !std::regex_match(device, std::regex("[A-Z2-7-]{56,80}")))
        throw std::runtime_error("Invalid Syncthing device ID");
    Json normalized = {{"version", 1},
            {"peer_id", valid_id(data.at("peer_id"))},
            {"name", name},
            {"address", address},
            {"endpoint", endpoint},
            {"public_key", publicKey},
            {"listen_port", data.at("listen_port")},
            {"mesh_port", data.at("mesh_port")},
            {"device_id", device}};
    if (data.contains("signing_key")) {
        auto signer = data.at("signing_key").get<std::string>();
        (void)cyrevision::fingerprint(signer);
        normalized["signing_key"] = signer;
    }
    return normalized;
}
static fs::path exchange_dir(const Json &p) {
    return project_child(
        fs::u8path(
            p.at(p.at("mode") == "git-bundles" ? "exchange_path" : "root").get<std::string>()),
        ".cysynck-network/peers");
}
Json Core::network_action(const std::string &action, const Json &data) {
    if (!registry_.contains("network"))
        registry_["network"] = {{"peers", Json::object()}};
    auto &network = registry_["network"];
    auto directory = project_child(state_, "vpn");
    fs::create_directories(directory);
    auto tunnel_name = vpn_interface_name(registry_.at("peer_id").get<std::string>());
    auto configuration = project_child(directory, tunnel_name + ".conf");
    if (action == "configure") {
        if (network.value("router_mapping", Json::object()).value("enabled", false) ||
            network.value("router_mapping", Json::object()).value("phase", "") == "cleanup-pending")
            throw std::runtime_error("Disable router mapping before reconfiguring the VPN identity");
        auto privateFile = project_child(directory, "private.key");
        if (!fs::exists(privateFile)) {
            auto result = process({wg_tool(), "genkey"});
            if (result.code)
                throw std::runtime_error("WireGuard key generation failed");
            key(trim(result.out));
            atomic_write(privateFile, trim(result.out) + "\n", true);
        }
        auto result = process({wg_tool(), "pubkey"}, {}, read_file(privateFile, 128));
        if (result.code)
            throw std::runtime_error("WireGuard public key derivation failed");
        auto p = profile({{"version", 1},
                          {"peer_id", registry_.at("peer_id")},
                          {"name", data.at("name")},
                          {"address", data.at("address")},
                          {"endpoint", data.value("endpoint", std::string{})},
                          {"listen_port", data.value("listen_port", 51820)},
                          {"mesh_port", data.value("mesh_port", 8485)},
                          {"public_key", trim(result.out)},
                          {"device_id", data.value("device_id", std::string{})}});
        auto signing_file = project_child(directory, "endpoint-signing.pem");
        if (!fs::exists(signing_file)) cyrevision::generate_key(signing_file);
        p["signing_key"] = cyrevision::public_key(signing_file);
        for (auto &peer : network["peers"])
            if (peer["profile"]["address"] == p["address"])
                throw std::runtime_error("VPN address already assigned to a peer");
        network["profile"] = p;
        return p;
    }
    if (action == "history") {
        project(data.at("project"));
        Json output = Json::array();
        for (auto &message : registry_["messages"])
            if (message.at("project") == data.at("project"))
                output.push_back(message);
        std::reverse(output.begin(), output.end());
        return output;
    }
    auto render = [&](bool secret, bool private_field = true) {
        auto local = profile(network.at("profile"));
        std::string out = "[Interface]\n";
        if (private_field) out += "PrivateKey = " +
            (secret ? trim(read_file(project_child(directory, "private.key"), 128)) : "<local private key>") + "\n";
        out += "Address = " + local["address"].get<std::string>() +
            "/32\nListenPort = " + std::to_string(local["listen_port"].get<int>()) + "\n";
        for (auto &peer : network["peers"]) {
            auto p = profile(peer.at("profile"));
            out += "\n[Peer]\nPublicKey = " + p["public_key"].get<std::string>() +
                   "\nAllowedIPs = " + p["address"].get<std::string>() + "/32\n";
            auto endpoint = effective_network_endpoint(peer, std::time(nullptr));
            if (!endpoint.empty())
                out += "Endpoint = " + endpoint +
                       "\nPersistentKeepalive = 25\n";
        }
        return out;
    };
    auto safe_update = [&]() {
        if (!fs::exists(configuration)) return std::string{};
        auto derived = process({wg_tool(), "pubkey"}, {}, read_file(project_child(directory, "private.key"), 128));
        if (derived.code || trim(derived.out) != profile(network.at("profile")).at("public_key").get<std::string>())
            throw std::runtime_error("Local WireGuard public identity differs from its private key");
        return render(true);
    };
    auto router_context = [&](const std::string &gateway) {
        auto local = profile(network.at("profile"));
        auto port = vpn_owned_listen_port(wg_tool(), activation_tool(), tunnel_name,
                                          local.at("public_key"), configuration);
        if (port != local.at("listen_port").get<uint16_t>())
            throw std::runtime_error("The live WireGuard UDP port differs from its published profile; restart the owned tunnel first");
        GatewayUdp route(gateway, 5351);
        return RouterLeaseContext{local.at("peer_id").get<std::string>() + ":" +
                                      local.at("public_key").get<std::string>(), route.local_address(), port};
    };
    RouterMapper mapper = [](const std::string &gateway, uint16_t internal, uint16_t external,
                             uint32_t lifetime, const std::string &protocol,
                             const std::string &nonce, const std::string &address, const std::string &source) {
        return map_gateway(gateway, internal, external, lifetime, protocol, nonce, address, 5351, source);
    };
    auto mapped_endpoint = [&]() -> Json {
        auto state = network.value("router_mapping", Json::object());
        if (!state.value("enabled", false)) return Json();
        auto context = router_context(state.at("options").at("gateway"));
        return router_lease_endpoint(state, context, std::time(nullptr));
    };
    if (action == "status") {
        Json handshakes = Json::array();
        Json tunnel;
        std::string error;
        try {
            if (network.contains("profile")) {
                tunnel = vpn_tunnel_status(wg_tool(), tunnel_name, profile(network.at("profile")).at("public_key"), configuration);
                handshakes = tunnel.at("handshakes");
                if (tunnel.at("present") == true && tunnel.at("owned") != true)
                    error = "A foreign tunnel occupies this profile's interface name";
                else if (tunnel.at("present") != true && tunnel.at("legacy").at("owned") != true)
                    error = "The profile's WireGuard tunnel is not active";
            }
        } catch (const std::exception &e) {
            error = e.what();
        }
        return {{"profile", network.value("profile", Json(nullptr))},
                {"peers", network["peers"]},
                {"handshakes", handshakes},
                {"interface", tunnel_name},
                {"tunnel", tunnel},
                {"endpoint_refresh_enabled", network.value("endpoint_refresh_enabled", false)},
                {"endpoint_refresh", network.value("endpoint_refresh", Json(nullptr))},
                {"router_mapping", router_lease_status(network.value("router_mapping", Json::object()), std::time(nullptr))},
                {"requires_restart", !network.value("pending_removals", Json::object()).empty() ||
                    network.value("endpoint_refresh", Json::object()).value("requires_restart", false)},
                {"pending_removals", network.value("pending_removals", Json::object()).size()},
                {"signing_fingerprint", network.contains("profile") && network["profile"].contains("signing_key")
                    ? cyrevision::fingerprint(network["profile"]["signing_key"]) : ""},
                {"status_error", error},
                {"config_preview", network.contains("profile") ? render(false) : ""}};
    }
    if (!network.contains("profile"))
        throw std::runtime_error("Configure the local WireGuard identity first");
    if (action == "router") {
        auto command = data.at("action").get<std::string>();
        auto &state = network["router_mapping"];
        if (!state.is_object()) state = Json::object();
        auto now = int64_t(std::time(nullptr));
        Json result;
        if (command == "enable") {
            if (!network.at("profile").at("endpoint").get<std::string>().empty())
                throw std::runtime_error("Clear the manual UDP endpoint before enabling router mapping");
            auto context = router_context(data.at("gateway"));
            enable_router_lease(state, data, context, now, [&] { save(); });
            result = renew_router_lease(state, context, now, true, mapper, [&] { save(); });
        } else if (command == "renew") {
            auto context = router_context(state.at("options").at("gateway"));
            result = renew_router_lease(state, context, now, !data.value("scheduled", false),
                                        mapper, [&] { save(); });
        } else if (command == "disable") {
            RouterLeaseContext context;
            if (state.contains("lease"))
                try { context = router_context(state.at("lease").at("gateway")); }
                catch (const std::exception &) { /* No owned listener: never delete another mapping. */ }
            result = disable_router_lease(state, context, now, mapper, [&] { save(); });
        } else throw std::runtime_error("Choose router enable, renew or disable");
        // Only projects explicitly published locally may receive automatic records.
        // Empty/disabled leases withdraw the local sidecar; already copied signed
        // records expire at their original bounded deadline.
        Json issues = Json::array();
        auto publications = network.value("endpoint_projects", Json::array());
        for (const auto &id : publications)
            try { network_action("publish", {{"project", id}}); }
            catch (const std::exception &e) { issues.push_back({{"project", id}, {"error", e.what()}}); }
        result["publication_errors"] = issues;
        return result;
    }
    if (action == "apparmor") {
#ifdef __linux__
        return {{"supported", true}, {"configuration", path_string(configuration)},
                {"rules", vpn_apparmor_rules(path_string(configuration))},
                {"profile_candidates", {"/etc/apparmor.d/wg-quick", "/etc/apparmor.d/usr.bin.wg-quick"}},
                {"note", "Add these exact read-only rules to the installed profile's local include and reload it as root; no policy is changed by this command"}};
#else
        return {{"supported", false}, {"note", "AppArmor policy export is available on Linux"}};
#endif
    }
    if (action == "automatic") {
        if (!data.at("enabled").is_boolean()) throw std::runtime_error("Choose automatic endpoint refresh true or false");
        network["endpoint_refresh_enabled"] = data.at("enabled");
        if (data.at("enabled") == true) network["endpoint_refresh"] = {{"checked_at", 0}};
        return {{"enabled", data.at("enabled")}, {"requires_tick", true}};
    }
    if (action == "refresh") {
        const auto now = int64_t(std::time(nullptr));
        auto wanted = data.value("project", std::string{});
        if (!wanted.empty()) project(wanted);
        Json connections = Json::object(), errors = Json::array(), output = Json::array(), tunnel_errors = Json::array();
        if (engine_config().at("key_configured").get<bool>())
            try { connections = engine_request("GET", "system/connections").at("connections"); }
            catch (const std::exception &e) { errors.push_back({{"source", "syncthing"}, {"error", e.what()}}); }
        for (auto &[id, peer] : network["peers"].items()) {
            auto pinned = profile(peer.at("profile"));
            if (!peer.contains("endpoint_checkpoints")) peer["endpoint_checkpoints"] = Json::object();
            for (const auto &project_id : peer.at("projects")) {
                auto name = project_id.get<std::string>();
                if ((!wanted.empty() && wanted != name) || !pinned.contains("signing_key")) continue;
                try {
                    auto publication = project_child(exchange_dir(project(name)), id + ".endpoint.json");
                    if (fs::exists(publication))
                        accept_network_endpoint(read_file(publication, 16384), network_endpoint_identity(pinned, name),
                                                peer["endpoint_checkpoints"][name], now, directory);
                } catch (const std::exception &e) {
                    errors.push_back({{"peer", id}, {"project", name}, {"error", e.what()}});
                }
            }
            peer["resolved_endpoint"] = resolved_network_endpoint(pinned, peer["endpoint_checkpoints"],
                connections.value(pinned.at("device_id").get<std::string>(), Json::object()), now);
            output.push_back({{"peer", id}, {"resolved", peer["resolved_endpoint"]}});
        }
        if (!network.contains("applied_peers")) network["applied_peers"] = Json::object();
        if (!network.contains("pending_removals")) network["pending_removals"] = Json::object();
        auto local = profile(network.at("profile"));
        auto apply = [&](const std::string &key, const std::string &address, const std::string &endpoint,
                         bool remove, bool clear) {
            return vpn_set_peer(wg_tool(), activation_tool(), tunnel_name, local.at("public_key"),
                                configuration, key, address, endpoint, remove, clear, {}, safe_update());
        };
        // Persist accepted counters and revocations before a possibly interrupted tool call.
        save();
        std::vector<std::string> removed;
        for (auto &[key, ignored] : network["pending_removals"].items()) {
            try {
                apply(key, "", "", true, false);
                removed.push_back(key);
                network["applied_peers"].erase(key);
            } catch (const std::exception &e) {
                Json issue = {{"public_key", key}, {"error", e.what()}};
                errors.push_back(issue); tunnel_errors.push_back(issue);
            }
        }
        for (const auto &key : removed) network["pending_removals"].erase(key);
        for (auto &[id, peer] : network["peers"].items()) {
            auto pinned = profile(peer.at("profile"));
            auto key = pinned.at("public_key").get<std::string>();
            if (network["pending_removals"].contains(key)) continue;
            auto endpoint = effective_network_endpoint(peer, now);
            Json desired = {{"address", pinned.at("address")}, {"endpoint", endpoint}};
            if (network["applied_peers"].value(key, Json()) == desired) continue;
            try {
                auto result = apply(key, pinned.at("address"), endpoint, false,
                                    endpoint.empty());
                if (result.at("active") == true) network["applied_peers"][key] = desired;
                else network["applied_peers"].erase(key);
            } catch (const std::exception &e) {
                Json issue = {{"peer", id}, {"error", e.what()}};
                errors.push_back(issue); tunnel_errors.push_back(issue);
            }
        }
        network["endpoint_refresh"] = {{"checked_at", now}, {"peers", output}, {"errors", errors},
                                       {"tunnel_errors", tunnel_errors},
                                       {"requires_restart", !network["pending_removals"].empty() || !tunnel_errors.empty()}};
        return network["endpoint_refresh"];
    }
    if (action == "preview")
        return {{"config", render(false)}, {"interface", tunnel_name}};
    if (action == "write-config" || action == "up" || action == "down") {
        if (action != "down") {
            auto derived = process({wg_tool(), "pubkey"}, {}, read_file(project_child(directory, "private.key"), 128));
            if (derived.code || trim(derived.out) != profile(network.at("profile")).at("public_key").get<std::string>())
                throw std::runtime_error("Local WireGuard public identity differs from its private key");
        }
        if (action == "write-config") {
            atomic_write(configuration, render(true), true);
            return {{"path", path_string(configuration)}, {"interface", tunnel_name}};
        }
        Json router;
        if (action == "down" && network.contains("router_mapping"))
            router = network_action("router", {{"action", "disable"}});
        auto result = vpn_tunnel_control(action, wg_tool(), activation_tool(), tunnel_name,
                                  profile(network.at("profile")).at("public_key"), configuration,
                                  action == "down" ? render(false, false) : render(true));
        network["applied_peers"] = Json::object();
        if (!router.is_null()) result["router_mapping"] = router;
        return result;
    }
    auto &p = project(data.at("project"));
    auto path = exchange_dir(p);
    if (action == "publish") {
        auto local = profile(network.at("profile"));
        if (local["device_id"] == "" && engine_config().at("key_configured").get<bool>()) {
            local["device_id"] = engine_request("GET", "system/status").at("myID");
            local = profile(local);
            network["profile"] = local;
        }
        fs::create_directories(path);
        auto output = path / (local["peer_id"].get<std::string>() + ".json");
        atomic_write(output, local.dump(2));
        Json result = {{"profile", local}, {"path", path_string(output)}};
        if (!network.contains("endpoint_projects")) network["endpoint_projects"] = Json::array();
        auto &publications = network["endpoint_projects"];
        if (std::find(publications.begin(), publications.end(), p.at("id")) == publications.end()) {
            if (publications.size() >= 256) throw std::runtime_error("Automatic endpoint publication project limit reached");
            publications.push_back(p.at("id"));
        }
        auto now = int64_t(std::time(nullptr));
        Json endpoint;
        if (!local.at("endpoint").get<std::string>().empty())
            endpoint = {{"endpoint", local.at("endpoint")}, {"source", "manual"}, {"expires_at", now + 900}};
        else endpoint = mapped_endpoint();
        auto signed_path = project_child(path, local["peer_id"].get<std::string>() + ".endpoint.json");
        if (local.contains("signing_key") && !endpoint.is_null()) {
            auto sequence = network.value("endpoint_sequence", int64_t(0));
            if (sequence < 0 || sequence == INT64_MAX) throw std::runtime_error("Endpoint sequence exhausted");
            network["endpoint_sequence"] = ++sequence;
            save();
            atomic_write(signed_path, sign_endpoint_record(network_endpoint_identity(local, p.at("id")),
                endpoint.at("endpoint"), endpoint.at("source"), sequence, now, endpoint.at("expires_at"),
                project_child(directory, "endpoint-signing.pem")));
            result["endpoint_path"] = path_string(signed_path);
            result["endpoint_expires_at"] = endpoint.at("expires_at");
            result["endpoint_source"] = endpoint.at("source");
        } else if (fs::exists(signed_path)) {
            if (path_is_link(signed_path) || !fs::is_regular_file(signed_path))
                throw std::runtime_error("Refuse to replace a linked or invalid endpoint publication");
            fs::remove(signed_path);
        }
        return result;
    }
    if (action == "discover") {
        Json connections = Json::object(), output = Json::array();
        std::string error;
        if (engine_config().at("key_configured").get<bool>())
            try {
                connections = engine_request("GET", "system/connections").at("connections");
            } catch (const std::exception &e) {
                error = e.what();
            }
        if (fs::exists(path))
            for (auto &entry : fs::directory_iterator(path))
                if (entry.path().extension() == ".json" && !fs::is_symlink(entry.symlink_status()))
                    try {
                        auto candidate = profile(Json::parse(read_file(entry.path(), 16384)));
                        auto peer = candidate["peer_id"].get<std::string>();
                        if (peer == registry_["peer_id"].get<std::string>() ||
                            path_string(entry.path().stem()) != peer)
                            continue;
                        auto connection = connections.value(
                            candidate["device_id"].get<std::string>(), Json::object());
                        bool trusted = network["peers"].contains(peer) &&
                                       network["peers"][peer]["profile"] == candidate &&
                                       std::find(network["peers"][peer]["projects"].begin(),
                                                 network["peers"][peer]["projects"].end(), p.at("id")) !=
                                           network["peers"][peer]["projects"].end();
                        output.push_back(
                            {{"profile", candidate},
                             {"trusted", trusted},
                             {"syncthing_connected", connection.value("connected", false)},
                             {"syncthing_transport", connection.value("type", std::string{})},
                             {"observed_sync_address",
                              connection.value("address", std::string{})},
                             {"signing_fingerprint", candidate.contains("signing_key")
                                ? cyrevision::fingerprint(candidate.at("signing_key")) : ""}});
                    } catch (const std::exception &) {
                    }
        return {{"peers", output},
                {"discovery_error", error},
                {"note", "Syncthing addresses are hints; transport and relay ports "
                         "are not WireGuard endpoints"}};
    }
    if (action == "trust") {
        auto candidate = profile(data.at("profile"));
        auto peer = candidate["peer_id"].get<std::string>();
        auto local = profile(network.at("profile"));
        if (data.at("expected_key") != candidate["public_key"])
            throw std::runtime_error("Confirm the peer public key before pairing");
        if (peer == registry_["peer_id"].get<std::string>() ||
            candidate["address"] == local["address"] ||
            candidate["public_key"] == local["public_key"])
            throw std::runtime_error("Peer identity must be unique");
        for (auto &[other, entry] : network["peers"].items())
            if (other != peer && (entry["profile"]["address"] == candidate["address"] ||
                                  entry["profile"]["public_key"] == candidate["public_key"]))
                throw std::runtime_error("Duplicate peer address/key");
        if (candidate.contains("signing_key") &&
            data.value("expected_fingerprint", std::string{}) != cyrevision::fingerprint(candidate.at("signing_key")))
            throw std::runtime_error("Independently confirm the endpoint signing fingerprint before pairing");
        if (network["peers"].contains(peer) && network["peers"][peer]["profile"] != candidate)
            throw std::runtime_error("Pinned peer identity changed; revoke and pair again");
        if (!network["peers"].contains(peer) && network["peers"].size() >= 256)
            throw std::runtime_error("VPN peer limit (256) reached");
        auto projects =
            network["peers"].contains(peer) ? network["peers"][peer]["projects"] : Json::array();
        bool exists = false;
        for (auto &value : projects)
            if (value == p.at("id"))
                exists = true;
        if (!exists)
            projects.push_back(p.at("id"));
        if (!network["peers"].contains(peer)) network["peers"][peer] = {{"profile", candidate}};
        network["peers"][peer]["projects"] = projects;
        event(p.at("id"), "vpn", "Peer public key pinned: " + peer);
        return {{"peer", peer}, {"trusted", true}};
    }
    if (action == "revoke") {
        auto peer = valid_id(data.at("peer"));
        if (!network["peers"].contains(peer)) return {{"revoked", peer}, {"requires_restart", false}};
        auto &entry = network["peers"][peer];
        auto &projects = entry["projects"];
        projects.erase(std::remove(projects.begin(), projects.end(), p.at("id")), projects.end());
        if (entry.contains("endpoint_checkpoints")) entry["endpoint_checkpoints"].erase(p.at("id").get<std::string>());
        entry["resolved_endpoint"] = resolved_network_endpoint(profile(entry.at("profile")),
            entry.value("endpoint_checkpoints", Json::object()), Json::object(), std::time(nullptr));
        bool remove = projects.empty();
        std::string error;
        if (remove) {
            auto key = entry["profile"]["public_key"].get<std::string>();
            network["peers"].erase(peer);
            if (!network.contains("pending_removals")) network["pending_removals"] = Json::object();
            network["pending_removals"][key] = true;
            save();
            try {
                vpn_set_peer(wg_tool(), activation_tool(), tunnel_name, network["profile"]["public_key"],
                             configuration, key, "", "", true, false, {}, safe_update());
                network["pending_removals"].erase(key);
                if (network.contains("applied_peers")) network["applied_peers"].erase(key);
            } catch (const std::exception &e) { error = e.what(); }
        } else {
            auto refreshed = network_action("refresh", Json::object());
            if (!refreshed.at("errors").empty()) error = refreshed.at("errors").dump();
        }
        return {{"revoked", peer}, {"project", p.at("id")}, {"tunnel_peer_removed", remove && error.empty()},
                {"requires_restart", !error.empty()}, {"error", error}};
    }
    throw std::runtime_error("Unknown network action");
}
void Core::maintain_network() {
    if (!registry_.contains("network")) return;
    auto now = int64_t(std::time(nullptr));
    auto &router = registry_["network"]["router_mapping"];
    if (!router.is_object()) router = Json::object();
    if (router_lease_due(router, now)) {
        try {
            network_action("router", {{"action", router.value("enabled", false) ? "renew" : "disable"},
                                       {"scheduled", true}});
        } catch (const std::exception &e) {
            router["error"] = e.what();
            router["retry_at"] = now + 45;
            router["last_attempt_at"] = now;
        }
    }
    if (!registry_["network"].value("endpoint_refresh_enabled", false)) return;
    auto last = registry_["network"].value("endpoint_refresh", Json::object()).value("checked_at", int64_t(0));
    if (now >= last && now - last < 45) return;
    try { network_action("refresh", Json::object()); }
    catch (const std::exception &e) {
        registry_["network"]["endpoint_refresh"] = {{"checked_at", now},
            {"errors", Json::array({{{"error", e.what()}}})}, {"requires_restart", true}};
    }
}
const Json &trusted_peer(const Json &registry, const std::string &project,
                         const std::string &peer) {
    valid_id(peer);
    auto &entry = registry.at("network").at("peers").at(peer);
    bool trusted = false;
    for (auto &p : entry.at("projects"))
        if (p == project)
            trusted = true;
    if (!trusted)
        throw std::runtime_error("Peer is not authorized for this project");
    return entry.at("profile");
}
Json Core::direct_send(const Json &data, bool file) {
    if (file && !data.value("legacy", false))
        return transfer_send(data);
    auto token = env("CYSYNCK_MESH_TOKEN");
    if (token.size() < 24)
        throw std::runtime_error("Set the same CYSYNCK_MESH_TOKEN (at least 24 "
                                 "random characters) on paired devices");
    std::string body, messageId = random_id(), sender, localAddress;
    Json peer, entry;
    auto projectId = data.at("project").get<std::string>(),
         peerId = data.at("peer").get<std::string>();
    {
        std::lock_guard lock(mutex_);
        DiskLock disk(state_);
        load();
        auto &p = project(projectId);
        peer = trusted_peer(registry_, projectId, peerId);
        localAddress = registry_.at("network").at("profile").at("address");
        sender = registry_.at("peer_id");
        if (file) {
            auto relative = data.at("path").get<std::string>();
            auto path = project_child(fs::u8path(p.at("root").get<std::string>()), relative);
            if (fs::file_size(path) > 100 * 1024 * 1024)
                throw std::runtime_error("Direct files are limited to 100 MiB");
            auto limit = transfer_limits(registry_).at("file_limit_mib").get<uintmax_t>() *
                         transfer_chunk_size;
            if (fs::file_size(path) > limit)
                throw std::runtime_error("File exceeds the configured transfer limit");
            entry = capture(path);
            if (entry.at("size").get<uintmax_t>() > limit)
                throw std::runtime_error("Captured file exceeds the configured transfer limit");
            body = read_file(state_ / "objects" / entry["sha256"].get<std::string>(),
                             100 * 1024 * 1024);
            entry["name"] = path_string(path.filename());
        } else {
            auto text = data.at("text").get<std::string>();
            if (text.empty() || text.size() > 16000)
                throw std::runtime_error("Messages must contain 1 to 16000 UTF-8 bytes");
            entry = {{"text", text}};
            body = Json{{"id", messageId}, {"project", projectId}, {"text", text}}.dump();
        }
    }
    httplib::Client client(peer.at("address").get<std::string>(), peer.at("mesh_port").get<int>());
    client.set_socket_options([localAddress](auto socket) {
        if (!httplib::detail::bind_ip_address(socket, localAddress))
            throw std::runtime_error(
                "The local VPN address is unavailable; connect WireGuard first");
    });
    client.set_connection_timeout(10);
    client.set_read_timeout(60);
    client.set_write_timeout(60);
    client.set_follow_location(false);
    httplib::Headers headers{{"Authorization", "Bearer " + token},
                             {"X-CySynck-Peer", sender},
                             {"X-CySynck-Project", projectId},
                             {"X-CySynck-Id", messageId}};
    if (file) {
        headers.emplace("X-CySynck-SHA256", entry.at("sha256").get<std::string>());
        headers.emplace("X-CySynck-Name",
                        httplib::detail::encode_url(entry.at("name").get<std::string>()));
    }
    auto response = client.Post(file ? "/file" : "/message", headers, body,
                                file ? "application/octet-stream" : "application/json");
    if (!response || response->status != 200)
        throw std::runtime_error("Direct peer unreachable or transfer refused; "
                                 "check tunnel, pairing secret and receiver");
    {
        std::lock_guard lock(mutex_);
        DiskLock disk(state_);
        load();
        registry_["messages"].push_back({{"id", messageId},
                                         {"created", timestamp()},
                                         {"project", projectId},
                                         {"peer", peerId},
                                         {"direction", "out"},
                                         {"kind", file ? "file" : "chat"},
                                         {"body", entry}});
        if (registry_["messages"].size() > 2000)
            registry_["messages"].erase(registry_["messages"].begin());
        save();
    }
    return {{"id", messageId}, {"delivered", true}, {"bytes", body.size()}};
}
Json Core::direct_receive(const Json &data, const std::string &remote, const std::string &bytes) {
    std::lock_guard lock(mutex_);
    DiskLock disk(state_);
    load();
    auto projectId = valid_id(data.at("project")), peerId = valid_id(data.at("peer")),
         messageId = valid_id(data.at("id"));
    project(projectId);
    auto peer = trusted_peer(registry_, projectId, peerId);
    if (peer.at("address") != remote)
        throw std::runtime_error("Sender address does not match pinned VPN identity");
    if (data.contains("transfer_action"))
        return transfer_receive(data, bytes);
    bool file = data.value("file", false);
    auto receipts = received_records(registry_, projectId);
    if (receipts.contains(messageId)) {
        auto receipt = received_record(receipts.at(messageId), projectId, messageId);
        if (!file || receipt.at("peer") != peerId || receipt.at("name") != data.at("name") ||
            receipt.at("sha256") != data.at("sha256") ||
            receipt.at("bytes").get<uintmax_t>() != bytes.size() ||
            sha256(bytes) != receipt.at("sha256").get<std::string>())
            throw std::runtime_error("Transfer ID was already used with different content");
        verify_received(received_path(state_, projectId, messageId), receipt);
        return {{"id", messageId}, {"delivered", true}, {"duplicate", true}};
    }
    for (auto &m : registry_["messages"])
        if (m.at("id") == messageId) {
            if (m.at("peer") != peerId || m.at("project") != projectId || file ||
                m.at("kind") != "chat" || m.at("body").at("text") != data.at("text"))
                throw std::runtime_error("Duplicate message ID");
            return {{"id", messageId}, {"delivered", true}, {"duplicate", true}};
        }
    Json body;
    if (file) {
        if (bytes.size() > 100 * 1024 * 1024 ||
            sha256(bytes) != data.at("sha256").get<std::string>())
            throw std::runtime_error("File too large or checksum mismatch");
        auto name = data.at("name").get<std::string>();
        received_name(name);
        if (receipts.size() + registry_.value("transfers_in", Json::object())
                                  .value(projectId, Json::object())
                                  .size() >=
            10000)
            throw std::runtime_error("Received file catalog limit (10000 per project) reached");
        check_incoming_capacity(state_, registry_, bytes.size());
        auto destination = received_path(state_, projectId, messageId);
        if (fs::exists(destination))
            throw std::runtime_error("Transfer ID already exists");
        atomic_write(destination, bytes, true);
        body = {{"name", name},
                {"bytes", bytes.size()},
                {"sha256", data.at("sha256")},
                {"path", path_string(destination)}};
        auto receipt = body;
        receipt.erase("path");
        receipt["id"] = messageId;
        receipt["project"] = projectId;
        receipt["peer"] = peerId;
        receipt["created"] = timestamp();
        receipts[messageId] = receipt;
        registry_["received_files"][projectId] = receipts;
    } else {
        auto text = data.at("text").get<std::string>();
        if (text.empty() || text.size() > 16000)
            throw std::runtime_error("Invalid direct message size");
        body = {{"text", text}};
    }
    registry_["messages"].push_back({{"id", messageId},
                                     {"created", timestamp()},
                                     {"project", projectId},
                                     {"peer", peerId},
                                     {"direction", "in"},
                                     {"kind", file ? "file" : "chat"},
                                     {"body", body}});
    if (registry_["messages"].size() > 2000)
        registry_["messages"].erase(registry_["messages"].begin());
    save();
    return {{"id", messageId}, {"delivered", true}};
}
} // namespace cysynck
