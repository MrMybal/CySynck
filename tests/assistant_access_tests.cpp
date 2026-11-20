#include "assistant.hpp"
#include <fstream>
#include <iostream>

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
} // namespace
int main() {
    auto parent = fs::current_path();
    auto root = parent / ("assistant-access-fixture-" + random_id());
    struct Cleanup {
        fs::path root, parent;
        ~Cleanup() {
            std::error_code error;
            if (root.parent_path() == parent &&
                root.filename().string().starts_with("assistant-access-fixture-") &&
                !fs::is_symlink(root))
                fs::remove_all(root, error);
        }
    } cleanup{root, parent};
    try {
        fs::create_directories(root);
        Core migration(root / "migration");
        auto file = migration.state() / "assistant-options.json";
        auto first = Options::load(migration.state());
        check(first.access_profile == cymcp::LocalAccessProfile::free &&
                  Json::parse(read_file(file)).at("access_profile") == "free",
              "Absent configuration migrates durably to Free");
        atomic_write(file, Json{{"compact_discovery", false}, {"custom", "preserve"}}.dump(), true);
        auto performance_only = Options::load(migration.state());
        check(performance_only.access_profile == cymcp::LocalAccessProfile::free &&
                  !performance_only.compact &&
                  Json::parse(read_file(file)).at("custom") == "preserve",
              "Performance-only options migrate to Free without losing unrelated fields");
        for (const auto &data : {Json{{"access_profile", nullptr}}, Json{{"access_profile", ""}}}) {
            atomic_write(file, data.dump(), true);
            check(Options::load(migration.state()).access_profile ==
                      cymcp::LocalAccessProfile::free,
                  "An empty access selection migrates to Free");
        }
        for (auto id :
             {"free", "local_controlled", "token_only", "protected", "protected_access"}) {
            auto original = Json{{"access_profile", id}, {"custom", "unchanged"}}.dump(2);
            atomic_write(file, original, true);
            check(Options::load(migration.state()).access_profile == parse_access_profile(id) &&
                      read_file(file) == original,
                  "Explicit profile selections are preserved byte for byte");
        }
        for (const auto &legacy :
             {Json{{"require_token", true}, {"control_operations", true}},
              Json{{"mcp_without_token", true}}, Json{{"allow_changes", true}}}) {
            atomic_write(file, legacy.dump(), true);
            auto options = Options::load(migration.state());
            auto expected = legacy.contains("mcp_without_token") ? "local_controlled"
                            : legacy.contains("allow_changes")   ? "token_only"
                                                                 : "protected";
            check(access_profile_name(options.access_profile) == std::string(expected) &&
                      Json::parse(read_file(file)).contains("access_profile"),
                  "Explicit legacy access controls retain their meaning during migration");
        }
        for (const auto &bad : {Json{{"access_profile", "unknown"}}, Json{{"access_profile", 1}},
                                Json{{"require_token", "true"}}, Json{{"retention_seconds", 0}}}) {
            auto before = bad.dump();
            atomic_write(file, before, true);
            refuses([&] { Options::load(migration.state()); },
                    "Invalid configuration does not silently grant Free");
            check(read_file(file) == before, "Invalid configuration is not rewritten");
        }
        struct Scenario {
            const char *name;
            cymcp::LocalAccessProfile profile;
            bool token, controlled;
        };
        for (const auto &scenario :
             {Scenario{"free", cymcp::LocalAccessProfile::free, false, false},
              {"local_controlled", cymcp::LocalAccessProfile::local_controlled, false, true},
              {"token_only", cymcp::LocalAccessProfile::token_only, true, false},
              {"protected", cymcp::LocalAccessProfile::protected_access, true, true}}) {
            auto directory = root / scenario.name;
            fs::create_directories(directory / "project");
            Core core(directory / "profile");
            Options::save_access_profile(core.state(), scenario.profile);
            Host host(core);
            check(host.access().require_token == scenario.token &&
                      host.access().control_operations == scenario.controlled &&
                      host.writes_allowed() == !scenario.controlled,
                  "All four profiles apply their SDK transport and policy settings");
            auto port = host.listen(0);
            auto origin = "http://127.0.0.1:" + std::to_string(port);
            auto native = [&](const std::string &token, const Json &request) {
                cymcp::HttpRequest http;
                http.method = "POST";
                http.url = origin + "/v1";
                http.body = request.dump();
                http.headers = {{"Content-Type", "application/json"}};
                if (!token.empty())
                    http.headers["Authorization"] = "Bearer " + token;
                return cymcp::HttpClient{}.perform(http);
            };
            auto read = Json{
                {"method", "invoke"}, {"name", "cysynck.overview"}, {"arguments", Json::object()}};
            auto without_token = native("", read);
            check(without_token.status == (scenario.token ? 401 : 200),
                  "Address-only native access follows the chosen profile");
            if (scenario.token) {
                check(native("incorrect-token", read).status == 401,
                      "Token profiles reject incorrect credentials");
                cymcp::McpClient anonymous(origin + "/mcp");
                refuses([&] { anonymous.connect(); },
                        "Token profiles reject an address-only SDK MCP client");
            }
            auto token = scenario.token ? core.api_token() : std::string{};
            cymcp::McpClient client(origin + "/mcp", token);
            client.connect();
            check(client.list_tools().size() == 5 && host.registry()->size() == 130,
                  "The complete business catalogue remains reachable through compact MCP");
            check(!client
                       .call_tool("cy_tools_call",
                                  {{"name", "cysynck.overview"}, {"arguments", Json::object()}})
                       .value("isError", false),
                  "SDK MCP reads the real application under every profile");
            auto create = Json{{"parameters",
                                {{"name", "Created without confirmation"},
                                 {"root", path_string(directory / "project")},
                                 {"mode", "backup"},
                                 {"backup_interval_minutes", 0}}}};
            auto result = client.call_tool(
                "cy_tools_call", {{"name", "cysynck.project.create"}, {"arguments", create}});
            check(result.value("isError", false) == scenario.controlled,
                  "Only controlled profiles require a write grant; Free and Token only execute "
                  "immediately");
            if (scenario.controlled) {
                check(core.request("GET", "/api/overview").at("projects").empty(),
                      "Denied writes leave the native application unchanged");
                host.allow_writes(true);
                result = client.call_tool(
                    "cy_tools_call", {{"name", "cysynck.project.create"}, {"arguments", create}});
                check(!result.value("isError", false),
                      "Controlled profiles retain the explicit native write grant");
            } else {
                host.allow_writes(false);
                check(host.writes_allowed(),
                      "An old write checkbox cannot silently restrict an unrestricted profile");
            }
            auto project = core.request("GET", "/api/overview").at("projects").at(0);
            auto change = native(token, {{"method", "invoke"},
                                         {"name", "cysynck.project.update"},
                                         {"arguments",
                                          {{"project_id", project.at("id")},
                                           {"parameters", {{"name", "Native update"}}}}}});
            check(change.status == 200 &&
                      core.request("GET", "/api/overview").at("projects").at(0).at("name") ==
                          "Native update",
                  "The native HTTP endpoint performs real writes through the selected profile");
            auto invalid = create;
            invalid["parameters"]["name"] = 123;
            auto bad = client.call_tool(
                "cy_tools_call", {{"name", "cysynck.project.create"}, {"arguments", invalid}});
            check(bad.value("isError", false) &&
                      core.request("GET", "/api/overview").at("projects").size() == 1,
                  "Schema validation remains active even for fully authorized calls");
            if (!scenario.controlled) {
                cymcp::CallContext another_local_caller;
                another_local_caller.principal = "unauthenticated-local";
                check(host.registry()
                          ->invoke("cysynck.overview", Json::object(), another_local_caller)
                          .contains("result"),
                      "Unrestricted registries use Policy::allow_all without an extra principal "
                      "gate");
            }
        }
        std::cout << "PASS " << checks
                  << " MCP access-profile checks; temporary profiles removed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
