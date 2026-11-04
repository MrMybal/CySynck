#include <cysynck/core.hpp>
#include "git_lfs.hpp"
#include <functional>
#include <future>
#include <iostream>
using namespace cysynck;
static int checks = 0;
static void check(bool ok, const char *message) {
    if (!ok)
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
    auto run = fs::current_path() / "git-storage-test-data" / random_id();
    fs::create_directories(run);
    try {
        Core source(run / "source"), target(run / "target");
        auto exchange = run / "exchange", root = run / "repo";
        fs::create_directories(root);
        Json settings = {{"id", "team"},
                         {"name", "Team"},
                         {"root", path_string(root)},
                         {"exchange_path", path_string(exchange)},
                         {"mode", "git-bundles"},
                         {"backup_interval_minutes", 0}};
        auto p = source.request("POST", "/api/projects", settings);
        auto storage = [&](const Json &data) {
            return source.request("POST", "/api/projects/team/git-storage", data);
        };
        auto empty = storage(Json::object());
        check(empty.at("can_prune") && empty.at("exports").empty() &&
                  !fs::exists(exchange / "peers"),
              "Empty preview does not create the peer namespace");
        git(p, {"init", "--initial-branch=main"});
        auto asset = std::string("shared asset\n"), digest = sha256(asset);
        auto local = lfs_object_path(lfs_local_store(p), digest);
        fs::create_directories(local.parent_path());
        atomic_write(local, asset);
        atomic_write(root / "Asset.uasset",
                     "version https://git-lfs.github.com/spec/v1\noid sha256:" + digest +
                         "\nsize " + std::to_string(asset.size()) + "\n");
        git(p, {"add", "--all"});
        git(p, {"-c", "user.name=Storage", "-c", "user.email=storage@example.invalid", "commit",
                "-m", "Asset"});
        auto first = cli(source, {"git", "export", "team"});
        auto second = cli(source, {"git", "export", "team"});
        auto legacy = cli(source, {"git", "export", "team", "--lfs", "false"});
        auto peer = first.at("peer").get<std::string>(), id = first.at("id").get<std::string>();
        auto owned = exchange / "peers" / peer;
        auto objects = owned / "lfs/objects";
        auto shared = lfs_object_path(objects, digest);
        auto orphanDigest = sha256("orphan");
        auto orphan = lfs_object_path(objects, orphanDigest);
        fs::create_directories(orphan.parent_path());
        atomic_write(orphan, "orphan");
        atomic_write(owned / "unindexed.bundle", "preserve");
        atomic_write(owned / "interrupted.part", "preserve");
        atomic_write(objects / "unknown.tmp", "preserve");
        auto other = exchange / "peers/other/lfs/objects";
        fs::create_directories(other);
        atomic_write(other / "keep", "other peer");
        auto preview = cli(source, {"git", "storage", "team", "--exports", id});
        check(preview.at("exports").size() == 3 && preview.at("retained_objects") == 1 &&
                  preview.at("unused_objects") == 1,
              "Retained exports protect shared LFS objects across mixed manifest versions");
        check(preview.at("reclaimable_bytes").get<uintmax_t>() > 6 &&
                  preview.at("ignored_entries").size() == 3,
              "Preview counts selected bundles and preserves unknown or unindexed files");
        rejects([&] { storage({{"exports", {id}}, {"apply", true}}); },
                "Cleanup needs a reviewed token");
        auto newExport = cli(source, {"git", "export", "team"});
        rejects(
            [&] { storage({{"exports", {id}}, {"apply", true}, {"plan", preview.at("plan")}}); },
            "A new export invalidates the reviewed plan");
        check(fs::exists(owned / (id + ".lfs.json")) && fs::exists(orphan),
              "Stale cleanup does not delete anything");
        preview = storage({{"exports", {id}}});
        auto applied = cli(source, {"git", "storage", "team", "--exports", id, "--apply", "true",
                                    "--plan", preview.at("plan")});
        check(applied.at("applied") && applied.at("failures").empty() &&
                  applied.at("removed_exports") == 1 && applied.at("removed_objects") == 1,
              "Reviewed cleanup removes exactly the selected export and orphan object");
        check(!fs::exists(owned / (id + ".lfs.json")) && !fs::exists(owned / (id + ".bundle")) &&
                  !fs::exists(orphan),
              "Selected export is unpublished and its bundle removed");
        check(sha256_file(shared) == digest && sha256_file(local) == digest &&
                  read_file(other / "keep") == "other peer",
              "Shared objects, local cache and other peers remain byte-identical");
        check(read_file(owned / "unindexed.bundle") == "preserve" &&
                  read_file(objects / "unknown.tmp") == "preserve",
              "Unindexed data is never pruned");
        fs::create_directories(run / "target-repo");
        settings["root"] = path_string(run / "target-repo");
        auto t = target.request("POST", "/api/projects", settings);
        git(t, {"init", "--initial-branch=main"});
        auto imported = cli(target, {"git", "import", "team", peer, second.at("id")});
        check(imported.at("lfs").at("verified") &&
                  sha256_file(lfs_object_path(lfs_local_store(t), digest)) == digest,
              "Retained export still imports with verified LFS bytes after cleanup");
        rejects([&] { storage({{"exports", {"../escape"}}}); },
                "Selection cannot escape the peer namespace");
        rejects([&] { storage({{"exports", {id, id}}}); }, "Duplicate export selection is refused");
        rejects([&] { storage({{"exports", {"missing"}}, {"apply", true}, {"plan", "bad"}}); },
                "Missing selected exports disable cleanup");
        rejects([&] { cli(source, {"git", "storage", "team", "--apply", "yes"}); },
                "CLI rejects invalid boolean options");
        auto retainedName = second.at("id").get<std::string>() + ".lfs.json";
        auto retainedBytes = read_file(owned / retainedName);
        auto stable = storage(Json::object());
        atomic_write(owned / retainedName, retainedBytes + "\n");
        rejects([&] { storage({{"apply", true}, {"plan", stable.at("plan")}}); },
                "Even metadata formatting changes invalidate tokens");
        atomic_write(owned / retainedName, retainedBytes);
        auto future = Json::parse(retainedBytes);
        future["version"] = 3;
        atomic_write(owned / retainedName, future.dump());
        check(!storage(Json::object()).at("can_prune").get<bool>(),
              "Future manifests block cleanup");
        atomic_write(owned / retainedName, retainedBytes);
        auto fresh = storage(Json::object());
        auto newDigest = sha256("new orphan");
        auto newObject = lfs_object_path(objects, newDigest);
        fs::create_directories(newObject.parent_path());
        atomic_write(newObject, "new orphan");
        rejects([&] { storage({{"apply", true}, {"plan", fresh.at("plan")}}); },
                "New objects invalidate cleanup plans");
        fs::remove(newObject);
        auto savedObjects = owned / "lfs/saved-objects";
        fs::rename(objects, savedObjects);
        auto outside = run / "outside";
        fs::create_directories(outside);
        atomic_write(outside / "keep", "external data");
        std::error_code linkError;
        fs::create_directory_symlink(outside, objects, linkError);
#ifdef _WIN32
        if (linkError) {
            auto script = run / "junction.ps1";
            atomic_write(
                script,
                "param([string]$LinkPath, [string]$TargetPath)\nNew-Item -ItemType Junction -Path "
                "$LinkPath -Value $TargetPath -ErrorAction Stop | Out-Null\n");
            if (process({"powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                         path_string(script), path_string(objects), path_string(outside)})
                    .code)
                throw std::runtime_error("Cannot create isolated storage junction fixture");
        }
#else
        if (linkError)
            throw std::runtime_error("Cannot create isolated storage link fixture");
#endif
        check(!storage(Json::object()).at("can_prune").get<bool>() &&
                  read_file(outside / "keep") == "external data",
              "Linked LFS namespace blocks cleanup without touching its target");
        fs::remove(objects);
        fs::rename(savedObjects, objects);
        auto lease = std::make_unique<DiskLock>(owned, ".cysynck-git.lock");
        auto exporting = std::async(std::launch::async, [&] {
            return cli(source, {"git", "export", "team", "--lfs", "false"});
        });
        bool blocked =
            exporting.wait_for(std::chrono::milliseconds(150)) == std::future_status::timeout;
        lease.reset();
        auto concurrent = exporting.get();
        check(blocked && concurrent.at("version") == 1,
              "Export publication obeys the shared local peer lock");
        auto removeConcurrent = storage({{"exports", {concurrent.at("id")}}});
        storage({{"exports", {concurrent.at("id")}},
                 {"apply", true},
                 {"plan", removeConcurrent.at("plan")}});
        fs::rename(shared, shared.parent_path() / "saved.tmp");
        check(!storage(Json::object()).at("can_prune").get<bool>(),
              "Missing retained LFS objects disable pruning");
        fs::rename(shared.parent_path() / "saved.tmp", shared);
        atomic_write(owned / "broken.json", "{}");
        auto invalid = storage(Json::object());
        check(!invalid.at("can_prune").get<bool>() && !invalid.at("errors").empty(),
              "Malformed manifests disable pruning");
        rejects([&] { storage({{"apply", true}, {"plan", invalid.at("plan")}}); },
                "Malformed metadata cannot authorize cleanup");
        fs::remove(owned / "broken.json");
        auto corrupt = second.at("id").get<std::string>() + ".bundle";
        auto bundleBytes = read_file(owned / corrupt);
        atomic_write(owned / corrupt, "corrupt");
        check(!storage({{"exports", {second.at("id")}}}).at("can_prune").get<bool>(),
              "A selected corrupt bundle is preserved for manual review");
        atomic_write(owned / corrupt, bundleBytes);
        auto all = storage({{"exports", {second.at("id"), newExport.at("id"), legacy.at("id")}}});
        check(all.at("unused_objects") == 1 && all.at("retained_objects") == 0,
              "Removing all exports releases the previously shared object");
        auto done = storage({{"exports", {second.at("id"), newExport.at("id"), legacy.at("id")}},
                             {"apply", true},
                             {"plan", all.at("plan")}});
        check(done.at("removed_exports") == 3 && done.at("removed_objects") == 1 &&
                  done.at("failures").empty(),
              "Mixed legacy and LFS exports can be removed together");
        check(fs::exists(local) && storage(Json::object()).at("exports").empty(),
              "Local Git LFS cache remains after all exchange exports are removed");
        std::cout << checks << " Git storage checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\nFixture: " << path_string(run) << "\n";
        return 1;
    }
}
