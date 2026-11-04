#include <cysynck/core.hpp>
#include <functional>
#include <future>
#include <iostream>
#include <thread>

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
static void add(Core &core, const fs::path &root, const std::string &id, int keep = 2) {
    fs::create_directories(root);
    core.request("POST", "/api/projects",
                 {{"id", id},
                  {"name", id},
                  {"mode", "backup"},
                  {"root", path_string(root)},
                  {"backup_interval_minutes", 0},
                  {"backup_keep", keep}});
}
static Json action(Core &core, const std::string &id, const std::string &name,
                   const Json &data = Json::object()) {
    return core.request("POST", "/api/projects/" + id + "/" + name, data);
}
int main() {
    auto run = fs::current_path() / "test-data" / random_id();
    fs::create_directories(run);
    try {
        Core core(run / "state");
        auto a = run / fs::u8path("Projet été"), b = run / "other";
        add(core, a, "a");
        add(core, b, "b", 1);
        check(core.request("GET", "/api/storage").at("objects") == 0, "Empty storage statistics");
        rejects([&] { action(core, "a", "verify"); }, "No empty synthetic backup for verification");
        rejects(
            [&] { action(core, "a", "restore", {{"destination", path_string(run / "empty")}}); },
            "Restore requires an actual snapshot");
        check(!fs::exists(run / "empty"), "Missing snapshot creates no destination");
        atomic_write(a / "common.txt", "shared\n");
        atomic_write(a / "nested/duplicate.txt", "shared\n");
        atomic_write(a / "version.txt", "first\n");
        atomic_write(b / "version.txt", "first\n");
        auto first = action(core, "a", "backup");
        action(core, "b", "backup");
        auto verified = cli(core, {"verify", "a"});
        check(verified.at("ok") && verified.at("files") == 3 && verified.at("objects_checked") == 2,
              "Integrity hashes unique objects and reports all logical files");
        auto target = run / fs::u8path("Restauration été/new copy");
        auto data = Json{{"snapshot", first.at("id")}, {"destination", path_string(target)}};
        auto plan = action(core, "a", "restore-plan", data);
        check(plan.at("ok") && plan.at("entries").size() == 3, "Restore preview lists exact files");
        check(!fs::exists(target.parent_path()),
              "Restore preview writes no destination or parents");
        rejects(
            [&] {
                action(core, "a", "restore-plan", {{"destination", path_string(b / "inside")}});
            },
            "Restore cannot enter any other project");
        action(core, "a", "restore", data);
        check(read_file(target / "version.txt") == "first\n" &&
                  read_file(target / "nested/duplicate.txt") == "shared\n",
              "Restore recovers exact nested bytes");
        rejects([&] { action(core, "a", "restore", data); },
                "Existing destination never overwritten");
        atomic_write(a / "version.txt", "second\n");
        action(core, "a", "backup");
        atomic_write(a / "version.txt", "third\n");
        auto latest = action(core, "a", "backup");
        auto status = cli(core, {"storage", "status"});
        check(status.at("snapshots") == 3 && status.at("unused_objects") == 0,
              "Retention preserves objects still referenced by another project");
        atomic_write(b / "version.txt", "fourth\n");
        action(core, "b", "backup");
        status = cli(core, {"storage", "prune"});
        check(!status.at("applied").get<bool>() && status.at("unused_objects") == 1 &&
                  status.at("reclaimable_bytes") == 6,
              "Cleanup defaults to an exact size preview");
        check(fs::exists(core.state() / "objects" / sha256("first\n")),
              "Preview preserves unused bytes");
        rejects([&] { cli(core, {"storage", "prune", "--apply", "yes"}); },
                "Invalid apply flag rejected");
        rejects([&] { cli(core, {"storage", "prune", "--apply", "true"}); },
                "Apply requires preview token");
        auto stale = status.at("plan").get<std::string>();
        atomic_write(a / "version.txt", "fifth\n");
        latest = action(core, "a", "backup");
        rejects([&] { cli(core, {"storage", "prune", "--apply", "true", "--plan", stale}); },
                "Snapshot/retention changes invalidate cleanup preview");
        check(fs::exists(core.state() / "objects" / sha256("first\n")),
              "Stale plan deletes nothing");

        // Include manifests belonging to a project no longer listed in the registry.
        auto registryPath = core.state() / "registry.json";
        auto registry = Json::parse(read_file(registryPath));
        registry["projects"].erase(registry["projects"].begin() + 1);
        atomic_write(registryPath, registry.dump());
        atomic_write(core.state() / "objects/part-interrupted", "preserve temporary bytes");
        atomic_write(core.state() / "objects/notes.txt", "preserve unknown bytes");
        status = core.request("GET", "/api/storage");
        check(status.at("ignored_entries").size() == 2 && status.at("snapshots") == 3,
              "Unlisted projects count; unknown object entries are preserved");
        auto manifestPath =
            core.state() / "snapshots/a" / (latest.at("id").get<std::string>() + ".json");
        auto original = Json::parse(read_file(manifestPath));
        atomic_write(core.state() / "snapshots/a/broken.json", "{broken");
        auto broken = core.request("GET", "/api/storage");
        check(!broken.at("can_prune").get<bool>() && !broken.at("errors").empty(),
              "Unreadable manifest blocks cleanup of all objects");
        check(core.request("GET", "/api/overview").at("projects").at(0).contains("backup_error"),
              "A damaged manifest remains diagnosable without breaking the overview");
        rejects(
            [&] {
                core.request("POST", "/api/storage/prune",
                             {{"apply", true}, {"plan", status.at("plan")}});
            },
            "Cleanup refuses a damaged catalog even with an older valid preview");
        fs::remove(core.state() / "snapshots/a/broken.json");

        auto goodObject = core.state() / "objects" / sha256("fifth\n");
        atomic_write(goodObject, "damaged!");
        verified = action(core, "a", "verify");
        check(!verified.at("ok").get<bool>() &&
                  verified.at("issues").at(0).at("path") == "version.txt",
              "Integrity identifies the corrupt file");
        auto corruptTarget = run / "corrupt/no-partial-directory";
        data = {{"snapshot", latest.at("id")}, {"destination", path_string(corruptTarget)}};
        check(!action(core, "a", "restore-plan", data).at("ok").get<bool>(),
              "Corrupt backup preview is unusable");
        rejects([&] { action(core, "a", "restore", data); }, "Corrupt backup refuses restoration");
        check(!fs::exists(corruptTarget.parent_path()),
              "Integrity failure creates no partial directory");
        latest = action(core, "a", "backup");
        check(action(core, "a", "verify").at("ok") && read_file(goodObject) == "fifth\n",
              "Fresh capture repairs an existing damaged deduplicated object");
        check(action(core, "a", "verify", {{"snapshot", original.at("id")}}).at("ok"),
              "Repair restores older references to the same object");
        fs::remove(goodObject);
        check(!action(core, "a", "verify").at("ok").get<bool>(), "Missing object is reported");
        check(core.request("GET", "/api/storage").at("missing_objects") == 1,
              "Storage statistics count missing referenced objects");
        atomic_write(goodObject, "fifth\n");

        // Deliberately hostile manifests must not permit traversal or retention deletion.
        for (const auto &kind : {"digest", "path", "id", "size", "collision"}) {
            auto m = original;
            if (std::string(kind) == "digest")
                m["files"]["version.txt"]["sha256"] = "../../registry.json";
            if (std::string(kind) == "path") {
                m["files"]["../outside.txt"] = m["files"]["version.txt"];
                m["files"].erase("version.txt");
            }
            if (std::string(kind) == "id")
                m["id"] = "../../registry";
            if (std::string(kind) == "size")
                m["files"]["version.txt"]["size"] = -1;
            if (std::string(kind) == "collision") {
                m["files"]["common.txt/nested"] = m["files"]["common.txt"];
                m["file_count"] = 4;
                m["bytes"] = m["bytes"].get<uintmax_t>() + 7;
            }
            atomic_write(manifestPath, m.dump());
            check(!core.request("GET", "/api/storage").at("can_prune").get<bool>(),
                  "Invalid manifest blocks cleanup");
            rejects([&] { action(core, "a", "restore", data); },
                    "Invalid manifest refuses restoration");
            atomic_write(manifestPath, original.dump());
        }

        // A linked catalog must never lead collection outside the private state.
        std::error_code linkError;
        auto link = core.state() / "snapshots/linked";
        fs::create_directories(run / "outside");
        fs::create_directory_symlink(run / "outside", link, linkError);
#ifdef _WIN32
        if (linkError) {
            auto script = run / "create-junction.ps1";
            atomic_write(script, "param([string]$LinkPath, [string]$TargetPath)\n"
                                 "New-Item -ItemType Junction -Path $LinkPath -Value $TargetPath "
                                 "-ErrorAction Stop | Out-Null\n");
            auto result =
                process({"powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                         path_string(script), path_string(link), path_string(run / "outside")});
            if (result.code == 0 && path_is_link(link))
                linkError.clear();
        }
#endif
        if (!linkError) {
            check(!core.request("GET", "/api/storage").at("can_prune").get<bool>(),
                  "Linked catalog blocks cleanup");
            fs::remove(link);
        } else {
            std::cout << "SKIP directory symlink creation unavailable: " << linkError.message()
                      << '\n';
        }

        status = cli(core, {"storage", "prune"});
        auto applied =
            cli(core, {"storage", "prune", "--apply", "true", "--plan", status.at("plan")});
        check(applied.at("applied") &&
                  applied.at("removed_objects") == status.at("unused_objects") &&
                  applied.at("failures").empty(),
              "Explicit current plan removes exactly unused objects");
        check(!fs::exists(core.state() / "objects" / sha256("first\n")) &&
                  fs::exists(core.state() / "objects" / sha256("fourth\n")),
              "Cleanup removes retired bytes and preserves unlisted project references");
        check(read_file(core.state() / "objects/notes.txt") == "preserve unknown bytes" &&
                  read_file(core.state() / "objects/part-interrupted") ==
                      "preserve temporary bytes",
              "Cleanup preserves unknown and temporary files");
        check(action(core, "a", "verify").at("ok") &&
                  core.request("GET", "/api/storage").at("unused_objects") == 0,
              "Every retained current backup remains usable after cleanup");
        auto restored = run / "after-cleanup";
        action(core, "a", "restore", {{"destination", path_string(restored)}});
        check(read_file(restored / "version.txt") == "fifth\n", "Restoration works after cleanup");
        Core otherController(core.state());
        auto writer = std::async(std::launch::async, [&] {
            for (int i = 0; i < 8; ++i) {
                atomic_write(a / "version.txt", "concurrent-" + std::to_string(i));
                action(otherController, "a", "backup");
            }
        });
        for (int i = 0; i < 8; ++i) {
            auto preview = core.request("GET", "/api/storage");
            try {
                core.request("POST", "/api/storage/prune",
                             {{"apply", true}, {"plan", preview.at("plan")}});
            } catch (const std::exception &e) {
                if (std::string(e.what()).find("plan changed") == std::string::npos)
                    throw;
            }
        }
        writer.get();
        auto remaining = core.request("GET", "/api/projects/a/snapshots");
        for (const auto &m : remaining)
            check(action(core, "a", "verify", {{"snapshot", m.at("id")}}).at("ok"),
                  "Concurrent backup and collection preserve every retained snapshot");
        check(fs::exists(core.state() / "objects" / sha256("fourth\n")),
              "Concurrent collection preserves another project's objects");
        std::cout << "PASS " << checks << " backup checks; fixtures: " << path_string(run) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
