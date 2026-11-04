#include <cysynck/core.hpp>
#include <httplib.h>
#include <atomic>
#include <iostream>
#include <thread>
using namespace cysynck;
static int checks = 0;
static void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    ++checks;
}
static void environment(const char *key, const std::string &value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}
static Json item(const Json &report, const std::string &id) {
    for (const auto &row : report.at("checks"))
        if (row.at("id") == id)
            return row;
    throw std::runtime_error("Missing diagnostic check: " + id);
}
int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--version") {
        atomic_write(fs::u8path(env("CYSYNCK_DOCTOR_TEST_MARKER")), "executed");
        std::cout << "git version 2.0.0-test\n";
        return 0;
    }
    auto run = fs::current_path() / "diagnostics-test-data" / random_id();
    httplib::Server server;
    std::thread listener;
    try {
        Core core(run / "state");
        environment("PATH", "");
        environment("CYSYNCK_WG", path_string(run / "missing-wg"));
        environment("CYSYNCK_WIREGUARD", path_string(run / "missing-tunnel"));
        environment("CYSYNCK_WG_QUICK", path_string(run / "missing-tunnel"));
        environment("CYSYNCK_MESH_TOKEN", "private-doctor-test-value");
        auto report = cli(core, {"doctor"});
        check(report.at("ready") && report.at("warnings") > 0,
              "Missing optional tools do not block a fresh profile");
        auto root = run / "Documents";
        fs::create_directories(root);
        core.request("POST", "/api/projects",
                     {{"id", "docs"},
                      {"name", "Documents"},
                      {"root", path_string(root)},
                      {"mode", "backup"},
                      {"backup_interval_minutes", 0}});
        auto before = read_file(core.state() / "registry.json");
        report = core.request("GET", "/api/doctor");
        check(report.at("ready"), "Backup-only projects do not require Syncthing or Git");
        check(read_file(core.state() / "registry.json") == before,
              "Inspection does not modify profile settings or project data");
        fs::rename(root, run / "Moved");
        report = cli(core, {"doctor"});
        check(item(report, "project.docs.root").at("status") == "error" && !report.at("ready"),
              "Missing project roots are reported without aborting other checks");
        fs::rename(run / "Moved", root);
        auto repository = run / "Repository", exchange = run / "Exchange";
        fs::create_directories(repository / ".git");
        fs::create_directories(exchange);
        core.request("POST", "/api/projects",
                     {{"id", "team"},
                      {"name", "Team"},
                      {"root", path_string(repository)},
                      {"exchange_path", path_string(exchange)},
                      {"mode", "git-bundles"},
                      {"backup_interval_minutes", 0}});
        report = cli(core, {"doctor"});
        check(item(report, "tool.git").at("status") == "error",
              "Git is required for Git exchange projects");
        check(item(report, "engine.configuration").at("status") == "error",
              "A synchronization project needs an engine connection");
        auto bin = run / "Tools with spaces";
        fs::create_directories(bin);
#ifdef _WIN32
        auto helper = bin / "git.exe";
        auto source = fs::absolute(fs::u8path(argv[0]));
        fs::copy_file(source.parent_path() / "cysynck_core.dll", bin / "cysynck_core.dll");
#else
        auto helper = bin / "git";
        auto source = fs::absolute(fs::u8path(argv[0]));
#endif
        fs::copy_file(source, helper);
        environment("PATH", path_string(bin));
        auto marker = run / "probe-marker";
        environment("CYSYNCK_DOCTOR_TEST_MARKER", path_string(marker));
        report = core.request("GET", "/api/doctor", {{"probe_tools", true}});
        check(item(report, "tool.git").at("status") == "ok" && !fs::exists(marker),
              "GET never executes tools, including when a body requests a probe");
        report = core.request("POST", "/api/doctor", {{"probe_tools", true}});
        check(item(report, "tool.git").at("details").at("version") == "2.0.0-test" &&
                  fs::exists(marker),
              "Explicit probes execute the resolved tool without shell splitting");
        std::atomic<int> requests = 0;
        server.Get("/rest/system/status",
                   [&](const httplib::Request &request, httplib::Response &response) {
                       ++requests;
                       check(request.get_header_value("X-API-Key") == "private-engine-test-value",
                             "The engine probe authenticates without exposing its key");
                       response.set_content("{\"myID\":\"TEST-DEVICE\"}", "application/json");
                   });
        int port = server.bind_to_any_port("127.0.0.1");
        check(port > 0, "An isolated loopback fixture is bound");
        listener = std::thread([&] { server.listen_after_bind(); });
        core.request("POST", "/api/engine/connect",
                     {{"url", "http://127.0.0.1:" + std::to_string(port)},
                      {"api_key", "private-engine-test-value"}});
        report = cli(core, {"doctor"});
        check(requests == 0 && report.at("ready"), "Inspection makes no engine requests");
        report = core.request("POST", "/api/doctor", {{"probe_engine", true}});
        check(requests == 1 && item(report, "engine.reachable").at("status") == "ok",
              "An explicit connection test reaches the configured engine");
        check(report.dump().find("private-engine-test-value") == std::string::npos &&
                  report.dump().find("private-doctor-test-value") == std::string::npos,
              "Reports contain no API keys or mesh secrets");
        server.stop();
        listener.join();
        report = core.request("POST", "/api/doctor", {{"probe_engine", true}});
        check(item(report, "engine.reachable").at("status") == "error",
              "An unavailable required engine is reported as an error");
        std::cout << "PASS " << checks << " diagnostic checks; fixtures: " << path_string(run)
                  << '\n';
        return 0;
    } catch (const std::exception &error) {
        server.stop();
        if (listener.joinable())
            listener.join();
        std::cerr << "FAIL: " << error.what() << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
