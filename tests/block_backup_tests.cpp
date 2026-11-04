#include <cysynck/core.hpp>
#include <functional>
#include <future>
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
static void add(Core &core, const fs::path &path, const std::string &id, int keep = 4) {
    fs::create_directories(path);
    core.request("POST", "/api/projects",
                 {{"id", id},
                  {"name", id},
                  {"mode", "backup"},
                  {"root", path_string(path)},
                  {"backup_keep", keep},
                  {"backup_interval_minutes", 0}});
}
static Json act(Core &core, const std::string &id, const std::string &action,
                const Json &data = Json::object()) {
    return core.request("POST", "/api/projects/" + id + "/" + action, data);
}
static fs::path manifest_path(Core &core, const std::string &project, const Json &snapshot) {
    return core.state() / "snapshots" / project / (snapshot.at("id").get<std::string>() + ".json");
}
static Json manifest(Core &core, const std::string &project, const Json &snapshot) {
    return Json::parse(read_file(manifest_path(core, project, snapshot)));
}
int main() {
    auto run = fs::current_path() / "block-backup-data" / random_id();
    fs::create_directories(run);
    constexpr size_t blockSize = 4 * 1024 * 1024;
    const std::string a(blockSize, '\x81'), b(blockSize, 'b'), tail(97, 't');
    auto original = a + b + tail;
    auto changed = original;
    changed[blockSize + 13] = 'c';
    try {
        Core core(run / "state");
        add(core, run / "a", "a");
        add(core, run / "b", "b", 1);
        add(core, run / "legacy", "legacy", 1);
        rejects([&] { cli(core, {"project", "set", "a", "--storage", "invalid"}); },
                "Storage policy is validated");
        atomic_write(run / "a/asset.bin", original);
        auto first = act(core, "a", "backup");
        auto firstManifest = manifest(core, "a", first);
        auto entry = firstManifest.at("files").at("asset.bin");
        check(firstManifest.at("format") == 2 && entry.at("storage") == "blocks-v1" &&
                  entry.at("blocks").size() == 3,
              "Large file has a versioned block recipe including its tail");
        check(!fs::exists(core.state() / "objects" / sha256(original)),
              "Block snapshot does not retain a full duplicate");
        auto stats = first.at("block_storage");
        check(stats.at("new_block_bytes") == original.size() && stats.at("reused_block_bytes") == 0,
              "First capture reports the actual written blocks");
        atomic_write(run / "a/asset.bin", changed);
        auto second = act(core, "a", "backup");
        auto secondManifest = manifest(core, "a", second);
        stats = second.at("block_storage");
        check(stats.at("new_block_bytes") == blockSize &&
                  stats.at("reused_block_bytes") == blockSize + tail.size(),
              "One byte changed writes one block and reuses the rest");
        atomic_write(run / "b/asset.bin", original);
        auto shared = act(core, "b", "backup");
        check(shared.at("block_storage").at("new_block_bytes") == 0 &&
                  shared.at("block_storage").at("reused_block_bytes") == original.size(),
              "Blocks deduplicate across projects");
        auto storage = cli(core, {"storage", "status"});
        check(storage.at("block_objects") == 4 &&
                  storage.at("block_stored_bytes") == 3 * blockSize + tail.size(),
              "Inventory counts physical blocks separately from logical files");
        check(storage.at("deduplicated_bytes") ==
                  3 * original.size() - (3 * blockSize + tail.size()),
              "Deduplicated size uses referenced physical data");
        check(act(core, "a", "verify").at("ok") && act(core, "b", "verify").at("ok"),
              "Both block versions verify");
        for (auto snapshot : {first, second}) {
            auto destination = run / ("restore-" + snapshot.at("id").get<std::string>());
            auto request =
                Json{{"snapshot", snapshot.at("id")}, {"destination", path_string(destination)}};
            check(act(core, "a", "restore-plan", request).at("ok") && !fs::exists(destination),
                  "Restore preview leaves no output");
            act(core, "a", "restore", request);
            auto expected = snapshot.at("id") == first.at("id") ? original : changed;
            check(read_file(destination / "asset.bin", original.size()) == expected,
                  "Restore assembles the exact historical bytes");
        }
        Core reopened(core.state());
        check(act(reopened, "a", "verify", {{"snapshot", first.at("id")}}).at("ok"),
              "Block recipes survive reopening the profile");
        auto diff = act(core, "a", "diff", {{"snapshot", first.at("id")}, {"path", "asset.bin"}});
        check(!diff.at("patch_available").get<bool>() &&
                  diff.at("before_info").at("bytes") == original.size() &&
                  diff.at("storage") == "blocks-v1",
              "Large block-backed diffs return bounded preview metadata");
        // Whole-file policy and older manifests remain usable in the same store.
        cli(core, {"project", "set", "legacy", "--storage", "whole"});
        atomic_write(run / "legacy/asset.bin", original);
        auto legacy = act(core, "legacy", "backup");
        auto legacyManifest = manifest(core, "legacy", legacy);
        check(!legacyManifest.at("files").at("asset.bin").contains("blocks") &&
                  fs::exists(core.state() / "objects" / sha256(original)),
              "Whole-file policy retains the legacy layout");
        legacyManifest.erase("format");
        legacyManifest.erase("block_storage");
        atomic_write(manifest_path(core, "legacy", legacy), legacyManifest.dump());
        check(act(core, "legacy", "verify").at("ok"),
              "Manifests without a format marker are still read");
        act(core, "legacy", "restore", {{"destination", path_string(run / "legacy-restored")}});
        check(read_file(run / "legacy-restored/asset.bin", original.size()) == original,
              "Legacy restoration remains byte exact");
        auto blockPath =
            core.state() / "blocks" / entry.at("blocks")[0].at("sha256").get<std::string>();
        atomic_write(blockPath, std::string(blockSize, 'x'));
        check(!act(core, "a", "verify").at("ok").get<bool>() &&
                  !act(core, "b", "verify").at("ok").get<bool>(),
              "Same-size corruption is detected for every sharing project");
        auto corruptTarget = run / "never-created/nested";
        rejects([&] { act(core, "a", "restore", {{"destination", path_string(corruptTarget)}}); },
                "Corrupt blocks prevent restoration");
        check(!fs::exists(corruptTarget.parent_path()),
              "Integrity failures do not create destination parents");
        shared = act(core, "b", "backup");
        check(shared.at("block_storage").at("new_block_bytes") == blockSize &&
                  act(core, "a", "verify", {{"snapshot", first.at("id")}}).at("ok"),
              "Fresh capture repairs a shared damaged block");
        auto tailPath =
            core.state() / "blocks" / entry.at("blocks")[2].at("sha256").get<std::string>();
        fs::remove(tailPath);
        check(core.request("GET", "/api/storage").at("missing_objects") == 1 &&
                  !act(core, "a", "verify").at("ok").get<bool>(),
              "Missing block appears in inventory and verification");
        act(core, "b", "backup");
        // All individual blocks are authentic; their order still has to match the file digest.
        auto forged = secondManifest;
        auto &blocks = forged["files"]["asset.bin"]["blocks"];
        std::swap(blocks[0], blocks[1]);
        atomic_write(manifest_path(core, "a", second), forged.dump());
        check(!act(core, "a", "verify", {{"snapshot", second.at("id")}}).at("ok").get<bool>(),
              "Whole-file digest detects an authentic but reordered block list");
        rejects(
            [&] {
                act(core, "a", "restore",
                    {{"snapshot", second.at("id")}, {"destination", path_string(run / "forged")}});
            },
            "Wrong block order cannot publish a restoration");
        atomic_write(manifest_path(core, "a", second), secondManifest.dump());
        for (const std::string kind : {"format", "layout", "digest", "negative", "tail", "count",
                                       "overflow", "implicit", "summary", "legacy-format"}) {
            forged = secondManifest;
            auto &file = forged["files"]["asset.bin"];
            if (kind == "format")
                forged["format"] = 99;
            if (kind == "legacy-format")
                forged["format"] = 1;
            if (kind == "summary")
                forged["block_storage"]["reused_block_bytes"] = -1;
            if (kind == "layout")
                file["storage"] = "future-layout";
            if (kind == "digest")
                file["blocks"][0]["sha256"] = "../../registry.json";
            if (kind == "negative")
                file["blocks"][0]["size"] = -1;
            if (kind == "tail")
                file["blocks"][2]["size"] = 96;
            if (kind == "count")
                file["blocks"].erase(file["blocks"].begin());
            if (kind == "overflow")
                file["size"] = UINT64_MAX;
            if (kind == "implicit")
                file.erase("storage");
            atomic_write(manifest_path(core, "a", second), forged.dump());
            check(!core.request("GET", "/api/storage").at("can_prune").get<bool>(),
                  "Malformed recipe blocks all cleanup");
            rejects(
                [&] {
                    act(core, "a", "restore",
                        {{"snapshot", second.at("id")},
                         {"destination", path_string(run / "invalid")}});
                },
                "Malformed recipe cannot restore");
        }
        atomic_write(manifest_path(core, "a", second), secondManifest.dump());
        // Retention and cleanup protect shared references, even for an unlisted project.
        cli(core, {"project", "set", "a", "--keep", "1"});
        atomic_write(run / "a/asset.bin", original);
        act(core, "a", "backup");
        auto registry = Json::parse(read_file(core.state() / "registry.json"));
        for (auto it = registry["projects"].begin(); it != registry["projects"].end();)
            if (it->at("id") == "b")
                it = registry["projects"].erase(it);
            else
                ++it;
        atomic_write(core.state() / "registry.json", registry.dump());
        atomic_write(core.state() / "blocks/notes.txt", "preserve this");
        atomic_write(core.state() / "blocks/part-interrupted", "preserve partial block");
        auto plan = core.request("GET", "/api/storage");
        check(plan.at("unused_objects") == 1 && plan.at("reclaimable_bytes") == blockSize,
              "Retention leaves exactly the changed block unreferenced");
        atomic_write(core.state() / "blocks" / sha256("orphan"), "orphan");
        rejects(
            [&] {
                core.request("POST", "/api/storage/prune",
                             {{"apply", true}, {"plan", plan.at("plan")}});
            },
            "A new block invalidates the cleanup token");
        plan = core.request("GET", "/api/storage");
        auto result = core.request("POST", "/api/storage/prune",
                                   {{"apply", true}, {"plan", plan.at("plan")}});
        check(result.at("removed_objects") == 2 && result.at("failures").empty(),
              "Explicit cleanup removes only orphaned blocks");
        check(read_file(core.state() / "blocks/notes.txt") == "preserve this" &&
                  read_file(core.state() / "blocks/part-interrupted") == "preserve partial block",
              "Unknown and partial block files are preserved");
        check(act(core, "a", "verify").at("ok") && fs::exists(blockPath) && fs::exists(tailPath),
              "Retained recipes survive cross-project cleanup");
        // Fixed-size blocks still reuse a repeated block within one file.
        add(core, run / "repeated", "repeated", 1);
        atomic_write(run / "repeated/repeated.bin", a + a);
        auto repeated = act(core, "repeated", "backup");
        check(repeated.at("block_storage").at("new_block_bytes") == 0 &&
                  repeated.at("block_storage").at("reused_block_bytes") == 2 * blockSize &&
                  act(core, "repeated", "verify").at("ok"),
              "Repeated block references reconstruct correctly");
        // A larger tree mixes many small legacy objects with the new large-file layout.
        add(core, run / "tree", "tree", 1);
        for (int i = 0; i < 512; ++i)
            atomic_write(run / "tree" / fs::u8path("dossier-é-" + std::to_string(i % 16)) /
                             (std::to_string(i) + ".txt"),
                         "small-asset-" + std::to_string(i));
        atomic_write(run / "tree/large.bin", original);
        auto tree = act(core, "tree", "backup");
        check(tree.at("file_count") == 513 && tree.at("block_storage").at("chunked_files") == 1 &&
                  act(core, "tree", "verify").at("ok"),
              "Mixed tree of 513 files is captured and verified");
        act(core, "tree", "restore", {{"destination", path_string(run / "tree-restored")}});
        bool exact = read_file(run / "tree-restored/large.bin", original.size()) == original;
        for (int i = 0; i < 512; ++i)
            exact &= read_file(run / "tree-restored" /
                               fs::u8path("dossier-é-" + std::to_string(i % 16)) /
                               (std::to_string(i) + ".txt")) == "small-asset-" + std::to_string(i);
        check(exact, "Mixed tree restores every Unicode path and original byte");
        Core concurrent(core.state());
        auto writer = std::async(std::launch::async, [&] {
            for (int i = 0; i < 3; ++i) {
                auto update = original;
                update[1] = static_cast<char>('0' + i);
                atomic_write(run / "a/asset.bin", update);
                act(concurrent, "a", "backup");
            }
        });
        for (int i = 0; i < 4; ++i) {
            auto preview = core.request("GET", "/api/storage");
            try {
                core.request("POST", "/api/storage/prune",
                             {{"apply", true}, {"plan", preview.at("plan")}});
            } catch (const std::exception &error) {
                if (std::string(error.what()).find("plan changed") == std::string::npos)
                    throw;
            }
        }
        writer.get();
        check(act(core, "a", "verify").at("ok") && act(core, "tree", "verify").at("ok") &&
                  fs::exists(blockPath),
              "Concurrent chunk capture and cleanup preserve all published references");
        auto store = core.state() / "blocks", saved = core.state() / "blocks-saved";
        fs::rename(store, saved);
        auto outside = run / "outside-blocks";
        fs::create_directories(outside);
        atomic_write(outside / "keep.txt", "external content");
        std::error_code linkError;
        fs::create_directory_symlink(outside, store, linkError);
#ifdef _WIN32
        if (linkError) {
            auto script = run / "junction.ps1";
            atomic_write(
                script,
                "param([string]$LinkPath, [string]$TargetPath)\nNew-Item -ItemType Junction -Path "
                "$LinkPath -Value $TargetPath -ErrorAction Stop | Out-Null\n");
            auto created =
                process({"powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                         path_string(script), path_string(store), path_string(outside)});
            if (created.code)
                throw std::runtime_error("Cannot create the isolated block-store junction fixture");
        }
#else
        if (linkError)
            throw std::runtime_error("Cannot create the isolated block-store link fixture");
#endif
        check(!core.request("GET", "/api/storage").at("can_prune").get<bool>(),
              "Linked block namespace prevents cleanup");
        rejects([&] { act(core, "a", "backup"); },
                "Capture cannot write through a linked block namespace");
        check(!act(core, "tree", "verify").at("ok").get<bool>() &&
                  read_file(outside / "keep.txt") == "external content",
              "Linked namespace is rejected without touching external files");
        fs::remove(store);
        fs::rename(saved, store);
        check(act(core, "tree", "verify").at("ok"),
              "Original block store remains usable after link review");
        std::cout << "PASS " << checks << " block backup checks; fixtures: " << path_string(run)
                  << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what()
                  << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
