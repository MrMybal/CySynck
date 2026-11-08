#include <cysynck/c_api.h>
#include <cysynck/core.hpp>
#include <fstream>
#include <functional>
#include <httplib.h>
#include <iostream>
#include <thread>

using namespace cysynck;
static int assertions = 0;
static void check(bool value, const char *message) {
    ++assertions;
    if (!value)
        throw std::runtime_error(message);
}
static void rejects(std::function<void()> call, const char *message) {
    try {
        call();
    } catch (const std::exception &) {
        ++assertions;
        return;
    }
    throw std::runtime_error(message);
}
static void setenv_local(const char *key, const std::string &value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}
static Json add(Core &core, const fs::path &root, const std::string &id = "project",
                const std::string &mode = "backup", const fs::path &exchange = {}) {
    fs::create_directories(root);
    return core.request("POST", "/api/projects",
                        {{"id", id},
                         {"name", "Test project"},
                         {"root", path_string(root)},
                         {"mode", mode},
                         {"backup_interval_minutes", 0},
                         {"backup_keep", 2},
                         {"exchange_path", exchange.empty() ? "" : path_string(exchange)}});
}
static Json act(Core &core, const std::string &name, const Json &body = Json::object()) {
    return core.request("POST", "/api/projects/project/" + name, body);
}
static void write(const fs::path &path, const std::string &text) { atomic_write(path, text); }
static size_t count_objects(const fs::path &state) {
    size_t count = 0;
    for (auto &e : fs::directory_iterator(state / "objects"))
        if (e.is_regular_file())
            ++count;
    return count;
}
static Json local_profile(const std::string &id, const std::string &address, char ch) {
    return {{"version", 1},
            {"peer_id", id},
            {"name", id},
            {"address", address},
            {"public_key", std::string(42, ch) + "A="},
            {"endpoint", "198.51.100.2:51820"},
            {"listen_port", 51820},
            {"mesh_port", 8485},
            {"device_id", ""}};
}

