#include <cysynck/core.hpp>
#include <cysynck/c_api.h>
#include <functional>
#include <iostream>
#include <set>
using namespace cysynck;
static int checks = 0;
static void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
    ++checks;
}
static void rejects(const std::function<void()> &call, const char *message) {
    try {
        call();
    } catch (const std::exception &) {
        ++checks;
        return;
    }
    throw std::runtime_error(message);
}
int main() {
    auto work = fs::current_path() / "backup-plan-test-data" / random_id();
    try {
        auto root = work / fs::u8path("Projet été");
        fs::create_directories(root);
        atomic_write(root / "source.cpp", "int value = 1;\n");
        atomic_write(root / "nested/keep.txt", "kept\n");
        atomic_write(root / "nested/drop.tmp", "omitted\n");
        atomic_write(root / ".git/objects/untouched", "metadata\n");
        atomic_write(root / ".cysynck-recovery/private.txt", "original\n");
        atomic_write(root / fs::u8path("été.txt"), "Unicode\n");
        atomic_write(root / "empty.txt", "");
        atomic_write(root / "large.bin", std::string(9 * 1024 * 1024, 'x'));
        Core core(work / "state");
        Json config{{"id", "p"},
                    {"name", "Preview"},
                    {"mode", "backup"},
                    {"root", path_string(root)},
                    {"backup_interval_minutes", 0},
                    {"backup_keep", 20}};
        core.request("POST", "/api/projects", config);
        auto action = [&](const std::string &name, const Json &input) {
            return core.request("POST", "/api/projects/p/" + name, input);
        };
        auto save = [&](const Json &rules, const Json &plan) {
            return core.request("PUT", "/api/projects/p/backup-excludes",
                                {{"excludes", rules}, {"plan", plan}});
        };
        auto before = read_file(core.state() / "registry.json");
        auto initial = action("backup-preview", Json::object());
        check(initial.at("files") == 6 && initial.at("skipped") == 2,
              "Default rules prune excluded directories");
        check(initial.at("total") == 8 && initial.at("visited") == 9,
              "Pruned descendants are not counted as visited");
        check(initial.at("content_verified") == false && !initial.at("rules_changed"),
              "Preview reports metadata-only scope");
        check(before == read_file(core.state() / "registry.json") &&
                  fs::is_empty(core.state() / "objects") && !fs::exists(core.state() / "snapshots"),
              "Preview writes no registry, snapshots or backup payloads");
        std::set<std::string> names;
        auto plan = initial.at("plan");
        for (size_t offset = 0; offset < 8; offset += 3) {
            auto page =
                action("backup-preview", {{"offset", offset},
                                          {"limit", 3},
                                          {"inventory_hash", initial.at("inventory_hash")}});
            check(page.at("plan") == plan, "Plans remain identical across metadata pages");
            for (const auto &row : page.at("entries"))
                check(names.insert(row.at("path")).second, "Pages have unique entries");
        }
        check(names.size() == 8 && !names.contains(".git/objects/untouched"),
              "Pages cover visible entries without traversing pruned folders");
        auto rules = initial.at("excludes");
        rules.push_back("*.tmp");
        auto proposed = action("backup-preview", {{"excludes", rules}});
        check(proposed.at("files") == 5 && proposed.at("skipped") == 3 &&
                  proposed.at("rules_changed"),
              "Candidate rules preview their impact without saving");
        bool explained = false, blocks = false;
        for (const auto &row : proposed.at("entries")) {
            if (row.at("path") == "nested/drop.tmp")
                explained = row.at("rule") == "*.tmp" && row.at("reason") == "rule";
            if (row.at("path") == "large.bin")
                blocks = row.at("storage") == "blocks-v1";
        }
        check(explained && blocks,
              "Inventory explains the first matching rule and storage strategy");
        rejects([&] { save(rules, initial.at("plan")); },
                "A plan must pin the exact proposed rules");
        check(read_file(core.state() / "registry.json") == before,
              "Rejected rule save preserves state");
        check(save(rules, proposed.at("plan")).at("saved"), "Checked rules can be saved");
        rejects([&] { save(rules, proposed.at("plan")); },
                "Previous stored rules invalidate a reused save plan");
        auto stored = action("backup-preview", Json::object());
        rejects([&] { action("backup", {{"plan", proposed.at("plan")}}); },
                "Candidate preview does not authorize a backup after saving");
        auto backed = action("backup", {{"plan", stored.at("plan")}});
        check(backed.at("file_count") == stored.at("files") &&
                  backed.at("bytes") == stored.at("bytes"),
              "Checked backup agrees with preview counts and bytes");
        auto manifest = Json::parse(read_file(core.state() / "snapshots/p" /
                                              (backed.at("id").get<std::string>() + ".json")));
        for (const auto &row : stored.at("entries"))
            check(manifest.at("files").contains(row.at("path")) == row.at("included").get<bool>(),
                  "Actual backup and preview share exclusion semantics");
        auto fresh = action("backup-preview", Json::object());
        atomic_write(root / "new.txt", "new\n");
        rejects([&] { action("backup", {{"plan", fresh.at("plan")}}); },
                "Added files invalidate a checked backup");
        rejects([&] { save(Json::array(), fresh.at("plan")); },
                "Changed inventories invalidate rule saves");
        rejects(
            [&] {
                action("backup-preview",
                       {{"inventory_hash", fresh.at("inventory_hash")}, {"offset", 1}});
            },
            "Changed inventories invalidate continuation pages");
        fresh = action("backup-preview", Json::object());
        atomic_write(root / "source.cpp", "changed length\n");
        rejects([&] { action("backup", {{"plan", fresh.at("plan")}}); },
                "Changed file metadata invalidates backup");
        fresh = action("backup-preview", Json::object());
        fs::remove(root / "new.txt");
        rejects([&] { action("backup", {{"plan", fresh.at("plan")}}); },
                "Deleted files invalidate backup");
        fresh = action("backup-preview", Json::object());
        core.request("PUT", "/api/projects/p", {{"backup_storage", "cdc"}});
        rejects([&] { action("backup", {{"plan", fresh.at("plan")}}); },
                "Changed backup settings invalidate preview");
        auto cdc = action("backup-preview", Json::object());
        bool saw_cdc = false;
        for (const auto &row : cdc.at("entries"))
            if (row.at("path") == "large.bin")
                saw_cdc = row.at("storage") == "blocks-cdc-v1";
        check(saw_cdc, "CDC prediction matches size policy");
        auto all = action("backup-preview", {{"excludes", Json::array()}});
        check(all.at("files") == 7 && all.at("skipped") == 1,
              "An empty rule list still protects recovery originals");
        auto first = action("backup-preview", {{"excludes", {"*.tmp", "drop.tmp"}}});
        for (const auto &row : first.at("entries"))
            if (row.at("path") == "nested/drop.tmp")
                check(row.at("rule_index") == 0, "Overlapping rules report the first match");
        auto path_rules = action("backup-preview", {{"excludes", {"nested/*"}}});
        check(path_rules.at("files") == 5,
              "Full relative path globs keep existing slash semantics");
        for (const auto &bad :
             {Json("*.tmp"), Json{1}, Json{""}, Json{"line\nbreak"}, Json{std::string("a\0b", 3)},
              Json{std::string(1025, 'x')}, Json(std::vector<std::string>(257, "x")),
              Json(std::vector<std::string>(32, std::string(600, 'x')))}) {
            rejects([&] { action("backup-preview", {{"excludes", bad}}); },
                    "Malformed or excessive rules rejected");
            rejects([&] { core.request("PUT", "/api/projects/p", {{"excludes", bad}}); },
                    "Bulk settings share rule validation");
        }
        for (const auto &bad : {Json{{"limit", 0}}, Json{{"limit", 201}}, Json{{"offset", -1}},
                                Json{{"offset", 0.5}}, Json{{"offset", true}}})
            rejects([&] { action("backup-preview", bad); }, "Invalid pagination rejected");
        rejects([&] { core.request("GET", "/api/projects/p/backup-preview"); },
                "Preview requires POST");
        rejects([&] { core.request("POST", "/api/projects/p/backup-excludes"); },
                "Rule save requires PUT");
        auto rule_file = work / fs::u8path("règles.json");
        atomic_write(rule_file, rules.dump());
        auto from_cli = cli(
            core, {"backup-preview", "p", "--excludes", path_string(rule_file), "--limit", "2"});
        check(from_cli.at("entries").size() == 2, "CLI loads UTF-8 rule files and page bounds");
        check(cli(core, {"backup-excludes", "p", path_string(rule_file), "--plan",
                         from_cli.at("plan").get<std::string>()})
                  .at("saved"),
              "CLI saves reviewed exclusions");
        rejects([&] { cli(core, {"backup", "p", "--unknown", "x"}); },
                "CLI rejects unknown backup flags");
        rejects([&] { cli(core, {"backup-excludes", "p", path_string(rule_file)}); },
                "CLI requires reviewed rules token");
        config["id"] = "q";
        config["root"] = path_string(work / "other");
        fs::create_directories(work / "other");
        core.request("POST", "/api/projects", config);
        auto q = core.request("POST", "/api/projects/q/backup-preview");
        rejects([&] { action("backup", {{"plan", q.at("plan")}}); },
                "Project-bound token cannot cross projects");
        Core other(work / "other state");
        auto saved_registry = Json::parse(read_file(core.state() / "registry.json"));
        auto same_project = saved_registry.at("projects").at(0);
        other.request("POST", "/api/projects", same_project);
        auto foreign = other.request("POST", "/api/projects/p/backup-preview");
        rejects([&] { action("backup", {{"plan", foreign.at("plan")}}); },
                "Identical project identity in another profile cannot authorize backup");
        char *error = nullptr;
        auto handle = cysynck_open(path_string(core.state()).c_str(), &error);
        check(handle && !error, "C API opens preview profile");
        char *output = nullptr;
        check(cysynck_request(handle, "POST", "/api/projects/p/backup-preview", "{}", &output) ==
                      0 &&
                  output,
              "Unchanged C ABI exposes preview");
        auto c = Json::parse(output);
        cysynck_free(output);
        check(c.at("files") == 5 && c.at("content_verified") == false,
              "C API retains preview metadata semantics");
        cysynck_close(handle);
        std::error_code link_error;
        fs::create_directory_symlink(root / "nested", root / "linked", link_error);
        if (!link_error) {
            auto linked = action("backup-preview", Json::object());
            bool seen = false;
            for (const auto &row : linked.at("entries"))
                if (row.at("path") == "linked")
                    seen = row.at("reason") == "link";
            check(seen && linked.at("files") == 5,
                  "Directory links are explained and never traversed");
        }
        check(action("backup", Json::object()).at("file_count") == 5,
              "Existing unchecked manual backup remains compatible");
        std::cout << "PASS " << checks << " backup preview checks; fixtures: " << path_string(work)
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << "; fixtures: " << path_string(work) << '\n';
        return 1;
    }
}
