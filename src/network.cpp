#include <cysynck/core.hpp>
#include "inbox.hpp"
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
    return {{"version", 1},
            {"peer_id", valid_id(data.at("peer_id"))},
            {"name", name},
            {"address", address},
            {"endpoint", endpoint},
            {"public_key", publicKey},
            {"listen_port", data.at("listen_port")},
            {"mesh_port", data.at("mesh_port")},
            {"device_id", device}};
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
    auto directory = state_ / "vpn";
    fs::create_directories(directory);
    if (action == "configure") {
        auto privateFile = directory / "private.key";
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
    auto render = [&](bool secret) {
        auto local = profile(network.at("profile"));
        std::string out =
            "[Interface]\nPrivateKey = " +
            (secret ? trim(read_file(directory / "private.key", 128)) : "<local private key>") +
            "\nAddress = " + local["address"].get<std::string>() +
            "/32\nListenPort = " + std::to_string(local["listen_port"].get<int>()) + "\n";
        for (auto &peer : network["peers"]) {
            auto p = profile(peer.at("profile"));
            out += "\n[Peer]\nPublicKey = " + p["public_key"].get<std::string>() +
                   "\nAllowedIPs = " + p["address"].get<std::string>() + "/32\n";
            if (!p["endpoint"].get<std::string>().empty())
                out += "Endpoint = " + p["endpoint"].get<std::string>() +
                       "\nPersistentKeepalive = 25\n";
        }
        return out;
    };
    if (action == "status") {
        Json handshakes = Json::array();
        std::string error;
        try {
            auto r = process({wg_tool(), "show", "cysynck", "latest-handshakes"}, {}, {}, 10);
            if (!r.code) {
                std::istringstream rows(r.out);
                std::string publicKey;
                long long time;
                while (rows >> publicKey >> time)
                    handshakes.push_back(
                        {{"public_key", publicKey},
                         {"timestamp", time},
                         {"recent", time != 0 && std::time(nullptr) - time < 180}});
            } else
                error = "Tunnel absent or permission to query it is unavailable";
        } catch (const std::exception &e) {
            error = e.what();
        }
        return {{"profile", network.value("profile", Json(nullptr))},
                {"peers", network["peers"]},
                {"handshakes", handshakes},
                {"status_error", error},
                {"config_preview", network.contains("profile") ? render(false) : ""}};
    }
    if (!network.contains("profile"))
        throw std::runtime_error("Configure the local WireGuard identity first");
    if (action == "preview")
        return {{"config", render(false)}};
    if (action == "write-config" || action == "up" || action == "down") {
        auto config = directory / "cysynck.conf";
        atomic_write(config, render(true), true);
        if (action == "write-config")
            return {{"path", path_string(config)}};
        std::vector<std::string> args;
#ifdef _WIN32
        auto tool = env("CYSYNCK_WIREGUARD", "C:/Program Files/WireGuard/wireguard.exe");
        args = action == "up"
                   ? std::vector<std::string>{tool, "/installtunnelservice", path_string(config)}
                   : std::vector<std::string>{tool, "/uninstalltunnelservice", "cysynck"};
#else
        args = {env("CYSYNCK_WG_QUICK", "wg-quick"), action, path_string(config)};
#endif
        if (process(args, {}, {}, 45).code)
            throw std::runtime_error("WireGuard requires administrator/root rights; "
                                     "check the WireGuard logs");
        return {{"requested", action}, {"interface", "cysynck"}};
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
        return {{"profile", local}, {"path", path_string(output)}};
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
                                       network["peers"][peer]["profile"] == candidate;
                        output.push_back(
                            {{"profile", candidate},
                             {"trusted", trusted},
                             {"syncthing_connected", connection.value("connected", false)},
                             {"syncthing_transport", connection.value("type", std::string{})},
                             {"observed_sync_address",
                              connection.value("address", std::string{})}});
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
        if (network["peers"].contains(peer) &&
            network["peers"][peer]["profile"]["public_key"] != candidate["public_key"])
            throw std::runtime_error("Pinned peer key changed; revoke and pair again");
        auto projects =
            network["peers"].contains(peer) ? network["peers"][peer]["projects"] : Json::array();
        bool exists = false;
        for (auto &value : projects)
            if (value == p.at("id"))
                exists = true;
        if (!exists)
            projects.push_back(p.at("id"));
        network["peers"][peer] = {{"profile", candidate}, {"projects", projects}};
        event(p.at("id"), "vpn", "Peer public key pinned: " + peer);
        return {{"peer", peer}, {"trusted", true}};
    }
    if (action == "revoke") {
        auto peer = valid_id(data.at("peer"));
        network["peers"].erase(peer);
        return {{"revoked", peer}};
    }
    throw std::runtime_error("Unknown network action");
}
static const Json &trusted_peer(const Json &registry, const std::string &project,
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
            entry = capture(path);
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
        if (receipts.size() >= 10000)
            throw std::runtime_error("Received file catalog limit (10000 per project) reached");
        auto used = inbox_used(state_);
        if (used + bytes.size() > 1024ull * 1024 * 1024)
            throw std::runtime_error("Received file quota (1 GiB) reached");
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
