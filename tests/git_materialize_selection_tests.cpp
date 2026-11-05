#include <cysynck/core.hpp>
#include "git_lfs.hpp"
#include <functional>
#include <iostream>
using namespace cysynck;
static int checks = 0;
static void check(bool value, const char *message) {
    if (!value)
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
static std::string pointer(const std::string &bytes) {
    return "version https://git-lfs.github.com/spec/v1\noid sha256:" + sha256(bytes) + "\nsize " +
           std::to_string(bytes.size()) + "\n";
}
int main() {
    auto run = fs::current_path() / "git-materialize-selection-data" / random_id();
    fs::create_directories(run);
    try {
        for (auto format : {"sha1", "sha256"}) {
            auto work = run / format, root = work / fs::u8path("repo été");
            fs::create_directories(root);
            Core core(work / "profile");
            auto p = core.request("POST", "/api/projects",
                                  {{"id", "team"},
                                   {"name", "Selected assets"},
                                   {"root", path_string(root)},
                                   {"mode", "git-bundles"},
                                   {"git_lfs", true},
                                   {"exchange_path", path_string(work / "exchange")},
                                   {"backup_interval_minutes", 0}});
            auto setup = cli(core, {"git", "setup", "team", "--object-format", format});
            cli(core, {"git", "setup", "team", "--object-format", format, "--apply", "true",
                       "--plan", setup.at("plan")});
            git(p, {"config", "core.autocrlf", "false"});
            std::string asset("asset\0binary\r\n", 14);
            auto text = pointer(asset), empty = pointer(""),
                 unavailable = pointer("missing cache bytes");
            auto name = std::string("Assets/\xc3\xa9t\xc3\xa9 [1].uasset");
            atomic_write(root / ".gitattributes", "*.uasset filter=lfs diff=lfs merge=lfs -text\n");
            atomic_write(root / fs::u8path(name), text);
            atomic_write(root / "Assets/empty.uasset", empty);
            atomic_write(root / "AssetsElse/other.uasset", unavailable);
            atomic_write(root / "ordinary.txt", "ordinary\n");
            git(p, {"-c", "filter.lfs.process=", "-c", "filter.lfs.clean=", "-c",
                    "filter.lfs.smudge=", "-c", "filter.lfs.required=false", "add", "-A"});
            git(p, {"-c", "user.name=Selection", "-c", "user.email=selection@example.invalid", "-c",
                    "commit.gpgsign=false", "commit", "-m", "Pointers"});
            for (auto key : {"process", "clean", "smudge"})
                git(p, {"config", std::string("filter.lfs.") + key, "cysynck-helper-must-not-run"});
            atomic_write(work / "asset", asset);
            atomic_write(work / "empty", "");
            auto store = lfs_local_store(p);
            copy_lfs_object(work / "asset", store, git_lfs_pointer(text));
            copy_lfs_object(work / "empty", store, git_lfs_pointer(empty));
            auto request = [&](const Json &q) {
                return core.request("POST", "/api/projects/team/git-materialize", q);
            };
            auto index = sha256_file(root / ".git/index"),
                 config = sha256_file(root / ".git/config");
            auto all = request(Json::object());
            check(!all.at("can_materialize").get<bool>() && all.at("pending") == 3 &&
                      all.at("selection").is_null(),
                  "Default all-file behavior changed");
            auto query = Json{{"paths", Json::array({"Assets/"})}};
            auto ready = request(query);
            check(ready.at("can_materialize") && ready.at("pending") == 2 &&
                      ready.at("excluded") == 1 && ready.at("files").size() == 2,
                  "Unselected unavailable cache blocked the selected folder");
            check(ready.at("bytes") == asset.size() && read_file(root / fs::u8path(name)) == text &&
                      !fs::exists(root / ".cysynck-lfs"),
                  "Preview wrote files or counted excluded assets");
            auto exact = request({{"paths", Json::array({name, name})}});
            check(exact.at("pending") == 1 && exact.at("selection") == Json::array({name}),
                  "Exact UTF-8 selection was not deduplicated");
            auto overlap = request({{"paths", Json::array({name, "Assets/"})}});
            check(overlap.at("files") == ready.at("files") &&
                      overlap.at("plan") != ready.at("plan"),
                  "Plan did not bind the canonical path selection");
            auto changed_query = query;
            changed_query["paths"] = overlap.at("selection");
            changed_query["apply"] = true;
            changed_query["plan"] = ready.at("plan");
            rejects([&] { request(changed_query); },
                    "Different selection reused an existing token");
            for (auto value :
                 {Json(nullptr), Json("Assets/"), Json::array(), Json::array({1}),
                  Json::array({""}), Json::array({"../outside"}), Json::array({"/absolute"}),
                  Json::array({".git/config"}), Json::array({"Assets\\empty.uasset"}),
                  Json::array({"Assets"}), Json::array({"absent/"}), Json::array({"ordinary.txt"}),
                  Json::array({"Assets/", "assets/"})})
                rejects([&] { request({{"paths", value}}); },
                        "Invalid or unmatched selection accepted");
            Json excessive = Json::array();
            for (int i = 0; i < 257; ++i)
                excessive.push_back(name);
            rejects([&] { request({{"paths", excessive}}); }, "Too many selectors accepted");
            excessive = Json::array();
            for (int i = 0; i < 5; ++i)
                excessive.push_back(std::string(4000, 'x') + std::to_string(i));
            rejects([&] { request({{"paths", excessive}}); }, "Oversized selection accepted");
            auto paths_file = work / "selection.json";
            atomic_write(paths_file, query.at("paths").dump());
            check(cli(core, {"git", "materialize", "team", "--paths-file", path_string(paths_file)})
                          .at("plan") == ready.at("plan"),
                  "CLI paths-file and API plans differ");
            core.request("PUT", "/api/projects/team", {{"name", "Renamed assets"}});
            auto apply = query;
            apply["apply"] = true;
            apply["plan"] = ready.at("plan");
            rejects([&] { request(apply); }, "Project configuration change retained a stale token");
            ready = request(query);
            atomic_write(root / "ordinary.txt", "unselected local edit\n");
            check(request(query).at("plan") == ready.at("plan"),
                  "Unrelated working edit invalidated the selected plan");
            auto before = sha256_file(root / "AssetsElse/other.uasset");
            auto result =
                cli(core, {"git", "materialize", "team", "--paths-file", path_string(paths_file),
                           "--apply", "true", "--plan", ready.at("plan")});
            check(result.at("applied") && result.at("files_written") == 2 &&
                      result.at("selection") == query.at("paths"),
                  "Selected application lost its scope or wrote the wrong count");
            check(read_file(root / fs::u8path(name)) == asset &&
                      fs::file_size(root / "Assets/empty.uasset") == 0,
                  "Selected assets differ from cache bytes");
            check(sha256_file(root / "AssetsElse/other.uasset") == before &&
                      !fs::exists(lfs_object_path(store, sha256("missing cache bytes"))),
                  "Unselected pointer or cache was changed");
            check(read_file(root / "ordinary.txt") == "unselected local edit\n" &&
                      sha256_file(root / ".git/index") == index &&
                      sha256_file(root / ".git/config") == config,
                  "Materialization changed unrelated data or Git metadata");
            auto receipt = fs::u8path(result.at("receipt_path").get<std::string>());
            auto receipt_query = Json{{"receipt", result.at("id")}};
            auto verify = [&] {
                return core.request("POST", "/api/projects/team/git-materialization-check",
                                    receipt_query);
            };
            auto checked = verify();
            check(checked.at("position") == "after" && checked.at("errors").empty() &&
                      checked.at("files").size() == 2,
                  "Selected receipt did not verify against committed pointers");
            auto history = cli(core, {"git", "materializations", "team"});
            check(history.at("records")[0].at("selection") == query.at("paths") &&
                      history.at("records")[0].at("excluded") == 1,
                  "History hid the selected scope");
            auto corrupt = result;
            corrupt["selection"] = Json::array({"Assets/empty.uasset"});
            atomic_write(receipt, corrupt.dump(), true);
            rejects(verify, "Receipt accepted files outside its selection");
            corrupt = result;
            corrupt["excluded"] = 0;
            atomic_write(receipt, corrupt.dump(), true);
            check(!verify().at("errors").empty(), "Receipt accepted a false excluded count");
            atomic_write(receipt, result.dump(), true);
            check(verify().at("errors").empty(), "Restored receipt no longer verifies");
            auto legacy = result;
            for (auto key : {"selection", "excluded", "project_hash"})
                legacy.erase(key);
            atomic_write(receipt, legacy.dump(), true);
            check(verify().at("position") == "after" && verify().at("errors").empty(),
                  "Pre-selection receipt fields are no longer compatible");
            auto interrupted = result;
            interrupted["status"] = "incomplete";
            interrupted["applied"] = false;
            interrupted["files_written"] = 0;
            atomic_write(receipt, interrupted.dump(), true);
            checked = verify();
            check(checked.at("can_acknowledge") && checked.at("position") == "after",
                  "A selected operation stopped before its journal update cannot be checked");
            auto acknowledged = cli(core, {"git", "materialization-acknowledge", "team",
                                           result.at("id"), "--plan", checked.at("plan")});
            check(acknowledged.at("selection") == query.at("paths") &&
                      acknowledged.at("status") == "incomplete" &&
                      !acknowledged.at("needs_attention").get<bool>(),
                  "Acknowledgement lost the selected scope or incomplete evidence");
            check(!request(query).at("can_materialize").get<bool>(),
                  "Already hydrated selected files were offered again");
        }
        std::cout << "PASS " << checks << " selective LFS materialization checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\nFixture: " << path_string(run) << '\n';
        return 1;
    }
}
