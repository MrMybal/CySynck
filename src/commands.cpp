#include <cysynck/core.hpp>
#include <map>
#include <stdexcept>

namespace cysynck {
Json cli(Core &core, const std::vector<std::string> &arguments) {
    std::vector<std::string> args;
    std::map<std::string, std::string> flags;
    for (size_t i = 0; i < arguments.size(); ++i) {
        auto arg = arguments[i];
        if (arg == "--help" || arg == "-h")
            return cli(core, {});
        if (arg.rfind("--", 0) == 0) {
            if (i + 1 == arguments.size())
                throw std::runtime_error("Missing flag value: " + arg);
            flags[arg] = arguments[++i];
        } else
            args.push_back(arg);
    }
    auto require = [&](size_t count) {
        if (args.size() != count)
            throw std::runtime_error("Invalid arguments; use cysynck help");
    };
    auto action = [&](const std::string &name, const Json &data) {
        return core.request("POST", "/api/projects/" + args.at(1) + "/" + name, data);
    };
    if (args.empty() || args[0] == "help")
        return {{"help", R"(CySynck 0.13 (C++20)
cysynck [--state DIR] [--plugin LIBRARY] [--server http://127.0.0.1:PORT] COMMAND
  overview | modes | events | api-token
  doctor [--probe true]  (test tool versions and engine connection only with --probe)
  service plan --platform linux|macos --executable /absolute/bin/cysynck [--target-state /profile] [--port 8484]
  service export NEW_DIRECTORY --platform ... --executable ... --plan PREVIEW_TOKEN [--target-state ...] [--port ...]
  desktop startup status|enable|disable  (local desktop installation only)
  engine configure --executable PATH [--port 8385] [--listen-port 22001] [--auto-start true]
  engine connect http://127.0.0.1:8384   (key from CYSYNCK_CONNECT_API_KEY)
  engine run | engine status | engine stop | engine log | engine devices
  engine add-device DEVICE_ID [--address tcp://host:port] [--name NAME]
  engine share PROJECT_ID DEVICE_ID
  project list
  project add NAME ROOT [--mode sendreceive|sendonly|receiveonly|backup|git-bundles]
      [--id SHARED_ID] [--exchange-path DIR] [--interval MINUTES] [--keep COUNT] [--storage auto|whole]
  project set ID [--interval MINUTES] [--keep COUNT] [--storage auto|whole]
      [--git-lfs true|false] [--lfs-storage ABSOLUTE_DIRECTORY]
  backup ID | snapshots ID | changes ID [--snapshot ID]
  diff ID RELATIVE_PATH [--snapshot ID]
  compare ID LEFT_RELATIVE_PATH RIGHT_RELATIVE_PATH
  conflicts ID | conflict-diff ID CONFLICT_RELATIVE_PATH
  resolution-plan ID CONFLICT_PATH --choice current|conflict|merged [--content TEXT]
  resolve-conflict ID CONFLICT_PATH --choice current|conflict|merged --plan TOKEN [--content TEXT]
  resolutions ID | resolution-export ID RESOLUTION_ID NEW_DIRECTORY
  resolution-acknowledge ID RESOLUTION_ID  (mark interrupted recovery as reviewed)
  verify ID [--snapshot SNAPSHOT_ID]
  restore-plan ID SNAPSHOT_ID NEW_DIRECTORY
  restore ID SNAPSHOT_ID NEW_DIRECTORY
  storage status | storage prune [--apply true --plan PREVIEW_TOKEN]
  register ID | sync ID | status ID | pause ID | resume ID
  git status|export|inbox ID  (export: --lfs true includes Git LFS branch history)
  git import ID PEER_ID BUNDLE_ID
  git review ID PEER_ID BRANCH
  git branches ID
  git review-file ID PEER_ID BRANCH RELATIVE_PATH [--base COMMIT --head COMMIT]
  vpn configure NAME 10.77.X.Y [--endpoint HOST:UDP_PORT] [--listen-port 51820] [--mesh-port 8485] [--device-id ID]
  vpn status|preview|write-config|up|down
  vpn publish|discover ID
  vpn trust ID PEER_ID --key EXPECTED_PUBLIC_KEY
  vpn revoke ID PEER_ID
  chat send ID PEER_ID TEXT | chat history ID | chat serve
  send-file ID PEER_ID RELATIVE_PATH [--legacy true]
  transfers list ID | transfers resume ID TRANSFER_ID
  transfers cancel ID TRANSFER_ID --direction in|out
  transfers limits
  transfers configure [--file-limit-mib 1024] [--inbox-quota-mib 4096] [--outbox-quota-mib 4096]
  received list ID | received verify ID TRANSFER_ID
  received export ID TRANSFER_ID NEW_DIRECTORY
  serve [--host 127.0.0.1] [--port 8484]
Desktop: cysynck-desktop [--state DIR] [--plugin LIBRARY] [--background|--foreground] [--no-tray]
Secrets: CYSYNCK_SYNCTHING_API_KEY, CYSYNCK_API_TOKEN, CYSYNCK_MESH_TOKEN.
VPN up/down requires administrator/root. No automatic NAT traversal in 0.13.)"}};
    if (args[0] == "service") {
        if (args.size() < 2 || (args[1] != "plan" && args[1] != "export"))
            throw std::runtime_error("Use service plan|export");
        require(args[1] == "plan" ? 2 : 3);
        if (!flags.contains("--platform") || !flags.contains("--executable"))
            throw std::runtime_error(
                "Choose --platform linux|macos and --executable /absolute/bin/cysynck");
        Json data{{"platform", flags.at("--platform")}, {"executable", flags.at("--executable")}};
        for (const auto &[flag, key] : std::vector<std::pair<std::string, std::string>>{
                 {"--target-state", "target_state"}, {"--plan", "plan"}})
            if (flags.contains(flag))
                data[key] = flags.at(flag);
        if (flags.contains("--port")) {
            size_t parsed = 0;
            auto port = std::stoi(flags.at("--port"), &parsed);
            if (parsed != flags.at("--port").size())
                throw std::runtime_error("Service port must be an integer");
            data["port"] = port;
        }
        if (args[1] == "export")
            data["destination"] = args[2];
        return core.request("POST", "/api/service/" + args[1], data);
    }
    if (args[0] == "doctor") {
        require(1);
        bool probe = false;
        if (flags.contains("--probe")) {
            if (flags.at("--probe") != "true" && flags.at("--probe") != "false")
                throw std::runtime_error("Boolean flags require true or false");
            probe = flags.at("--probe") == "true";
        }
        return core.request(probe ? "POST" : "GET", "/api/doctor",
                            {{"probe_tools", probe}, {"probe_engine", probe}});
    }
    if (args[0] == "overview" || args[0] == "modes") {
        require(1);
        auto result = core.request("GET", "/api/overview");
        return args[0] == "modes" ? result.at("modes") : result;
    }
    if (args[0] == "events") {
        require(1);
        return core.request("GET", "/api/events");
    }
    if (args[0] == "api-token") {
        require(1);
        return {{"token", core.api_token()}};
    }
    if (args[0] == "engine") {
        if (args.size() < 2)
            throw std::runtime_error("Expected an engine command");
        auto command = args[1];
        Json data = Json::object();
        if (command == "configure") {
            require(2);
            if (!flags.contains("--executable"))
                throw std::runtime_error("Select an executable with --executable");
            data["executable"] = flags["--executable"];
            for (auto [flag, key] : std::vector<std::pair<std::string, std::string>>{
                     {"--port", "port"}, {"--listen-port", "listen_port"}})
                if (flags.contains(flag))
                    data[key] = std::stoi(flags[flag]);
            for (auto [flag, key] : std::vector<std::pair<std::string, std::string>>{
                     {"--auto-start", "auto_start"}, {"--loopback-only", "loopback_only"}})
                if (flags.contains(flag)) {
                    if (flags[flag] != "true" && flags[flag] != "false")
                        throw std::runtime_error("Boolean flags require true or false");
                    data[key] = flags[flag] == "true";
                }
        } else if (command == "connect") {
            require(3);
            data = {{"url", args[2]},
                    {"api_key", env("CYSYNCK_CONNECT_API_KEY", env("CYSYNCK_SYNCTHING_API_KEY"))}};
        } else if (command == "add-device") {
            require(3);
            data = {{"device_id", args[2]},
                    {"address", flags.contains("--address") ? flags["--address"] : "dynamic"},
                    {"name", flags.contains("--name") ? flags["--name"] : "CySynck peer"}};
        } else if (command == "share") {
            require(4);
            data = {{"project", args[2]}, {"device_id", args[3]}};
        } else
            require(2);
        if (command == "run")
            throw std::runtime_error("Start engine run from an external terminal");
        bool read = command == "status" || command == "devices" || command == "log";
        return core.request(read ? "GET" : "POST", "/api/engine/" + command, data);
    }
    if (args[0] == "project") {
        if (args.size() > 1 && args[1] == "list") {
            require(2);
            return core.request("GET", "/api/overview").at("projects");
        }
        if (args.size() > 1 && args[1] == "add") {
            require(4);
            Json p = {{"name", args[2]},
                      {"root", args[3]},
                      {"mode", flags.contains("--mode") ? flags["--mode"] : "sendreceive"}};
            for (auto [flag, key] : std::vector<std::pair<std::string, std::string>>{
                     {"--id", "id"}, {"--exchange-path", "exchange_path"}})
                if (flags.contains(flag))
                    p[key] = flags[flag];
            if (flags.contains("--interval"))
                p["backup_interval_minutes"] = std::stoi(flags["--interval"]);
            if (flags.contains("--keep"))
                p["backup_keep"] = std::stoi(flags["--keep"]);
            if (flags.contains("--storage"))
                p["backup_storage"] = flags.at("--storage");
            if (flags.contains("--git-lfs")) {
                if (flags.at("--git-lfs") != "true" && flags.at("--git-lfs") != "false")
                    throw std::runtime_error("Choose --git-lfs true|false");
                p["git_lfs"] = flags.at("--git-lfs") == "true";
            }
            if (flags.contains("--lfs-storage"))
                p["git_lfs_storage"] = flags.at("--lfs-storage");
            return core.request("POST", "/api/projects", p);
        }
        if (args.size() > 1 && args[1] == "set") {
            require(3);
            Json data = Json::object();
            if (flags.contains("--interval"))
                data["backup_interval_minutes"] = std::stoi(flags["--interval"]);
            if (flags.contains("--keep"))
                data["backup_keep"] = std::stoi(flags["--keep"]);
            if (flags.contains("--storage"))
                data["backup_storage"] = flags.at("--storage");
            if (flags.contains("--git-lfs")) {
                if (flags.at("--git-lfs") != "true" && flags.at("--git-lfs") != "false")
                    throw std::runtime_error("Choose --git-lfs true|false");
                data["git_lfs"] = flags.at("--git-lfs") == "true";
            }
            if (flags.contains("--lfs-storage"))
                data["git_lfs_storage"] = flags.at("--lfs-storage");
            return core.request("PUT", "/api/projects/" + args[2], data);
        }
        throw std::runtime_error("Unknown project command");
    }
    if (args[0] == "vpn") {
        if (args.size() < 2)
            throw std::runtime_error("Expected VPN command");
        auto command = args[1];
        Json data = Json::object();
        if (command == "status") {
            require(2);
            return core.request("GET", "/api/network/status");
        }
        if (command == "configure") {
            require(4);
            data = {{"name", args[2]}, {"address", args[3]}};
            for (auto [flag, key] : std::vector<std::pair<std::string, std::string>>{
                     {"--endpoint", "endpoint"}, {"--device-id", "device_id"}})
                if (flags.contains(flag))
                    data[key] = flags[flag];
            for (auto [flag, key] : std::vector<std::pair<std::string, std::string>>{
                     {"--listen-port", "listen_port"}, {"--mesh-port", "mesh_port"}})
                if (flags.contains(flag))
                    data[key] = std::stoi(flags[flag]);
        } else if (command == "publish" || command == "discover") {
            require(3);
            data["project"] = args[2];
        } else if (command == "trust") {
            require(4);
            if (!flags.contains("--key"))
                throw std::runtime_error("Confirm the expected key with --key");
            auto candidates = core.request("POST", "/api/network/discover", {{"project", args[2]}});
            bool found = false;
            for (auto &peer : candidates.at("peers"))
                if (peer["profile"]["peer_id"] == args[3]) {
                    data = {{"project", args[2]},
                            {"profile", peer["profile"]},
                            {"expected_key", flags["--key"]}};
                    found = true;
                }
            if (!found)
                throw std::runtime_error("Peer public profile not found");
        } else if (command == "revoke") {
            require(4);
            data = {{"project", args[2]}, {"peer", args[3]}};
        } else
            require(2);
        return core.request("POST", "/api/network/" + command, data);
    }
    if (args[0] == "received") {
        if (args.size() < 3)
            throw std::runtime_error("Expected received list, verify or export and project ID");
        auto command = args[1];
        Json data = Json::object();
        if (command == "list")
            require(3);
        else if (command == "verify") {
            require(4);
            data = {{"transfer", args[3]}};
        } else if (command == "export") {
            require(5);
            data = {{"transfer", args[3]}, {"destination", args[4]}};
        } else
            throw std::runtime_error("Unknown received file command");
        return core.request("POST",
                            "/api/projects/" + args[2] + "/received-" +
                                (command == "list" ? "files" : command),
                            data);
    }
    if (args[0] == "chat") {
        if (args.size() < 2)
            throw std::runtime_error("Expected chat command");
        if (args[1] == "history") {
            require(3);
            return core.request("POST", "/api/network/history", {{"project", args[2]}});
        }
        if (args[1] == "send") {
            require(5);
            return core.request("POST", "/api/network/chat",
                                {{"project", args[2]}, {"peer", args[3]}, {"text", args[4]}});
        }
        throw std::runtime_error("Start chat serve in an external terminal");
    }
    if (args[0] == "transfers") {
        if (args.size() < 2)
            throw std::runtime_error("Use transfers limits|configure|list|resume|cancel");
        auto command = args[1];
        if (command == "limits" || command == "configure") {
            require(2);
            Json limits = Json::object();
            for (const auto &[flag, value] : flags) {
                std::string key;
                if (flag == "--file-limit-mib")
                    key = "file_limit_mib";
                else if (flag == "--inbox-quota-mib")
                    key = "inbox_quota_mib";
                else if (flag == "--outbox-quota-mib")
                    key = "outbox_quota_mib";
                else
                    throw std::runtime_error("Unknown transfer limit flag");
                if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                    throw std::runtime_error("Transfer limits require whole MiB");
                limits[key] = std::stoull(value);
            }
            if (command == "limits" && !limits.empty())
                throw std::runtime_error("Use transfers configure to change limits");
            return core.request(command == "limits" ? "GET" : "POST",
                                "/api/network/transfer-" + (command == "limits"
                                                                ? std::string("limits")
                                                                : std::string("configure")),
                                limits);
        }
        require(command == "list" ? 3 : 4);
        Json data{{"project", args[2]}};
        if (command != "list")
            data["transfer"] = args[3];
        if (command == "cancel") {
            if (!flags.contains("--direction"))
                throw std::runtime_error("Choose --direction in|out");
            data["direction"] = flags.at("--direction");
        } else if (command != "list" && command != "resume")
            throw std::runtime_error("Use transfers list|resume|cancel");
        return core.request(
            "POST",
            "/api/network/" + std::string(command == "list" ? "transfers" : "transfer-" + command),
            data);
    }
    if (args[0] == "send-file") {
        require(4);
        return core.request(
            "POST", "/api/network/send-file",
            {{"project", args[1]},
             {"peer", args[2]},
             {"path", args[3]},
             {"legacy", flags.contains("--legacy") && flags.at("--legacy") == "true"}});
    }
    if (args[0] == "git") {
        if (args.size() < 3)
            throw std::runtime_error("Expected git command and project ID");
        Json data = Json::object();
        auto command = args[1];
        if (command == "review" || command == "review-file") {
            require(command == "review" ? 5 : 6);
            data = {{"peer", args[3]}, {"branch", args[4]}};
            if (command == "review-file")
                data["path"] = args[5];
            for (auto key : {"base", "head"})
                if (flags.contains("--" + std::string(key)))
                    data[key] = flags.at("--" + std::string(key));
        } else if (command == "import") {
            require(5);
            data = {{"peer", args[3]}, {"bundle", args[4]}};
        } else
            require(3);
        if (command == "export" && flags.contains("--lfs")) {
            if (flags.at("--lfs") != "true" && flags.at("--lfs") != "false")
                throw std::runtime_error("Choose --lfs true|false");
            data["lfs"] = flags.at("--lfs") == "true";
        }
        return core.request(
            "POST",
            "/api/projects/" + args[2] + "/" +
                (command == "status" ? "git-status"
                 : command == "review" || command == "review-file" || command == "branches"
                     ? "git-" + command
                     : command),
            data);
    }
    if (args[0] == "diff") {
        require(3);
        return action("diff",
                      {{"path", args[2]},
                       {"snapshot", flags.contains("--snapshot") ? flags["--snapshot"] : ""}});
    }
    if (args[0] == "compare") {
        require(4);
        return action("compare", {{"left", args[2]}, {"right", args[3]}});
    }
    if (args[0] == "conflict-diff") {
        require(3);
        return action("conflict-diff", {{"path", args[2]}});
    }
    if (args[0] == "resolution-plan" || args[0] == "resolve-conflict") {
        require(3);
        if (!flags.contains("--choice"))
            throw std::runtime_error("Choose --choice current, conflict or merged");
        Json data = {{"path", args[2]}, {"choice", flags["--choice"]}};
        if (flags.contains("--content"))
            data["content"] = flags["--content"];
        if (flags.contains("--plan"))
            data["plan"] = flags["--plan"];
        return action(args[0], data);
    }
    if (args[0] == "resolution-export") {
        require(4);
        return action(args[0], {{"resolution", args[2]}, {"destination", args[3]}});
    }
    if (args[0] == "resolution-acknowledge") {
        require(3);
        return action(args[0], {{"resolution", args[2]}});
    }
    if (args[0] == "storage") {
        require(2);
        if (args[1] == "status")
            return core.request("GET", "/api/storage");
        if (args[1] != "prune")
            throw std::runtime_error("Expected storage status or storage prune");
        Json data = Json::object();
        if (flags.contains("--apply")) {
            if (flags["--apply"] != "true" && flags["--apply"] != "false")
                throw std::runtime_error("--apply requires true or false");
            data["apply"] = flags["--apply"] == "true";
        }
        if (flags.contains("--plan"))
            data["plan"] = flags["--plan"];
        return core.request("POST", "/api/storage/prune", data);
    }
    if (args[0] == "restore" || args[0] == "restore-plan") {
        require(4);
        return action(args[0], {{"snapshot", args[2]}, {"destination", args[3]}});
    }
    if (args[0] == "snapshots") {
        require(2);
        return core.request("GET", "/api/projects/" + args[1] + "/snapshots");
    }
    if (args[0] == "pause" || args[0] == "resume") {
        require(2);
        return action("pause", {{"paused", args[0] == "pause"}});
    }
    for (auto name :
         {"backup", "changes", "register", "sync", "status", "verify", "conflicts", "resolutions"})
        if (args[0] == name) {
            require(2);
            return action(name, flags.contains("--snapshot")
                                    ? Json{{"snapshot", flags["--snapshot"]}}
                                    : Json::object());
        }
    throw std::runtime_error("Unknown command; use cysynck help");
}
} // namespace cysynck
