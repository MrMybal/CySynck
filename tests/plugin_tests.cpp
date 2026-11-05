#include <cysynck/core.hpp>
#include <cysynck/c_api.h>
#include "plugin_metadata.hpp"
#include <functional>
#include <iostream>
using namespace cysynck;
static int checks = 0;
static void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
    ++checks;
}
static void rejects(const std::function<void()> &fn, const char *message) {
    try {
        fn();
    } catch (const std::exception &) {
        ++checks;
        return;
    }
    throw std::runtime_error(message);
}
static Json add(Core &core, const fs::path &root, const std::string &id, const std::string &mode,
                const Json &options = Json::object()) {
    fs::create_directories(root);
    return core.request("POST", "/api/projects",
                        {{"id", id},
                         {"name", id},
                         {"root", path_string(root)},
                         {"mode", mode},
                         {"backup_interval_minutes", 0},
                         {"backup_before_sync", false},
                         {"mode_options", options}});
}
int main(int argc, char **argv) {
    auto work = fs::current_path() / "plugin-test-data" / random_id();
    fs::create_directories(work);
    try {
        if (argc != 7)
            throw std::runtime_error("Expected six native plugin fixture paths");
        auto legacy = fs::u8path(argv[1]), report = fs::u8path(argv[2]),
             memory = fs::u8path(argv[3]);
        Core core(work / "state");
        check(core.request("GET", "/api/plugins").at("plugins").empty(),
              "Plugins are not discovered or loaded implicitly");
        core.load_plugin(legacy);
        auto inventory = cli(core, {"plugins"});
        check(inventory.at("plugins").size() == 1 &&
                  inventory.at("plugins")[0].at("descriptor") == false,
              "Legacy ABI v1 remains loadable without metadata");
        check(inventory.at("plugins")[0].at("modes")[0].at("description") ==
                  "Example native extension without file mutation",
              "Legacy mode descriptions become available");
        rejects([&] { core.load_plugin(legacy); }, "Duplicate library rejected before publication");
        core.load_plugin(report);
        inventory = core.request("GET", "/api/plugins");
        const auto &catalog = inventory.at("plugins")[1];
        check(catalog.at("id") == "example.report" && catalog.at("version") == "1.0.0" &&
                  catalog.at("descriptor"),
              "Optional descriptor exposes plugin identity and version");
        check(catalog.at("sha256") == sha256_file(report) &&
                  catalog.at("path") == path_string(fs::canonical(report)),
              "Inventory pins the selected canonical library and its hash");
        check(catalog.at("modes")[0].at("fields").size() == 3 &&
                  catalog.at("name_fr") == "Exemple de rapport de projet",
              "Typed schema and French labels are retained");
        for (int i = 4; i < 7; ++i)
            rejects([&] { core.load_plugin(fs::u8path(argv[i])); },
                    "Malformed schema, duplicate ID and unsupported ABI rejected");
        check(core.request("GET", "/api/plugins").at("plugins").size() == 2,
              "Rejected libraries publish no modes or inventory records");
        auto a = work / fs::u8path("projet été"), b = work / "other";
        auto created = add(core, a, "a", "project-report");
        add(core, b, "b", "project-report");
        atomic_write(a / fs::u8path("été.txt"), "project data\n");
        check(created.at("mode_options").at("max_entries") == 100 &&
                  created.at("mode_options").at("include_names") == false,
              "Declared defaults are persisted on project creation");
        auto settings = core.request("GET", "/api/projects/a/mode-options");
        check(settings.at("available") && settings.at("valid") && settings.at("fields").size() == 3,
              "Project mode schema and defaults inspect without execution");
        auto values = Json{{"title", "Rapport été"}, {"max_entries", 5}, {"include_names", true}};
        auto saved = core.request("PUT", "/api/projects/a/mode-options",
                                  {{"plan", settings.at("plan")}, {"values", values}});
        check(saved.at("values") == values,
              "Project overrides survive normalized typed validation");
        rejects(
            [&] {
                core.request("PUT", "/api/projects/a/mode-options",
                             {{"plan", settings.at("plan")}, {"values", values}});
            },
            "Concurrent settings changes invalidate previous plans");
        auto other = core.request("GET", "/api/projects/b/mode-options");
        check(other.at("values").at("title") == "Project report",
              "Different projects keep independent mode options");
        rejects(
            [&] {
                core.request("PUT", "/api/projects/b/mode-options",
                             {{"plan", saved.at("plan")}, {"values", values}});
            },
            "Mode option plans are tied to the selected project");
        {
            Core other_profile(work / "other-profile");
            other_profile.load_plugin(report);
            add(other_profile, a, "a", "project-report", values);
            rejects(
                [&] {
                    other_profile.request("PUT", "/api/projects/a/mode-options",
                                          {{"plan", saved.at("plan")}, {"values", values}});
                },
                "Mode option plans are tied to the state profile even for the same project root");
        }
        for (const auto &bad :
             {Json{{"title", std::string(161, 'x')}}, Json{{"title", 3}},
              Json{{"include_names", 1}}, Json{{"max_entries", 0}}, Json{{"max_entries", 501}},
              Json{{"max_entries", 1.5}}, Json{{"unknown", true}}, Json::array()}) {
            rejects(
                [&] {
                    core.request("PUT", "/api/projects/a/mode-options",
                                 {{"plan", saved.at("plan")}, {"values", bad}});
                },
                "Invalid typed settings are rejected");
            check(core.request("GET", "/api/projects/a/mode-options").at("values") == values,
                  "Rejected settings preserve prior values");
        }
        auto result = core.request("POST", "/api/projects/a/sync");
        check(result.at("title") == "Rapport été" && result.at("files") == 1 &&
                  result.at("entries").size() == 1,
              "Native mode receives effective project options");
        check(read_file(a / fs::u8path("été.txt")) == "project data\n",
              "Example report preserves project bytes");
        {
            Core reopened(core.state());
            reopened.load_plugin(report);
            check(reopened.request("GET", "/api/projects/a/mode-options").at("values") == values,
                  "Options persist across a restarted host");
        }
        {
            Core unloaded(core.state());
            auto unavailable = unloaded.request("GET", "/api/projects/a/mode-options");
            check(!unavailable.at("available").get<bool>() && unavailable.at("plan").is_null(),
                  "Missing plugins are reported without deleting settings");
            rejects([&] { unloaded.request("POST", "/api/projects/a/sync"); },
                    "A mode cannot run without its explicitly loaded plugin");
            check(unloaded.request("POST", "/api/projects/a/backup").at("file_count") == 1,
                  "Backups remain available while the mode plugin is absent");
        }
        atomic_write(work / "options.json", "{\"title\":\"CLI report\"}");
        auto fresh = cli(core, {"project", "mode-options", "a"});
        cli(core, {"project", "mode-options-set", "a", path_string(work / "options.json"), "--plan",
                   fresh.at("plan")});
        check(core.request("POST", "/api/projects/a/sync").at("title") == "CLI report",
              "CLI settings files apply through the same checked route");
        core.load_plugin(memory);
        add(core, work / "memory", "m", "fixture-memory");
        auto run = [&] { return core.request("POST", "/api/projects/m/sync"); };
        auto set = [&](const std::string &behavior) {
            core.request("PUT", "/api/projects/m", {{"mode_options", {{"behavior", behavior}}}});
        };
        auto first = run();
        check(first.at("project_options") == first.at("context_options"),
              "Resolved options are present in both plugin call contexts");
        check(run().at("released") == first.at("released").get<int>() + 1,
              "Successful plugin results are released exactly once");
        set("invalid");
        rejects([&] { run(); }, "Invalid plugin JSON is rejected");
        set("valid");
        auto after_invalid = run().at("released").get<int>();
        check(after_invalid == first.at("released").get<int>() + 3,
              "Failed JSON still releases the plugin buffer");
        set("oversize");
        rejects([&] { run(); }, "Oversize mode results are bounded");
        set("valid");
        check(run().at("released") == after_invalid + 2,
              "Oversize result is released before reporting failure");
        set("veto");
        rejects([&] { run(); }, "Existing before-event veto remains effective");
        set("valid");
        check(run().at("released") == after_invalid + 4,
              "Veto strings retain their original ownership contract");
        char *out = nullptr;
        auto handle = cysynck_open(path_string(work / fs::u8path("C profil été")).c_str(), &out);
        check(handle && !out, "C ABI opens an independent Unicode profile");
        check(cysynck_load_plugin(handle, path_string(report).c_str(), &out) == 0 && out,
              "C ABI explicitly loads a native plugin");
        auto loaded = Json::parse(out);
        cysynck_free(out);
        out = nullptr;
        check(loaded.at("plugins")[0].at("id") == "example.report",
              "C ABI load returns the validated catalog");
        check(cysynck_load_plugin(handle, path_string(report).c_str(), &out) != 0 && out,
              "C ABI reports duplicate loading as JSON");
        cysynck_free(out);
        out = nullptr;
        check(cysynck_load_plugin(nullptr, path_string(report).c_str(), &out) != 0 && out,
              "C ABI validates invalid handles before loading");
        cysynck_free(out);
        out = nullptr;
        check(cysynck_load_plugin(handle, path_string(report).c_str(), nullptr) != 0,
              "C ABI requires an output pointer before any load");
        check(cysynck_load_plugin(handle, path_string(memory).c_str(), &out) == 0,
              "C ABI loads an additional native plugin");
        cysynck_free(out);
        out = nullptr;
        fs::create_directories(work / "C project");
        auto c_project = Json{{"id", "c"},
                              {"name", "C fixture"},
                              {"root", path_string(work / "C project")},
                              {"mode", "fixture-memory"},
                              {"backup_before_sync", false},
                              {"backup_interval_minutes", 0},
                              {"mode_options", {{"behavior", "throw"}}}}
                             .dump();
        check(cysynck_request(handle, "POST", "/api/projects", c_project.c_str(), &out) == 0,
              "C ABI registers a plugin-mode project");
        cysynck_free(out);
        out = nullptr;
        check(cysynck_request(handle, "POST", "/api/projects/c/sync", "{}", &out) != 0 && out,
              "Unexpected plugin exceptions do not cross the C request boundary");
        cysynck_free(out);
        out = nullptr;
        cysynck_close(handle);
        // A malformed saved override is diagnosable and cannot run silently.
        auto registry = Json::parse(read_file(core.state() / "registry.json"));
        registry["projects"][0]["mode_options"] = {{"max_entries", "wrong"}};
        atomic_write(core.state() / "registry.json", registry.dump());
        auto invalid = core.request("GET", "/api/projects/a/mode-options");
        check(!invalid.at("valid").get<bool>() && invalid.at("values") == invalid.at("defaults"),
              "Invalid stored options expose defaults and diagnostics for explicit repair");
        rejects([&] { core.request("POST", "/api/projects/a/sync"); },
                "Invalid persisted options are checked before running a mode");
        core.request("PUT", "/api/projects/a/mode-options",
                     {{"plan", invalid.at("plan")}, {"values", Json::object()}});
        check(core.request("GET", "/api/projects/a/mode-options").at("valid"),
              "Checked reset repairs invalid mode settings");
        std::cout << "PASS " << checks << " plugin workflow checks; fixtures: " << path_string(work)
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << "; fixtures: " << path_string(work) << '\n';
        return 1;
    }
}
