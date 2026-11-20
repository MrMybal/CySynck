#include "assistant.hpp"
#include <httplib.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <regex>
using namespace cysynck;
using namespace cysynck::assistant;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    ++checks;
}
template <class F> void refuses(F work, const char *message) {
    try {
        work();
    } catch (const std::exception &) {
        ++checks;
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
// Deterministic stand-ins for wg and Syncthing; no tunnel is ever activated.
int model(int argc, char **argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (env("CYSYNCK_ASSISTANT_ACTION_FIXTURE").empty())
        return 22;
    if (args.at(0) == "--version") {
        std::cout << "syncthing vfixture";
        return 0;
    }
    if (args.at(0) == "genkey" || args.at(0) == "pubkey") {
        std::cout << std::string(43, 'A') << "=\n";
        return 0;
    }
    if (args.at(0) == "show")
        return 0;
    if (args.size() > 1 && args.at(1) == "--help") {
        std::cout << "--no-port-probing --log-file";
        return 0;
    }
    auto home = std::find(args.begin(), args.end(), "--home");
    if (home == args.end() || ++home == args.end())
        return 23;
    auto path = fs::u8path(*home) / "config.xml";
    if (args.at(0) == "generate") {
        atomic_write(path, "<configuration><gui/><options/></configuration>");
        return 0;
    }
    if (args.at(0) != "serve")
        return 24;
    std::smatch address;
    auto xml = read_file(path);
    if (!std::regex_search(xml, address, std::regex("127\\.0\\.0\\.1:([0-9]+)")))
        return 25;
    httplib::Server server;
    server.Get("/rest/system/status", [](const auto &, auto &r) {
        r.set_content("{\"myID\":\"fixture\",\"uptime\":1}", "application/json");
    });
    server.Get("/rest/system/version", [](const auto &, auto &r) {
        r.set_content("{\"version\":\"fixture\"}", "application/json");
    });
    server.Post("/rest/system/shutdown", [&](const auto &, auto &r) {
        r.set_content("{}", "application/json");
        server.stop();
    });
    return server.listen("127.0.0.1", std::stoi(address[1])) ? 0 : 26;
}
int free_port() {
    httplib::Server server;
    auto port = server.bind_to_any_port("127.0.0.1");
    server.stop();
    if (port <= 0)
        throw std::runtime_error("Cannot reserve fixture port");
    return port;
}
} // namespace
int main(int argc, char **argv) {
    if (argc > 1 && env("CYSYNCK_ASSISTANT_ACTION_FIXTURE").size())
        return model(argc, argv);
    auto parent = fs::current_path();
    auto directory = parent / ("assistant-actions-fixture-" + random_id());
    struct Cleanup {
        fs::path root, parent;
        ~Cleanup() {
            std::error_code error;
            if (root.parent_path() == parent &&
                root.filename().string().starts_with("assistant-actions-fixture-") &&
                !fs::is_symlink(root))
                fs::remove_all(root, error);
        }
    } cleanup{directory, parent};
    try {
        fs::create_directories(directory / "project");
        Core core(directory / "profile");
        Options controlled;
        controlled.access_profile = cymcp::LocalAccessProfile::protected_access;
        Host host(core, controlled);
        auto call = [&](const std::string &name, Json data = nullptr) {
            if (data.is_null())
                data = Json::object();
            return host.registry()->invoke("cysynck." + name, {{"parameters", data}}).at("result");
        };
        check(host.registry()->size() >= 125, "Complete application registry is discoverable");
        for (auto name : {"engine.configure",
                          "engine.connect",
                          "engine.start",
                          "engine.stop",
                          "engine.add-device",
                          "engine.share",
                          "network.configure",
                          "network.write-config",
                          "network.up",
                          "network.down",
                          "network.router",
                          "network.automatic",
                          "network.refresh",
                          "network.publish",
                          "network.trust",
                          "network.revoke",
                          "chat.send",
                          "transfers.send",
                          "transfers.resume",
                          "transfers.cancel",
                          "transfers.configure",
                          "cyrevision.identity-create",
                          "service.export",
                          "maintenance.tick",
                          "settings.set",
                          "ui.configure",
                          "ui.help",
                          "startup.enable",
                          "startup.disable",
                          "receiver.start",
                          "receiver.stop",
                          "plugins.load"}) {
            check(!host.registry()
                       ->describe("cysynck." + std::string(name))
                       .at("annotations")
                       .value("readOnlyHint", true),
                  "Side effects are not read-only");
            refuses([&] { call(name); }, "Read-only host refuses application mutations");
        }
        host.allow_writes(true);
        refuses([&] { call("engine.start"); }, "Ephemeral engine start is refused");
        refuses([&] { call("receiver.start"); }, "Ephemeral receiver start is refused");
        refuses([&] { call("ui.configure", {{"ui_language", "fr"}}); },
                "Headless host cannot pretend to modify a live UI");
        check(call("receiver.status").at("running") == false, "Receiver status reflects this host");
        call("settings.set", {{"ui_language", "fr"}, {"ui_theme", "navy"}});
        check(call("settings.get").at("ui_language") == "fr",
              "Preferences persist for the next desktop launch");
        refuses([&] { call("settings.set", {{"ui_language", "xx"}}); },
                "Unsupported language is refused");
        refuses([&] { call("settings.set", {{"engine", Json::object()}}); },
                "Settings cannot bypass engine commands");
        auto quotas =
            call("transfers.configure",
                 {{"file_limit_mib", 1}, {"inbox_quota_mib", 2}, {"outbox_quota_mib", 2}});
        check(quotas == call("transfers.limits"), "Native transfer quotas persist");
        refuses([&] { call("transfers.configure", {{"file_limit_mib", 3}}); },
                "Native quota consistency remains enforced");
        auto id = call("project.create", {{"name", "fixture"},
                                          {"root", path_string(directory / "project")},
                                          {"mode", "backup"},
                                          {"backup_interval_minutes", 0}})
                      .at("id")
                      .get<std::string>();
        auto identity = call("cyrevision.identity-create", {{"name", "Fixture"}});
        check(identity == call("cyrevision.identity") && identity.contains("fingerprint"),
              "Public CyRevision identity is stable");
        fs::create_directories(directory / "git");
        check(process({"git", "init"}, directory / "git").code == 0,
              "Git fixture is initialized locally");
        auto git_id =
            call("project.create", {{"name", "Git fixture"},
                                    {"root", path_string(directory / "git")},
                                    {"mode", "git-bundles"},
                                    {"exchange_path", path_string(directory / "exchange")},
                                    {"backup_interval_minutes", 0}})
                .at("id")
                .get<std::string>();
        auto revision_call = [&](const std::string &name, const Json &parameters) {
            return host.registry()
                ->invoke("cysynck.project.cyrevision-" + name,
                         {{"project_id", git_id}, {"parameters", parameters}})
                .at("result");
        };
        revision_call("configure", {{"project_id", "7da39085-4180-4739-8174-705d77c34efc"},
                                    {"signature_format", "lf"}});
        refuses(
            [&] {
                revision_call("trust",
                              {{"identity", identity.at("identity")}, {"fingerprint", "wrong"}});
            },
            "CyRevision trust requires an independently confirmed fingerprint");
        check(revision_call("trust", {{"identity", identity.at("identity")},
                                      {"fingerprint", identity.at("fingerprint")}})
                      .at("fingerprint") == identity.at("fingerprint"),
              "CyRevision trust uses the native pinned public identity");
        host.allow_writes(false);
        check(revision_call("recover", {{"device", identity.at("identity").at("deviceId")}})
                      .at("applied") == false,
              "CyRevision recovery preview remains available without write permission");
        host.allow_writes(true);
        check(revision_call("revoke", {{"device", identity.at("identity").at("deviceId")}})
                      .at("revoked") == true,
              "CyRevision revocation retains native semantics");
        auto export_data = Json{{"platform", "linux"},
                                {"executable", "/opt/cysynck/bin/cysynck"},
                                {"target_state", "/srv/cysynck"}};
        auto service = call("service.plan", export_data);
        export_data.update(
            {{"destination", path_string(directory / "service")}, {"plan", "stale"}});
        refuses([&] { call("service.export", export_data); }, "Stale service plan is refused");
        export_data["plan"] = service.at("plan");
        check(call("service.export", export_data).at("installed") == false &&
                  fs::is_regular_file(directory / "service/service-export.json"),
              "Reviewed export writes files without service installation");
        environment("CYSYNCK_ASSISTANT_ACTION_FIXTURE", path_string(directory));
        auto executable = fs::absolute(fs::u8path(argv[0]));
        environment("CYSYNCK_WG", path_string(executable));
        auto profile = call("network.configure", {{"name", "Fixture"}, {"address", "10.77.90.1"}});
        check(profile.contains("public_key") && !profile.contains("private_key"),
              "VPN configuration uses only the fixture executable");
        check(call("network.preview").at("config").get<std::string>().find("<local private key>") !=
                  std::string::npos,
              "VPN preview omits private key material");
        auto config = call("network.write-config");
        check(fs::is_regular_file(fs::u8path(config.at("path").get<std::string>())),
              "VPN configuration stays in the temporary profile");
        check(call("network.automatic", {{"enabled", false}}).at("enabled") == false,
              "Endpoint refresh preference is native");
        call("network.publish", {{"project", id}});
        check(call("network.discover", {{"project", id}}).at("peers").empty(),
              "Discovery does not grant trust");
        auto peer = profile;
        peer["peer_id"] = "remote-fixture";
        peer["address"] = "10.77.90.2";
        peer["public_key"] = std::string(42, 'B') + "A=";
        peer.erase("signing_key");
        refuses(
            [&] {
                call("network.trust",
                     {{"project", id}, {"profile", peer}, {"expected_key", "wrong"}});
            },
            "Peer key confirmation is mandatory");
        check(call("network.trust",
                   {{"project", id}, {"profile", peer}, {"expected_key", peer.at("public_key")}})
                      .at("trusted") == true,
              "A confirmed fixture identity can be pinned");
        refuses([&] { call("network.router", {{"action", "enable"}, {"gateway", "127.0.0.1"}}); },
                "Router requires an owned live tunnel before making requests");
        environment("CYSYNCK_MESH_TOKEN", std::string(32, 'x'));
        refuses(
            [&] {
                call("chat.send",
                     {{"project", id}, {"peer", "remote-fixture"}, {"text", "never sent"}});
            },
            "No chat is sent without an active owned tunnel");
        check(call("network.history", {{"project", id}}).empty(),
              "Failed send has no delivery receipt");
        check(call("network.revoke", {{"project", id}, {"peer", "remote-fixture"}})
                  .contains("revoked"),
              "Native project-scoped revocation works");
        auto engine_port = free_port(), sync_port = free_port();
        while (sync_port == engine_port)
            sync_port = free_port();
        call("engine.configure", {{"executable", path_string(executable)},
                                  {"port", engine_port},
                                  {"listen_port", sync_port},
                                  {"auto_start", false},
                                  {"loopback_only", true}});
        Host persistent(core, {}, true);
        persistent.allow_writes(true);
        check(persistent.registry()
                      ->invoke("cysynck.engine.start", Json::object())
                      .at("result")
                      .at("owned") == true,
              "Persistent instance starts an isolated fixture child");
        refuses([&] { call("engine.configure", {{"executable", path_string(executable)}}); },
                "Running owned engine config is protected");
        check(persistent.registry()
                      ->invoke("cysynck.engine.stop", Json::object())
                      .at("result")
                      .at("stopped") == true,
              "Owning instance stops the fixture child");
        refuses(
            [&] {
                call("engine.connect",
                     {{"url", "http://127.0.0.1:1"}, {"api_key_env", "MISSING_FIXTURE_KEY"}});
            },
            "Empty API-key environment variable is refused");
        environment("CYSYNCK_FIXTURE_ENGINE_KEY", "private-fixture-engine-key");
        auto connected = call("engine.connect", {{"url", "http://127.0.0.1:1"},
                                                 {"api_key_env", "CYSYNCK_FIXTURE_ENGINE_KEY"}});
        check(connected.at("kind") == "external" &&
                  connected.dump().find("private-fixture-engine-key") == std::string::npos,
              "Existing-engine connection consumes its environment key without exposing it");
        check(read_file(directory / "profile/engine/api-key") == "private-fixture-engine-key",
              "Engine key is saved only in the protected temporary profile");
        if (argc == 2) {
            auto plugin = fs::absolute(fs::u8path(argv[1]));
            refuses(
                [&] {
                    persistent.registry()->invoke("cysynck.plugins.load",
                                                  {{"parameters",
                                                    {{"path", path_string(plugin)},
                                                     {"expected_sha256", std::string(64, '0')}}}});
                },
                "Plugin hash must match reviewed code");
            auto loaded = persistent.registry()->invoke(
                "cysynck.plugins.load",
                {{"parameters",
                  {{"path", path_string(plugin)}, {"expected_sha256", sha256_file(plugin)}}}});
            check(loaded.dump().find("example.inspect") != std::string::npos,
                  "Known test plugin loads into the host");
        }
        auto bridge = std::make_shared<DesktopBridge>();
        Host desktop(core, {}, true, bridge);
        desktop.allow_writes(true);
        auto ui_thread = std::this_thread::get_id();
        auto action = std::async(std::launch::async, [&] {
            return desktop.registry()->invoke("cysynck.ui.configure",
                                              {{"parameters", {{"ui_language", "en"}}}});
        });
        while (action.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready)
            bridge->drain([&](const std::string &name, const Json &data) {
                check(std::this_thread::get_id() == ui_thread && name == "cysynck.ui.configure",
                      "Desktop action executes only on UI thread");
                return data;
            });
        check(action.get().at("result").at("ui_language") == "en",
              "UI bridge returns the action receipt");
        cymcp::CallContext context;
        auto waiting = std::async(std::launch::async, [&] {
            return bridge->invoke("cancelled", Json::object(), context);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        context.cancellation.cancel();
        refuses([&] { waiting.get(); }, "Queued UI actions can be cancelled");
        bool replayed = false;
        bridge->drain([&](const auto &, const auto &) {
            replayed = true;
            return Json::object();
        });
        check(!replayed, "Cancelled action cannot execute later");
        auto closing = std::async(std::launch::async,
                                  [&] { return bridge->invoke("closing", Json::object(), {}); });
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        bridge->close();
        refuses([&] { closing.get(); }, "Closing rejects pending UI work without deadlock");
        std::cout << "PASS " << checks << " assistant application action checks; fixture removed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
