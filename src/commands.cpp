#include <cysynck/core.hpp>
#include <algorithm>
#include <map>
#include <charconv>
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
        return {{"help", R"(CySynck 0.40 (C++20)
cysynck [--state DIR] [--plugin LIBRARY] [--server http://127.0.0.1:PORT] COMMAND
  overview | modes | plugins | events | api-token
  docs [PAGE] [--language en|fr] [--section ID] [--print]  (local offline manual)
  project mode-options ID
  project mode-options-set ID OPTIONS.json --plan TOKEN
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
      [--id SHARED_ID] [--exchange-path DIR] [--interval MINUTES] [--keep COUNT] [--storage auto|whole|cdc]
  project set ID [--interval MINUTES] [--keep COUNT] [--storage auto|whole|cdc]
      [--git-lfs true|false] [--lfs-storage ABSOLUTE_DIRECTORY]
  backup ID [--plan TOKEN] | snapshots ID | changes ID [--snapshot ID]
  backup-preview ID [--excludes RULES.json] [--offset N] [--limit N] [--inventory-hash HASH]
  backup-excludes ID RULES.json --plan TOKEN
  diff ID RELATIVE_PATH [--snapshot ID]
  compare ID LEFT_RELATIVE_PATH RIGHT_RELATIVE_PATH
  conflicts ID | conflict-diff ID CONFLICT_RELATIVE_PATH
  resolution-plan ID CONFLICT_PATH --choice current|conflict|merged [--content TEXT]
  resolve-conflict ID CONFLICT_PATH --choice current|conflict|merged --plan TOKEN [--content TEXT]
  resolutions ID | resolution-export ID RESOLUTION_ID NEW_DIRECTORY
  resolution-acknowledge ID RESOLUTION_ID  (mark interrupted recovery as reviewed)
  verify ID [--snapshot SNAPSHOT_ID]
  snapshot-files ID SNAPSHOT_ID [--prefix FOLDER --offset N --limit N --manifest-hash HASH]
  snapshot-compare ID LEFT_SNAPSHOT RIGHT_SNAPSHOT [--prefix FOLDER --offset N --limit N]
  snapshot-compare-file ID LEFT_SNAPSHOT RIGHT_SNAPSHOT PATH [--left-hash HASH --right-hash HASH]
  restore-plan ID SNAPSHOT_ID NEW_DIRECTORY [--paths-file JSON_ARRAY]
  restore ID SNAPSHOT_ID NEW_DIRECTORY [--paths-file JSON_ARRAY --plan TOKEN]
  storage status | storage prune [--apply true --plan PREVIEW_TOKEN]
  register ID | sync ID | status ID | pause ID | resume ID
  git status|export|inbox ID  (export: --lfs true includes Git LFS branch history)
  git import ID PEER_ID BUNDLE_ID
  cyrevision identity [--name DISPLAY_NAME]  (public identity only; private key stays in the profile)
  cyrevision status|inbox|export ID
  cyrevision configure ID PROJECT_GUID [--signature-format lf|windows-legacy]
  cyrevision trust ID PUBLIC_IDENTITY_JSON --fingerprint SHA256
  cyrevision revoke ID DEVICE_GUID | cyrevision import ID TRANSACTION_GUID
  cyrevision recover ID DEVICE_GUID [--apply true --plan PREVIEW_TOKEN]
  git setup ID [--branch main --object-format sha1|sha256] [--apply true --plan PREVIEW_TOKEN]
  git integrate ID PEER_ID BRANCH [--lfs-policy reject|pointers] [--apply true --plan PREVIEW_TOKEN]
      [--base COMMIT|unborn --head COMMIT --local-ref REF]  (clean tracked files; unrelated local files preserved)
  git merge-review ID PEER_ID BRANCH  (committed three-way file overview; no application)
  git merge-file ID PEER_ID BRANCH PATH [--base COMMIT --head COMMIT --local-ref REF --merge-base COMMIT]
      [--regions CHOICES.json --region-token TOKEN]  (local, peer or both per conflict region; read-only draft)
  git merge ID PEER_ID BRANCH [--apply true --plan PREVIEW_TOKEN]
      [--base COMMIT --head COMMIT --local-ref REF --resolutions FILE.json]
      (reviewed divergent merge; local/peer files or edited UTF-8 conflict text)
  git integrations ID [--offset 0 --limit 20]
  git lfs-cache ID [--peer PEER --branch BRANCH] [--verify true] [--offset N --limit N --inventory-hash HASH]
  git lfs-recover ID SOURCE_PEER [--peer PEER --branch BRANCH] [--objects IDS.json] [--apply true --plan TOKEN]
  git lfs-recoveries ID [--offset N --limit N] | git lfs-recovery ID RECEIPT
  git materialize ID [--paths-file JSON_ARRAY] [--apply true --plan PREVIEW_TOKEN]  (verified local LFS cache; preserves modified files)
  git materializations ID [--offset 0 --limit 20]
  git materialization-check ID RECEIPT_ID
  git materialization-resume ID RECEIPT_ID [--apply true --plan PREVIEW_TOKEN]
  git materialization-export ID RECEIPT_ID --destination ABSOLUTE_DIR [--paths-file JSON_ARRAY] [--apply true --plan PREVIEW_TOKEN]
  git dehydrate ID [--paths-file JSON_ARRAY] [--apply true --plan PREVIEW_TOKEN]
  git materialization-acknowledge ID RECEIPT_ID --plan CHECK_TOKEN  (preserves Git and recovery files)
  git lfs-recovery-check ID RECEIPT_ID  (verify every planned cache object)
  git lfs-recovery-acknowledge ID RECEIPT_ID --plan CHECK_TOKEN  (mark an interruption as reviewed)
  git integration-check ID RECEIPT_ID
  git integration-acknowledge ID RECEIPT_ID --plan CHECK_TOKEN  (preserve receipt and Git files)
  git extract ID PEER_ID BRANCH NEW_DIRECTORY [--commit FULL_OID --lfs true|false]
      [--apply true --plan PREVIEW_TOKEN --base COMMIT|unborn --head COMMIT --local-ref REF]
  git review ID PEER_ID BRANCH
  git branches ID
  git review-file ID PEER_ID BRANCH RELATIVE_PATH [--base COMMIT|unborn --head COMMIT]
  git history ID PEER_ID BRANCH [--limit 50 --offset 0 --base COMMIT|unborn --head COMMIT]
  git commit ID PEER_ID BRANCH COMMIT [--parent 1]
  git commit-file ID PEER_ID BRANCH COMMIT RELATIVE_PATH [--parent 1 --base COMMIT|unborn --head COMMIT]
  Git review/history/commit commands also accept --local-ref refs/heads/BRANCH|detached.
  git storage ID [--exports ID1,ID2] [--apply true --plan PREVIEW_TOKEN]
  vpn configure NAME 10.77.X.Y [--endpoint HOST:UDP_PORT] [--listen-port 51820] [--mesh-port 8485] [--device-id ID]
  vpn status|preview|write-config|up|down|apparmor
  vpn publish|discover ID
  vpn refresh [ID] | vpn automatic true|false
  vpn trust ID PEER_ID --key EXPECTED_PUBLIC_KEY [--fingerprint VERIFIED_SIGNER_SHA256]
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
VPN up/down requires administrator/root. No automatic NAT traversal.)"}};
    if (args[0] == "cyrevision") {
        if (args.size() < 2) throw std::runtime_error("Choose a CyRevision command; use cysynck help");
        auto command = args[1];
        if (command == "identity") {
            require(2);
            return core.request(flags.contains("--name") ? "POST" : "GET", "/api/cyrevision/identity",
                                flags.contains("--name") ? Json{{"name", flags.at("--name")}} : Json::object());
        }
        Json data = Json::object();
        if (command == "configure") {
            require(4); data = {{"project_id", args[3]}, {"signature_format", flags.contains("--signature-format") ? flags.at("--signature-format") : "lf"}};
        } else if (command == "trust") {
            require(4);
            if (!flags.contains("--fingerprint")) throw std::runtime_error("Supply the independently verified --fingerprint SHA256");
            auto identity = Json::parse(read_file(fs::u8path(args[3]), 4096));
            if (identity.contains("identity")) identity = identity.at("identity");
            data = {{"identity", identity}, {"fingerprint", flags.at("--fingerprint")}};
        } else if (command == "revoke" || command == "import" || command == "recover") {
            require(4); data[command == "revoke" ? "device" : "transaction"] = args[3];
            if (command == "recover") {
                data = {{"device", args[3]}};
                if (flags.contains("--apply")) {
                    if (flags.at("--apply") != "true" && flags.at("--apply") != "false") throw std::runtime_error("Choose --apply true|false");
                    data["apply"] = flags.at("--apply") == "true";
                }
                if (flags.contains("--plan")) data["plan"] = flags.at("--plan");
            }
        } else if (command == "status" || command == "inbox" || command == "export") require(3);
        else throw std::runtime_error("Unknown CyRevision command");
        return core.request(command == "status" || command == "inbox" ? "GET" : "POST",
                            "/api/projects/" + args[2] + "/cyrevision-" + command, data);
    }
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
    if (args[0] == "plugins") {
        require(1);
        if (!flags.empty())
            throw std::runtime_error("Unexpected plugins flag");
        return core.request("GET", "/api/plugins");
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
        if (args.size() > 1 && (args[1] == "mode-options" || args[1] == "mode-options-set")) {
            bool setting = args[1] == "mode-options-set";
            require(setting ? 4 : 3);
            for (const auto &[flag, _] : flags)
                if (!setting || flag != "--plan")
                    throw std::runtime_error("Unknown mode option flag: " + flag);
            if (!setting)
                return core.request("GET", "/api/projects/" + args[2] + "/mode-options");
            if (!flags.contains("--plan"))
                throw std::runtime_error("Load mode options first and supply --plan");
            return core.request("PUT", "/api/projects/" + args[2] + "/mode-options",
                                {{"values", Json::parse(read_file(fs::u8path(args[3]), 32768))},
                                 {"plan", flags.at("--plan")}});
        }
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
        } else if (command == "automatic") {
            require(3);
            if (args[2] != "true" && args[2] != "false") throw std::runtime_error("Choose automatic true|false");
            data["enabled"] = args[2] == "true";
        } else if (command == "refresh") {
            if (args.size() != 2 && args.size() != 3) throw std::runtime_error("Use vpn refresh [PROJECT]");
            if (args.size() == 3) data["project"] = args[2];
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
                    if (flags.contains("--fingerprint")) data["expected_fingerprint"] = flags["--fingerprint"];
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
        if (command == "materialization-export") {
            require(4);
            data["receipt"] = args[3];
            for (const auto &[flag, value] : flags) {
                if (flag == "--destination" || flag == "--plan")
                    data[flag.substr(2)] = value;
                else if (flag == "--paths-file")
                    data["paths"] = Json::parse(read_file(fs::u8path(value), 65536));
                else if (flag == "--apply") {
                    if (value != "true" && value != "false")
                        throw std::runtime_error("Choose --apply true|false");
                    data["apply"] = value == "true";
                } else
                    throw std::runtime_error("Unknown LFS export flag: " + flag);
            }
            return core.request("POST", "/api/projects/" + args[2] + "/git-" + command, data);
        }
        if (command == "materializations" || command == "materialization-check" ||
            command == "materialization-acknowledge" || command == "materialization-resume") {
            require(command == "materializations" ? 3 : 4);
            if (command != "materializations")
                data["receipt"] = args[3];
            for (const auto &[flag, value] : flags) {
                if ((command == "materialization-acknowledge" ||
                     command == "materialization-resume") &&
                    flag == "--plan")
                    data["plan"] = value;
                else if (command == "materialization-resume" && flag == "--apply") {
                    if (value != "true" && value != "false")
                        throw std::runtime_error("Choose --apply true|false");
                    data["apply"] = value == "true";
                } else if (command == "materializations" &&
                           (flag == "--offset" || flag == "--limit")) {
                    uint64_t number = 0;
                    auto parsed =
                        std::from_chars(value.data(), value.data() + value.size(), number);
                    if (value.empty() || parsed.ec != std::errc{} ||
                        parsed.ptr != value.data() + value.size())
                        throw std::runtime_error("Expected an integer " + flag);
                    data[flag.substr(2)] = number;
                } else
                    throw std::runtime_error("Unknown LFS receipt flag: " + flag);
            }
            return core.request("POST", "/api/projects/" + args[2] + "/git-" + command, data);
        }
        if (command == "materialize" || command == "dehydrate") {
            require(3);
            if (command == "dehydrate")
                data["operation"] = "dehydrate";
            for (const auto &[flag, value] : flags) {
                if (flag == "--apply") {
                    if (value != "true" && value != "false")
                        throw std::runtime_error("Choose --apply true|false");
                    data["apply"] = value == "true";
                } else if (flag == "--plan" || flag == "--base")
                    data[flag.substr(2)] = value;
                else if (flag == "--local-ref")
                    data["local_ref"] = value == "detached" ? Json(nullptr) : Json(value);
                else if (flag == "--paths-file")
                    data["paths"] = Json::parse(read_file(fs::u8path(value), 32768));
                else
                    throw std::runtime_error("Unknown LFS materialization flag: " + flag);
            }
            return core.request("POST", "/api/projects/" + args[2] + "/git-materialize", data);
        }
        if (command == "lfs-cache" || command == "lfs-recover" || command == "lfs-recoveries" ||
            command == "lfs-recovery" || command == "lfs-recovery-check" ||
            command == "lfs-recovery-acknowledge") {
            bool receipt = command == "lfs-recovery" || command == "lfs-recovery-check" ||
                           command == "lfs-recovery-acknowledge";
            require(command == "lfs-recover" || receipt ? 4 : 3);
            if (command == "lfs-recover")
                data["source_peer"] = args[3];
            if (receipt)
                data["receipt"] = args[3];
            for (const auto &[flag, value] : flags) {
                if ((flag == "--peer" || flag == "--branch") &&
                    (command == "lfs-cache" || command == "lfs-recover"))
                    data[flag.substr(2)] = value;
                else if ((flag == "--verify" && command == "lfs-cache") ||
                         (flag == "--apply" && command == "lfs-recover")) {
                    if (value != "true" && value != "false")
                        throw std::runtime_error("Choose true or false for " + flag);
                    data[flag.substr(2)] = value == "true";
                } else if (flag == "--plan" &&
                           (command == "lfs-recover" || command == "lfs-recovery-acknowledge"))
                    data["plan"] = value;
                else if (flag == "--objects" && command == "lfs-recover")
                    data["objects"] = Json::parse(read_file(fs::u8path(value), 300 * 1024));
                else if (flag == "--inventory-hash" && command == "lfs-cache")
                    data["inventory_hash"] = value;
                else if ((flag == "--offset" || flag == "--limit") &&
                         (command == "lfs-cache" || command == "lfs-recoveries")) {
                    uint64_t number = 0;
                    auto parsed =
                        std::from_chars(value.data(), value.data() + value.size(), number);
                    if (value.empty() || parsed.ec != std::errc{} ||
                        parsed.ptr != value.data() + value.size())
                        throw std::runtime_error("Invalid LFS page bound");
                    data[flag.substr(2)] = number;
                } else
                    throw std::runtime_error("Unknown LFS cache flag: " + flag);
            }
            return core.request("POST", "/api/projects/" + args[2] + "/git-" + command, data);
        }
        if (command == "integration-check" || command == "integration-acknowledge" ||
            command == "integrations") {
            require(command == "integrations" ? 3 : 4);
            if (command != "integrations")
                data["receipt"] = args[3];
            for (const auto &[flag, value] : flags) {
                if (command == "integration-acknowledge" && flag == "--plan")
                    data["plan"] = value;
                else if (command == "integrations" && (flag == "--offset" || flag == "--limit")) {
                    uint64_t number = 0;
                    auto parsed =
                        std::from_chars(value.data(), value.data() + value.size(), number);
                    if (value.empty() || parsed.ec != std::errc{} ||
                        parsed.ptr != value.data() + value.size())
                        throw std::runtime_error("Expected an integer " + flag);
                    data[flag.substr(2)] = number;
                } else
                    throw std::runtime_error("Unknown Git receipt flag: " + flag);
            }
            return core.request("POST", "/api/projects/" + args[2] + "/git-" + command, data);
        }
        if (command == "merge-review" || command == "merge-file")
            for (const auto &[flag, value] : flags)
                if (flag != "--base" && flag != "--head" && flag != "--local-ref" &&
                    flag != "--merge-base" &&
                    !(command == "merge-file" && (flag == "--regions" || flag == "--region-token")))
                    throw std::runtime_error("Unknown three-way Git review flag: " + flag);
        if (command == "integrate" || command == "merge")
            for (const auto &[flag, value] : flags)
                if (flag != "--base" && flag != "--head" && flag != "--local-ref" &&
                    flag != "--apply" && flag != "--plan" &&
                    !(command == "integrate" && flag == "--lfs-policy") &&
                    !(command == "merge" && flag == "--resolutions"))
                    throw std::runtime_error("Unknown Git integration flag: " + flag);
        if (command == "review" || command == "review-file" || command == "history" ||
            command == "commit" || command == "commit-file" || command == "extract" ||
            command == "integrate" || command == "merge" || command == "merge-review" ||
            command == "merge-file") {
            require(command == "commit-file" ? 7
                    : command == "review-file" || command == "commit" || command == "extract" ||
                            command == "merge-file"
                        ? 6
                        : 5);
            data = {{"peer", args[3]}, {"branch", args[4]}};
            if (command == "merge" && flags.contains("--resolutions"))
                data["resolutions"] =
                    Json::parse(read_file(fs::u8path(flags.at("--resolutions")), 1024 * 1024));
            if (command == "review-file" || command == "merge-file")
                data["path"] = args[5];
            if (flags.contains("--merge-base"))
                data["merge_base"] = flags.at("--merge-base");
            if (command == "commit" || command == "commit-file")
                data["commit"] = args[5];
            if (command == "commit-file")
                data["path"] = args[6];
            if (command == "extract" || command == "integrate" || command == "merge") {
                if (command == "extract")
                    data["destination"] = args[5];
                if (command == "extract" && flags.contains("--commit"))
                    data["commit"] = flags.at("--commit");
                if (flags.contains("--plan"))
                    data["plan"] = flags.at("--plan");
                if (flags.contains("--apply")) {
                    if (flags.at("--apply") != "true" && flags.at("--apply") != "false")
                        throw std::runtime_error("Choose --apply true|false");
                    data["apply"] = flags.at("--apply") == "true";
                }
            }
            for (auto key : {"base", "head"})
                if (flags.contains("--" + std::string(key)))
                    data[key] = flags.at("--" + std::string(key));
            if (data.value("base", std::string{}) == "unborn")
                data["base"] = nullptr;
            if (flags.contains("--local-ref"))
                data["local_ref"] = flags.at("--local-ref") == "detached"
                                        ? Json(nullptr)
                                        : Json(flags.at("--local-ref"));
            for (auto key : {"offset", "limit", "parent"})
                if (flags.contains("--" + std::string(key))) {
                    const auto &value = flags.at("--" + std::string(key));
                    uint64_t number = 0;
                    auto parsed =
                        std::from_chars(value.data(), value.data() + value.size(), number);
                    if (value.empty() || parsed.ec != std::errc{} ||
                        parsed.ptr != value.data() + value.size())
                        throw std::runtime_error(std::string("Expected an integer --") + key);
                    data[key] = number;
                }
        } else if (command == "import") {
            require(5);
            data = {{"peer", args[3]}, {"bundle", args[4]}};
        } else
            require(3);
        if (flags.contains("--regions") || flags.contains("--region-token")) {
            if (command != "merge-file" || !flags.contains("--regions") ||
                !flags.contains("--region-token"))
                throw std::runtime_error("Use git merge-file with --regions and --region-token");
            data["region_choices"] =
                Json::parse(read_file(fs::u8path(flags.at("--regions")), 16384));
            data["region_token"] = flags.at("--region-token");
        }
        if ((command == "export" || command == "extract") && flags.contains("--lfs")) {
            if (flags.at("--lfs") != "true" && flags.at("--lfs") != "false")
                throw std::runtime_error("Choose --lfs true|false");
            data["lfs"] = flags.at("--lfs") == "true";
        }
        if (flags.contains("--lfs-policy")) {
            if (command != "integrate" ||
                (flags.at("--lfs-policy") != "reject" && flags.at("--lfs-policy") != "pointers"))
                throw std::runtime_error("Use git integrate --lfs-policy reject|pointers");
            data["lfs_policy"] = flags.at("--lfs-policy");
        }
        if (command == "setup") {
            for (const auto &[flag, key] : std::vector<std::pair<std::string, std::string>>{
                     {"--branch", "branch"},
                     {"--object-format", "object_format"},
                     {"--plan", "plan"}})
                if (flags.contains(flag))
                    data[key] = flags.at(flag);
            if (flags.contains("--apply")) {
                if (flags.at("--apply") != "true" && flags.at("--apply") != "false")
                    throw std::runtime_error("Choose --apply true|false");
                data["apply"] = flags.at("--apply") == "true";
            }
        }
        if (command == "storage") {
            data["exports"] = Json::array();
            if (flags.contains("--exports")) {
                auto list = flags.at("--exports");
                size_t start = 0, end;
                do {
                    end = list.find(',', start);
                    data["exports"].push_back(list.substr(start, end - start));
                    start = end == std::string::npos ? end : end + 1;
                } while (end != std::string::npos);
            }
            if (flags.contains("--apply")) {
                if (flags.at("--apply") != "true" && flags.at("--apply") != "false")
                    throw std::runtime_error("Choose --apply true|false");
                data["apply"] = flags.at("--apply") == "true";
            }
            if (flags.contains("--plan"))
                data["plan"] = flags.at("--plan");
        }
        return core.request(
            "POST",
            "/api/projects/" + args[2] + "/" +
                (command == "status" ? "git-status"
                 : command == "review" || command == "review-file" || command == "branches" ||
                         command == "storage" || command == "history" || command == "commit" ||
                         command == "commit-file" || command == "setup" || command == "extract" ||
                         command == "integrate" || command == "merge" ||
                         command == "merge-review" || command == "merge-file"
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
        for (const auto &[flag, _] : flags)
            if (flag != "--paths-file" && flag != "--plan" && flag != "--manifest-hash")
                throw std::runtime_error("Unknown restoration flag: " + flag);
        Json data{{"snapshot", args[2]}, {"destination", args[3]}};
        if (flags.contains("--paths-file"))
            data["paths"] = Json::parse(read_file(fs::u8path(flags.at("--paths-file")), 32768));
        if (flags.contains("--plan"))
            data["plan"] = flags.at("--plan");
        if (flags.contains("--manifest-hash"))
            data["manifest_hash"] = flags.at("--manifest-hash");
        return action(args[0], data);
    }
    if (args[0] == "snapshot-files" || args[0] == "snapshot-compare" ||
        args[0] == "snapshot-compare-file") {
        bool file = args[0] == "snapshot-compare-file", browse = args[0] == "snapshot-files";
        require(browse ? 3 : file ? 5 : 4);
        for (const auto &[flag, _] : flags)
            if (flag != "--prefix" && flag != "--offset" && flag != "--limit" &&
                flag != "--manifest-hash" && flag != "--left-hash" && flag != "--right-hash")
                throw std::runtime_error("Unknown snapshot query flag: " + flag);
        Json data =
            browse ? Json{{"snapshot", args[2]}} : Json{{"left", args[2]}, {"right", args[3]}};
        if (file)
            data["path"] = args[4];
        for (auto key : {"prefix", "manifest-hash", "left-hash", "right-hash"})
            if (flags.contains(std::string("--") + key)) {
                auto name = std::string(key);
                std::replace(name.begin(), name.end(), '-', '_');
                data[name] = flags.at(std::string("--") + key);
            }
        for (auto key : {"offset", "limit"})
            if (flags.contains(std::string("--") + key)) {
                const auto &text = flags.at(std::string("--") + key);
                uint64_t number = 0;
                auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                    throw std::runtime_error("Invalid snapshot page number");
                data[key] = number;
            }
        return action(args[0], data);
    }
    if (args[0] == "backup-preview" || args[0] == "backup-excludes" || args[0] == "backup") {
        auto command = args[0];
        require(command == "backup-excludes" ? 3 : 2);
        Json data = Json::object();
        for (const auto &[flag, value] : flags) {
            if (flag == "--plan" && command != "backup-preview")
                data["plan"] = value;
            else if (flag == "--excludes" && command == "backup-preview")
                data["excludes"] = Json::parse(read_file(fs::u8path(value), 16384));
            else if (flag == "--inventory-hash" && command == "backup-preview")
                data["inventory_hash"] = value;
            else if ((flag == "--offset" || flag == "--limit") && command == "backup-preview") {
                uint64_t number = 0;
                auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                    throw std::runtime_error("Invalid backup preview page number");
                data[flag.substr(2)] = number;
            } else
                throw std::runtime_error("Unknown backup preview flag: " + flag);
        }
        if (command == "backup-excludes") {
            if (!data.contains("plan"))
                throw std::runtime_error("Preview the proposed backup rules and supply --plan");
            data["excludes"] = Json::parse(read_file(fs::u8path(args[2]), 16384));
            return core.request("PUT", "/api/projects/" + args[1] + "/backup-excludes", data);
        }
        return action(command, data);
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
