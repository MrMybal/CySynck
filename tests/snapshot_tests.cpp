#include <cysynck/core.hpp>
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
    auto run = fs::current_path() / "snapshot-test-data" / random_id();
    fs::create_directories(run);
    try {
        auto root = run / fs::u8path("Projet été");
        fs::create_directories(root);
        Core core(run / "state");
        core.request("POST", "/api/projects",
                     {{"id", "p"},
                      {"name", "p"},
                      {"root", path_string(root)},
                      {"mode", "backup"},
                      {"backup_interval_minutes", 0},
                      {"backup_keep", 20}});
        auto action = [&](const std::string &name, const Json &data) {
            return core.request("POST", "/api/projects/p/" + name, data);
        };
        atomic_write(root / "source.cpp", "int value = 1;\n");
        atomic_write(root / "deleted.txt", "remove me\n");
        atomic_write(root / "empty.txt", "");
        atomic_write(root / "binary.bin", std::string("a\0b", 3));
        atomic_write(root / "folder/a.txt", "nested A\n");
        atomic_write(root / fs::u8path("folder/été.txt"), "nested B\n");
        atomic_write(root / "folder-other/keep.txt", "sibling\n");
        atomic_write(root / "large.bin", std::string(9 * 1024 * 1024, 'x'));
        for (int i = 0; i < 215; ++i)
            atomic_write(root / ("page/" + std::to_string(i) + ".txt"), "shared\n");
        auto first = action("backup", Json::object());
        auto a = first.at("id").get<std::string>();
        auto manifest_path = core.state() / "snapshots/p" / (a + ".json");
        auto manifest = Json::parse(read_file(manifest_path));
        Json query{{"snapshot", a}, {"limit", 100}};
        auto page = action("snapshot-files", query);
        check(page.at("total") == 223 && page.at("files").size() == 100 && page.at("has_more"),
              "Bounded first metadata page");
        check(page.at("content_verified") == false && !page.at("files").at(0).contains("content"),
              "Catalog does not claim content verification");
        std::set<std::string> names;
        for (size_t offset = 0; offset < 223; offset += 100) {
            query["offset"] = offset;
            query["manifest_hash"] = page.at("manifest_hash");
            auto next = action("snapshot-files", query);
            for (const auto &entry : next.at("files"))
                check(names.insert(entry.at("path")).second, "Pages do not duplicate files");
        }
        check(names.size() == 223, "Pages cover the complete manifest");
        auto folder = action("snapshot-files", {{"snapshot", a}, {"prefix", "folder"}});
        check(folder.at("total") == 2 && folder.at("prefix") == "folder/",
              "Folder prefix does not select a sibling");
        atomic_write(core.state() / "snapshots/p/broken.json", "{broken");
        check(action("snapshot-files", {{"snapshot", a}}).at("total") == 223,
              "Explicit browsing does not depend on neighboring manifests");
        fs::remove(core.state() / "snapshots/p/broken.json");
        for (const auto &bad :
             {Json{{"limit", 0}}, Json{{"offset", -1}}, Json{{"limit", 201}}, Json{{"limit", 1.5}},
              Json{{"prefix", "../"}}, Json{{"prefix", "/abs"}}}) {
            auto input = bad;
            input["snapshot"] = a;
            rejects([&] { action("snapshot-files", input); }, "Invalid page or prefix rejected");
        }
        rejects([&] { action("snapshot-files", {{"snapshot", "../x"}}); },
                "Manifest traversal rejected");
        query["manifest_hash"] = "stale";
        rejects([&] { action("snapshot-files", query); }, "Stale page token rejected");
        atomic_write(core.state() / "objects" / sha256("remove me\n"), "corrupt");
        check(action("snapshot-files", {{"snapshot", a}}).at("total") == 223,
              "Metadata browsing works with a damaged payload");
        auto destination = run / fs::u8path("Restauré été");
        Json selection{{"snapshot", a},
                       {"paths", {"folder/", "folder/a.txt", "large.bin", "empty.txt"}},
                       {"destination", path_string(destination)}};
        auto plan = action("restore-plan", selection);
        check(plan.at("ok") && plan.at("files") == 4 && plan.at("selection").size() == 4,
              "Selection verifies only requested files and deduplicates overlaps");
        check(!fs::exists(destination), "Preview creates no destination");
        rejects([&] { action("restore", selection); }, "Partial restore requires an explicit plan");
        selection["plan"] = plan.at("plan");
        auto changed = selection;
        changed["paths"] = {"folder/"};
        rejects([&] { action("restore", changed); }, "Changed selection invalidates the plan");
        changed = selection;
        changed["destination"] = path_string(run / "different");
        rejects([&] { action("restore", changed); }, "Changed destination invalidates the plan");
        auto restored = action("restore", selection);
        check(restored.at("files") == 4 &&
                  read_file(destination / "folder/a.txt") == "nested A\n" &&
                  sha256_file(destination / "large.bin") ==
                      manifest.at("files").at("large.bin").at("sha256").get<std::string>(),
              "Selected whole and block files restored exactly");
        check(!fs::exists(destination / "deleted.txt") && !fs::exists(destination / "folder-other"),
              "Unselected files remain absent");
        rejects([&] { action("restore", selection); }, "Destination cannot be reused");
        for (const auto &paths :
             {Json::array(), Json::array({"../x"}), Json::array({"missing"}),
              Json::array({"folder"}), Json::array({"/abs"}), Json::array({"folder\\a.txt"})}) {
            auto bad = selection;
            bad["paths"] = paths;
            bad["destination"] = path_string(run / "bad");
            rejects([&] { action("restore-plan", bad); }, "Invalid or absent selection refused");
        }
        check(!fs::exists(run / "bad"), "Bad selection creates no output");
        auto too_many = selection;
        too_many["destination"] = path_string(run / "many");
        too_many["paths"] = Json::array();
        for (int i = 0; i < 257; ++i)
            too_many["paths"].push_back("source.cpp");
        rejects([&] { action("restore-plan", too_many); },
                "Selection count is bounded even for duplicate selectors");
        auto block_path =
            core.state() / "blocks" /
            manifest.at("files").at("large.bin").at("blocks").at(0).at("sha256").get<std::string>();
        auto block_bytes = read_file(block_path, 4 * 1024 * 1024);
        atomic_write(block_path, std::string(block_bytes.size(), 'z'));
        auto corrupt_selection = selection;
        corrupt_selection["destination"] = path_string(run / "bad-block");
        check(!action("restore-plan", corrupt_selection).at("ok").get<bool>(),
              "Selected same-size corrupt blocks fail integrity");
        rejects([&] { action("restore", corrupt_selection); },
                "Corrupt selected blocks cannot create output");
        check(!fs::exists(run / "bad-block"), "Corrupt selected output remains absent");
        atomic_write(block_path, block_bytes);
        auto all =
            action("restore-plan", {{"snapshot", a}, {"destination", path_string(run / "full")}});
        check(!all.at("ok").get<bool>(), "Full restoration still detects unselected corruption");
        atomic_write(core.state() / "objects" / sha256("remove me\n"), "remove me\n");
        atomic_write(root / "source.cpp", "int value = 2;\n");
        fs::remove(root / "deleted.txt");
        atomic_write(root / "added.txt", "new file\n");
        fs::permissions(root / "empty.txt", fs::perms::owner_read);
        core.request("PUT", "/api/projects/p", {{"backup_storage", "cdc"}});
        auto second = action("backup", Json::object());
        auto b = second.at("id").get<std::string>();
        Json cdc_selection{
            {"snapshot", b}, {"paths", {"large.bin"}}, {"destination", path_string(run / "cdc")}};
        auto cdc_plan = action("restore-plan", cdc_selection);
        cdc_selection["plan"] = cdc_plan.at("plan");
        action("restore", cdc_selection);
        check(sha256_file(run / "cdc/large.bin") ==
                  manifest.at("files").at("large.bin").at("sha256").get<std::string>(),
              "Selected CDC files restore across storage policy changes");
        Json comparison{{"left", a}, {"right", b}, {"limit", 2}};
        auto changes = action("snapshot-compare", comparison);
        check(changes.at("total") == 4 && changes.at("has_more"),
              "Added, removed, modified and permission-only files listed");
        comparison["offset"] = 2;
        comparison["left_hash"] = changes.at("left_hash");
        comparison["right_hash"] = changes.at("right_hash");
        check(action("snapshot-compare", comparison).at("files").size() == 2,
              "Snapshot comparison is paginated");
        Json file{{"left", a}, {"right", b}, {"path", "source.cpp"}};
        auto diff = action("snapshot-compare-file", file);
        check(!diff.at("binary").get<bool>() && diff.at("patch_available") &&
                  diff.at("before") == "int value = 1;\n" && diff.at("after") == "int value = 2;\n",
              "Committed snapshot bytes yield an exact text patch");
        atomic_write(root / "source.cpp", "unrelated current work\n");
        check(action("snapshot-compare-file", file) == diff,
              "Snapshot diff does not read current project files");
        file["path"] = "added.txt";
        check(!action("snapshot-compare-file", file).at("before_info").at("exists").get<bool>(),
              "Added file has absent left side");
        file["path"] = "deleted.txt";
        check(!action("snapshot-compare-file", file).at("after_info").at("exists").get<bool>(),
              "Deleted file has absent right side");
        file["path"] = "binary.bin";
        check(action("snapshot-compare-file", file).at("binary"),
              "Binary files have no text patch");
        file["path"] = "large.bin";
        auto large = action("snapshot-compare-file", file);
        check(large.at("binary") && large.at("identical").is_null() &&
                  !large.at("patch_available").get<bool>(),
              "Large block files stay bounded and do not claim payload equality");
        file["path"] = "empty.txt";
        check(action("snapshot-compare-file", file).at("permissions_changed"),
              "Permission-only changes exposed beside identical content");
        file["path"] = "../x";
        rejects([&] { action("snapshot-compare-file", file); }, "Absent diff path refused");
        file["path"] = "source.cpp";
        file["left_hash"] = "stale";
        rejects([&] { action("snapshot-compare-file", file); },
                "File comparison checks both selected manifest hashes");
        file.erase("left_hash");
        atomic_write(core.state() / "objects" / sha256("int value = 1;\n"), "bad bytes");
        check(action("snapshot-compare", {{"left", a}, {"right", b}}).at("total") == 4,
              "Comparison catalog does not load payloads");
        rejects([&] { action("snapshot-compare-file", file); },
                "Text comparison verifies corrupt snapshot bytes");
        atomic_write(core.state() / "objects" / sha256("int value = 1;\n"), "int value = 1;\n");
        auto cli_page = cli(core, {"snapshot-files", "p", a, "--prefix", "folder", "--limit", "1"});
        check(cli_page.at("files").size() == 1, "CLI metadata paging");
        rejects([&] { cli(core, {"snapshot-files", "p", a, "--offset", "-1"}); },
                "CLI rejects negative page numbers");
        atomic_write(run / "paths.json", "[\"source.cpp\"]");
        auto cli_plan = cli(core, {"restore-plan", "p", a, path_string(run / "cli"), "--paths-file",
                                   path_string(run / "paths.json")});
        cli(core, {"restore", "p", a, path_string(run / "cli"), "--paths-file",
                   path_string(run / "paths.json"), "--plan", cli_plan.at("plan")});
        check(read_file(run / "cli/source.cpp") == "int value = 1;\n",
              "CLI restores an exact JSON-file selection");
        auto stale_data = Json{{"snapshot", a},
                               {"paths", {"source.cpp"}},
                               {"destination", path_string(run / "stale")}};
        auto stale_plan = action("restore-plan", stale_data);
        stale_data["plan"] = stale_plan.at("plan");
        auto modified = manifest;
        modified["reason"] = "changed metadata";
        atomic_write(manifest_path, modified.dump());
        rejects([&] { action("restore", stale_data); },
                "Manifest metadata changes invalidate restoration preview");
        check(!fs::exists(run / "stale"), "Stale restore creates no output");
        atomic_write(manifest_path, manifest.dump());
        std::cout << "PASS " << checks
                  << " snapshot workflow checks; fixtures: " << path_string(run) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << "; fixtures: " << path_string(run) << '\n';
        return 1;
    }
}