int main(int argc, char **argv) {
    if (argc > 1 && std::string(argv[1]) == "--echo") {
        std::cout << (argc > 2 ? argv[2] : "");
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--sleep") {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return 0;
    }
    auto run = fs::current_path() / "test-data" / random_id();
    fs::create_directories(run);
    try {
        check(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA-256 known vector");
        Core core(run / "state");
        auto root = run / fs::u8path("Projet éspace");
        add(core, root);
        write(root / "alpha.txt", "one\n");
        write(root / "nested/data.txt", "été\n");
        write(root / ".git/internal", "ignored");
        auto first = act(core, "backup");
        check(first["file_count"] == 2, ".git must be excluded from snapshots");
        auto objectCount = count_objects(core.state());
        act(core, "backup");
        check(count_objects(core.state()) == objectCount,
              "Identical snapshots deduplicate content");
        write(root / "alpha.txt", "two\n");
        fs::remove(root / "nested/data.txt");
        write(root / "added.txt", "new\n");
        auto changed = act(core, "changes");
        check(changed["files"].size() == 3,
              "Changes include additions, modifications and deletions");
        auto diff = act(core, "diff", {{"path", "alpha.txt"}});
        check(diff["before"] == "one\n" && diff["after"] == "two\n",
              "Diff reads exact snapshot and working contents");
        check(diff["patch"].get<std::string>().find("+two") != std::string::npos,
              "Unified diff contains additions");
        auto restored =
            act(core, "restore",
                {{"snapshot", first["id"]}, {"destination", path_string(run / "restored")}});
        check(read_file(run / "restored/alpha.txt") == "one\n", "Restore recovers old contents");
        check(restored["files"] == 2, "Restore recovers file count");
        rejects(
            [&] {
                act(core, "restore",
                    {{"snapshot", first["id"]}, {"destination", path_string(root)}});
            },
            "Restore must never overwrite a working tree");
        rejects([&] { act(core, "diff", {{"path", "../state/registry.json"}}); },
                "Diff traversal must fail");
        rejects([&] { add(core, root / "nested", "overlap"); }, "Overlapping projects must fail");
        rejects(
            [&] {
                core.request("PUT", "/api/projects/project",
                             {{"root", path_string(run / "restored")}});
            },
            "Root mutation must fail");
        auto last = act(core, "backup");
        auto list = core.request("GET", "/api/projects/project/snapshots");
        check(list.size() == 2 && list[0]["id"] == last["id"],
              "Retention keeps the most recent snapshot");
        auto snapshotPath =
            core.state() / "snapshots/project" / (last["id"].get<std::string>() + ".json");
        auto manifest = Json::parse(read_file(snapshotPath));
        auto digest = manifest["files"]["alpha.txt"]["sha256"].get<std::string>();
        write(core.state() / "objects" / digest, "corrupt");
        rejects(
            [&] {
                act(core, "restore",
                    {{"snapshot", last["id"]},
                     {"destination", path_string(run / "corrupt-restore")}});
            },
            "Corrupt object must be detected during restore");
        std::cout << "PASS snapshots, deduplication, diffs, retention, restore and "
                     "path boundaries\n";

        Core scheduler(run / "scheduler-state");
        auto scheduledRoot = run / "scheduled";
        add(scheduler, scheduledRoot);
        write(scheduledRoot / "note.txt", "snapshot\n");
        scheduler.request("PUT", "/api/projects/project", {{"backup_interval_minutes", 1}});
        scheduler.tick();
        auto scheduled = scheduler.request("GET", "/api/projects/project/snapshots");
        check(scheduled.size() == 1 && scheduled[0]["reason"] == "scheduled",
              "Scheduler creates due snapshot");
        scheduler.tick();
        check(scheduler.request("GET", "/api/projects/project/snapshots").size() == 1,
              "Scheduler avoids duplicate snapshots");
        act(scheduler, "pause", {{"paused", true}});
        fs::last_write_time(scheduler.state() / "snapshots/project" /
                                (scheduled[0]["id"].get<std::string>() + ".json"),
                            fs::file_time_type::clock::now() - std::chrono::minutes(3));
        scheduler.tick();
        check(scheduler.request("GET", "/api/projects/project/snapshots").size() == 1,
              "Paused project does not schedule backups");
        std::cout << "PASS scheduling and pause\n";

        // Exercise the documented Syncthing REST contract against an isolated local fixture.
        httplib::Server engine;
        Json folder = Json::object(), ignoreLines = nullptr;
        bool registeredPaused = false, scanAfterBackup = false;
        Core syncCore(run / "sync-state");
        auto syncRoot = run / "sync-project";
        add(syncCore, syncRoot, "project", "sendreceive");
        write(syncRoot / "important.txt", "protected\n");
        auto jsonResponse = [](httplib::Response &response, const Json &value) {
            response.set_content(value.dump(), "application/json");
        };
        engine.Get("/rest/config/folders", [&](const auto &, auto &response) {
            jsonResponse(response, folder.empty() ? Json::array() : Json::array({folder}));
        });
        engine.Get("/rest/config/defaults/folder",
                   [&](const auto &, auto &response) { jsonResponse(response, Json::object()); });
        engine.Get("/rest/system/status", [&](const auto &, auto &response) {
            jsonResponse(response, {{"myID", "LOCAL-DEVICE"}});
        });
        engine.Post("/rest/config/folders", [&](const auto &request, auto &response) {
            folder = Json::parse(request.body);
            registeredPaused = folder.at("paused");
            jsonResponse(response, Json::object());
        });
        engine.Get("/rest/config/folders/cysynck-project",
                   [&](const auto &, auto &response) { jsonResponse(response, folder); });
        engine.Patch("/rest/config/folders/cysynck-project",
                     [&](const auto &request, auto &response) {
                         folder.update(Json::parse(request.body));
                         jsonResponse(response, Json::object());
                     });
        engine.Get("/rest/db/ignores", [&](const auto &, auto &response) {
            jsonResponse(response, {{"ignore", ignoreLines}});
        });
        engine.Post("/rest/db/ignores", [&](const auto &request, auto &response) {
            ignoreLines = Json::parse(request.body).at("ignore");
            jsonResponse(response, {{"ignore", ignoreLines}});
        });
        engine.Post("/rest/db/scan", [&](const auto &, auto &response) {
            scanAfterBackup = fs::exists(syncCore.state() / "snapshots/project") &&
                              !fs::is_empty(syncCore.state() / "snapshots/project");
            jsonResponse(response, Json::object());
        });
        int enginePort = engine.bind_to_any_port("127.0.0.1");
        std::thread engineThread([&] { engine.listen_after_bind(); });
        auto previousUrl = env("CYSYNCK_SYNCTHING_URL"),
             previousKey = env("CYSYNCK_SYNCTHING_API_KEY");
        setenv_local("CYSYNCK_SYNCTHING_URL", "http://127.0.0.1:" + std::to_string(enginePort));
        setenv_local("CYSYNCK_SYNCTHING_API_KEY", "local-test-key");
        try {
            act(syncCore, "register");
            check(registeredPaused,
                  "New Syncthing folders are paused until exclusions are installed");
            check(std::find(ignoreLines.begin(), ignoreLines.end(), Json("(?i).git")) !=
                          ignoreLines.end() &&
                      ignoreLines[0] == "(?i).cysynck-recovery",
                  "Active .git directories are excluded from peer transport");
            check(folder["versioning"]["type"] == "staggered" && !folder["paused"].get<bool>(),
                  "Syncthing versioning and requested pause state are applied");
            auto repeated = act(syncCore, "register");
            check(repeated["existing"].get<bool>() && ignoreLines.size() == 2 &&
                      !folder["paused"].get<bool>(),
                  "Registration can resume safely without duplicating ignore rules");
            Core connection(run / "external-connection");
            auto publicConfig = connection.request(
                "POST", "/api/engine/connect",
                {{"url", env("CYSYNCK_SYNCTHING_URL")}, {"api_key", "private-test-credential"}});
            check(publicConfig["key_configured"].get<bool>() &&
                      publicConfig.dump().find("private-test-credential") == std::string::npos &&
                      read_file(connection.state() / "registry.json")
                              .find("private-test-credential") == std::string::npos,
                  "Saved connection credentials stay outside public configuration and registry");
            rejects([&] { connection.request("POST", "/api/engine/stop"); },
                    "An external instance is never stopped");
            rejects(
                [&] {
                    connection.request("POST", "/api/engine/connect",
                                       {{"url", "http://192.0.2.1:8384"}, {"api_key", "key"}});
                },
                "External engine credentials require a loopback API");
            act(syncCore, "sync");
            check(scanAfterBackup, "Manual sync scans follow a successful backup");
            folder["path"] = path_string(run / "wrong-folder");
            rejects([&] { act(syncCore, "sync"); },
                    "A mismatched engine folder path blocks synchronization");
            engine.stop();
            engineThread.join();
        } catch (...) {
            engine.stop();
            engineThread.join();
            setenv_local("CYSYNCK_SYNCTHING_URL", previousUrl);
            setenv_local("CYSYNCK_SYNCTHING_API_KEY", previousKey);
            throw;
        }
        setenv_local("CYSYNCK_SYNCTHING_URL", previousUrl);
        setenv_local("CYSYNCK_SYNCTHING_API_KEY", previousKey);
        std::cout
            << "PASS Syncthing registration, .git exclusion, versioning and backup-before-scan\n";

        Core peerA(run / "peer-a"), peerB(run / "peer-b");
        auto shared = run / "exchange";
        auto a = add(peerA, run / "repo-a", "project", "git-bundles", shared),
             b = add(peerB, run / "repo-b", "project", "git-bundles", shared);
        for (auto p : {a, b}) {
            git(p, {"init", "--initial-branch=main"});
            write(fs::u8path(p["root"].get<std::string>()) / "file.txt",
                  p["root"].get<std::string>());
            git(p, {"add", "file.txt"});
            git(p, {"-c", "user.name=CySynck Test", "-c", "user.email=test@example.invalid",
                    "commit", "-m", "Initial"});
        }
        auto originalHead = git(b, {"rev-parse", "HEAD"}),
             originalContents = read_file(run / "repo-b/file.txt");
        auto bundle = act(peerA, "export");
        auto inbox = act(peerB, "inbox");
        check(inbox.size() == 1, "Peer sees complete Git bundle");
        auto imported = act(peerB, "import", {{"peer", bundle["peer"]}, {"bundle", bundle["id"]}});
        check(git(b, {"rev-parse", "HEAD"}) == originalHead &&
                  read_file(run / "repo-b/file.txt") == originalContents,
              "Git import must preserve local HEAD and working tree");
        check(git(b, {"rev-parse", imported["tracking_ref"].get<std::string>()}) ==
                  bundle["head"].get<std::string>(),
              "Tracking ref points to exported commit");
        write(shared / "peers" / bundle["peer"].get<std::string>() /
                  (bundle["id"].get<std::string>() + ".bundle"),
              "tampered");
        rejects([&] { act(peerB, "import", {{"peer", bundle["peer"]}, {"bundle", bundle["id"]}}); },
                "Tampered Git bundle must fail");
        std::cout << "PASS real Git export/import, working tree preservation and "
                     "tampering detection\n";

        if (argc > 1) {
            core.load_plugin(fs::u8path(argv[1]));
            auto plugin = add(core, run / "extension-project", "extension", "inspect");
            auto result = core.request("POST", "/api/projects/extension/sync");
            check(result["state"] == "inspection-complete", "Native plugin mode executes");
            std::cout << "PASS native plugin ABI\n";
        }
        char *error = nullptr;
        auto handle = cysynck_open(path_string(run / "abi-state").c_str(), &error);
        check(handle != nullptr && error == nullptr, "C ABI opens a core handle");
        char *result = nullptr;
        check(cysynck_request(handle, "GET", "/api/overview", "{}", &result) == 0,
              "C ABI request succeeds");
        check(Json::parse(result)["version"] == version, "C ABI returns valid JSON");
        cysynck_free(result);
        check(cysynck_request(handle, "POST", "/invalid", "{", &result) == 1,
              "C ABI contains parsing exceptions");
        cysynck_free(result);
        cysynck_close(handle);
        std::cout << "PASS reusable C API\n";

        Core vpn(run / "vpn-state");
        add(vpn, run / "vpn-project");
        auto registry = Json::parse(read_file(vpn.state() / "registry.json"));
        auto local = local_profile(registry["peer_id"], "10.77.0.1", 'A');
        auto remote = local_profile("remote", "10.77.0.2", 'B');
        registry["network"] = {{"profile", local}, {"peers", Json::object()}};
        atomic_write(vpn.state() / "registry.json", registry.dump());
        rejects(
            [&] {
                vpn.request(
                    "POST", "/api/network/trust",
                    {{"project", "project"}, {"profile", remote}, {"expected_key", "wrong"}});
            },
            "Pairing requires exact public key confirmation");
        vpn.request(
            "POST", "/api/network/trust",
            {{"project", "project"}, {"profile", remote}, {"expected_key", remote["public_key"]}});
        auto preview = vpn.request("POST", "/api/network/preview")["config"].get<std::string>();
        check(preview.find("AllowedIPs = 10.77.0.2/32") != std::string::npos &&
                  preview.find("0.0.0.0/0") == std::string::npos,
              "VPN routes only trusted host addresses");
        check(preview.find("<local private key>") != std::string::npos,
              "VPN preview never reveals private keys");
        auto published = vpn.request("POST", "/api/network/publish", {{"project", "project"}});
        check(Json::parse(read_file(fs::u8path(published["path"].get<std::string>())))
                  .contains("public_key"),
              "Public profile published for Syncthing transport");
        Json message = {
            {"project", "project"}, {"peer", "remote"}, {"id", "message1"}, {"text", "Bonjour"}};
        rejects([&] { vpn.direct_receive(message, "10.77.0.3"); },
                "Message sender must match pinned VPN IP");
        vpn.direct_receive(message, "10.77.0.2");
        vpn.direct_receive(message, "10.77.0.2");
        check(vpn.request("POST", "/api/network/history", {{"project", "project"}}).size() == 1,
              "Messages are idempotent");
        std::string binary("hello\0file", 10);
        Json transfer = {{"project", "project"}, {"peer", "remote"},
                         {"id", "file1"},        {"file", true},
                         {"name", "sample.bin"}, {"sha256", sha256(binary)}};
        vpn.direct_receive(transfer, "10.77.0.2", binary);
        check(read_file(vpn.state() / "inbox/project/file1.received") == binary,
              "Direct receive preserves binary bytes");
        transfer["id"] = "file2";
        transfer["sha256"] = std::string(64, '0');
        rejects([&] { vpn.direct_receive(transfer, "10.77.0.2", binary); },
                "Direct file checksum mismatch is rejected");
        std::cout << "PASS VPN pairing, host routes, chat idempotency and binary "
                     "file reception\n";

        HttpServer server(core);
        int port = server.bind("127.0.0.1", 0);
        std::thread listener([&] { server.listen(); });
        try {
            httplib::Client client("127.0.0.1", port);
            client.set_connection_timeout(2);
            httplib::Result response;
            for (int i = 0; i < 50; ++i) {
                response = client.Get("/api/overview");
                if (response)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            check(response && response->status == 401, "API rejects unauthenticated reads");
            response = client.Get("/");
            check(response && response->status == 200 &&
                      response->body.find("<html lang=\"en\">") != std::string::npos,
                  "Web console serves English as its default source language");
            response = client.Get("/i18n.js");
            check(response && response->status == 200 &&
                      response->get_header_value("Content-Type").find("javascript") !=
                          std::string::npos &&
                      response->body.find("CySynckI18n") != std::string::npos,
                  "Web console serves its language catalog as JavaScript");
            auto token = core.api_token();
            response = client.Get("/api/overview", {{"Authorization", "Bearer " + token}});
            check(response && response->status == 200, "API accepts bearer token");
            response =
                client.Post("/api/projects/project/compare", {{"Authorization", "Bearer " + token}},
                            "{\"left\":\"alpha.txt\",\"right\":\"added.txt\"}", "application/json");
            check(response && response->status == 200 &&
                      Json::parse(response->body).at("before") == "two\n",
                  "HTTP exposes the same two-file comparison");
            response =
                client.Post("/api/projects/project/conflicts",
                            {{"Authorization", "Bearer " + token}}, "{}", "application/json");
            check(response && response->status == 200 &&
                      Json::parse(response->body).at("files").empty(),
                  "HTTP exposes conflict discovery");
            write(root / "alpha.sync-conflict-20261104-120000-ABCDEFG.txt", "selected via HTTP\n");
            Json resolutionInput = {{"path", "alpha.sync-conflict-20261104-120000-ABCDEFG.txt"},
                                    {"choice", "conflict"}};
            response = client.Post("/api/projects/project/resolution-plan",
                                   {{"Authorization", "Bearer " + token}}, resolutionInput.dump(),
                                   "application/json");
            check(response && response->status == 200,
                  "HTTP prepares a resolution without changing files");
            resolutionInput["plan"] = Json::parse(response->body).at("plan");
            response = client.Post("/api/projects/project/resolve-conflict",
                                   {{"Authorization", "Bearer " + token}}, resolutionInput.dump(),
                                   "application/json");
            check(response && response->status == 400 && read_file(root / "alpha.txt") == "two\n",
                  "HTTP cannot resolve an active project");
            response =
                client.Post("/api/projects/project/pause", {{"Authorization", "Bearer " + token}},
                            "{\"paused\":true}", "application/json");
            check(response && response->status == 200,
                  "HTTP pauses the backup project for an explicit resolution");
            response = client.Post("/api/projects/project/resolve-conflict",
                                   {{"Authorization", "Bearer " + token}}, resolutionInput.dump(),
                                   "application/json");
            check(response && response->status == 200 &&
                      read_file(root / "alpha.txt") == "selected via HTTP\n",
                  "HTTP applies exactly the prepared choice");
            response = client.Get("/api/storage", {{"Authorization", "Bearer " + token}});
            check(response && response->status == 200 &&
                      Json::parse(response->body).contains("reclaimable_bytes"),
                  "Authenticated API exposes the storage preview");
            response = client.Post("/api/storage/prune", {{"Authorization", "Bearer " + token}},
                                   "{\"apply\":true}", "application/json");
            check(response && response->status == 400,
                  "HTTP cleanup cannot apply without a preview token");
            response =
                client.Post("/api/projects/project/verify", {{"Authorization", "Bearer " + token}},
                            "{}", "application/json");
            check(response && response->status == 200 &&
                      !Json::parse(response->body).at("ok").get<bool>(),
                  "HTTP returns a structured corruption report");
            response = client.Get("/api/overview", {{"Authorization", "Bearer " + token},
                                                    {"Origin", "https://untrusted.invalid"}});
            check(response && response->status == 403, "API rejects cross-origin access");
            response = client.Post("/api/cli", {{"Authorization", "Bearer " + token}},
                                   "{\"args\":[\"project\",\"list\"]}", "application/json");
            check(response && response->status == 200 && Json::parse(response->body).size() == 2,
                  "Remote CLI uses the same core");
            for (auto path : {"/api/engine/configure", "/api/engine/connect"}) {
                response = client.Post(path, {{"Authorization", "Bearer " + token}}, "{}",
                                       "application/json");
                check(response && response->status == 403,
                      "HTTP rejects executable and credential configuration");
            }
            response = client.Post("/api/cli", {{"Authorization", "Bearer " + token}},
                                   "{\"args\":[\"engine\",\"connect\",\"http://127.0.0.1:8384\"]}",
                                   "application/json");
            check(response && response->status == 400,
                  "Remote CLI cannot bypass local engine configuration");
            response =
                client.Post("/api/cli", {{"Authorization", "Bearer " + token}},
                            "{\"args\":[\"desktop\",\"startup\",\"enable\"]}", "application/json");
            check(response && response->status == 400,
                  "Remote CLI cannot register programs at login");
            for (const auto &action : {"up", "down", "configure", "automatic", "refresh", "trust", "revoke", "write-config"}) {
                response = client.Post(std::string("/api/network/") + action,
                    {{"Authorization", "Bearer " + token}}, "{}", "application/json");
                check(response && response->status == 403,
                      "HTTP cannot arm or mutate privileged VPN operations");
                for (auto args : {std::vector<std::string>{"--unused", "value", "vpn", action, "true"},
                                  std::vector<std::string>{"vpn", "--unused", "value", action, "true"}}) {
                    response = client.Post("/api/cli", {{"Authorization", "Bearer " + token}},
                        Json{{"args", args}}.dump(), "application/json");
                    check(response && response->status == 400 && response->body.find("require the local CLI") != std::string::npos,
                          "CLI flag ordering bypassed the local VPN policy");
                }
            }
            response = client.Post("/api/cli", {{"Authorization", "Bearer " + token}},
                Json{{"args", {"--unused", "value", "engine", "connect", "http://127.0.0.1:8384"}}}.dump(), "application/json");
            check(response && response->status == 400 && response->body.find("configured locally") != std::string::npos,
                  "CLI flag ordering bypassed the local engine policy");
            server.stop();
            listener.join();
        } catch (...) {
            server.stop();
            listener.join();
            throw;
        }
        std::cout << "PASS HTTP authentication, origin checks and remote CLI\n";
        auto executable = fs::absolute(fs::u8path(argv[0]));
        std::string special = "spaces \" quotes \\ $(literal)";
        check(process({path_string(executable), "--echo", special}).out == special,
              "Process arguments survive quoting without a shell");
        rejects([&] { process({path_string(executable), "--sleep"}, {}, {}, 1); },
                "Process timeout must terminate its child");
        std::cout << "PASS process quoting and timeout\n";
        std::cout << "PASS " << assertions << " assertions; fixtures: " << path_string(run) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
