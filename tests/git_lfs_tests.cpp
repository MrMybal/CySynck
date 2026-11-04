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
static Json action(Core &core, const std::string &name, const Json &data = Json::object()) {
    return core.request("POST", "/api/projects/game/" + name, data);
}
static Json setup(Core &core, const fs::path &root, const fs::path &exchange) {
    fs::create_directories(root);
    auto p = core.request("POST", "/api/projects",
                          {{"id", "game"},
                           {"name", "Game"},
                           {"mode", "git-bundles"},
                           {"root", path_string(root)},
                           {"exchange_path", path_string(exchange)},
                           {"backup_interval_minutes", 0}});
    git(p, {"init", "--initial-branch=main"});
    return p;
}
static void commit(const Json &p, const std::string &message) {
    git(p, {"add", "--all"});
    git(p, {"-c", "user.name=LFS Test", "-c", "user.email=lfs@example.invalid", "commit", "-m",
            message});
}
static std::string pointer(const std::string &bytes) {
    return "version https://git-lfs.github.com/spec/v1\noid sha256:" + sha256(bytes) + "\nsize " +
           std::to_string(bytes.size()) + "\n";
}
static fs::path object(const fs::path &store, const std::string &bytes) {
    return lfs_object_path(store, sha256(bytes));
}
static std::string raw_commit(const Json &project, const std::string &bytes) {
    auto clean = [](std::string value) {
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
            value.pop_back();
        return value;
    };
    auto blob = clean(offline_git(project, {"hash-object", "-w", "--stdin"}, bytes));
    auto tree = clean(offline_git(project, {"mktree"}, "100644 blob " + blob + "\tAsset.uasset\n"));
    return clean(offline_git(project,
                             {"-c", "user.name=Pointer Test", "-c",
                              "user.email=pointer@example.invalid", "commit-tree", tree},
                             "Pointer fixture\n"));
}
int main() {
    auto run = fs::current_path() / "git-lfs-test-data" / random_id();
    fs::create_directories(run);
    try {
        Core sender(run / "sender"), receiver(run / "receiver");
        auto exchange = run / "exchange";
        auto a = setup(sender, run / "repo a été", exchange),
             b = setup(receiver, run / "repo b été", exchange);
        auto rootA = fs::u8path(a.at("root").get<std::string>()),
             rootB = fs::u8path(b.at("root").get<std::string>());
        std::string first(2 * 1024 * 1024 + 17, '\x89'), second("binary\0second\xff", 14),
            secret("other-branch-only");
        auto sourceStore = lfs_local_store(a), localStore = lfs_local_store(b);
        atomic_write(rootA / ".gitattributes", "*.uasset filter=lfs diff=lfs merge=lfs -text\n");
        // Populate Git with canonical pointers directly, independent of installed LFS filters.
        atomic_write(object(sourceStore, first), first, true);
        atomic_write(rootA / "asset.uasset", pointer(first));
        commit(a, "First asset");
        auto initial = git(a, {"rev-parse", "HEAD"});
        atomic_write(object(sourceStore, second), second, true);
        atomic_write(rootA / "asset.uasset", pointer(second));
        atomic_write(rootA / "copy.uasset", pointer(second));
        commit(a, "Second asset and duplicate");
        fs::remove(rootA / "asset.uasset");
        fs::remove(rootA / "copy.uasset");
        commit(a, "Deleted assets remain in history");
        git(a, {"checkout", "-b", "private-only"});
        atomic_write(object(sourceStore, secret), secret, true);
        atomic_write(rootA / "private.uasset", pointer(secret));
        commit(a, "Unrelated branch asset");
        git(a, {"checkout", "main"});
        atomic_write(rootB / "local.txt", "local committed\n");
        commit(b, "Local branch");
        atomic_write(rootB / "local.txt", "local dirty\n");
        atomic_write(rootB / "staged.txt", "keep staged\n");
        git(b, {"add", "staged.txt"});
        auto oldHead = git(b, {"rev-parse", "HEAD"}), oldStatus = git(b, {"status", "--porcelain"});
        auto oldIndex = sha256_file(rootB / ".git/index");
        auto inventory = lfs_inventory(a, git(a, {"rev-parse", "HEAD"}));
        auto crlf = raw_commit(a, "version https://git-lfs.github.com/spec/v1\r\noid sha256:" +
                                      sha256(second) + "\r\nsize 14\r\n");
        rejects([&] { lfs_inventory(a, crlf); },
                "Noncanonical CRLF pointers cannot silently omit their payload");
        auto legacy = raw_commit(a, "version https://hawser.github.com/spec/v1\noid sha256:" +
                                        sha256(second) + "\nsize 14\n");
        rejects([&] { lfs_inventory(a, legacy); }, "Legacy pointer versions fail explicitly");
        check(inventory.at("objects").size() == 2 &&
                  inventory.at("bytes") == first.size() + second.size(),
              "History includes deleted versions, deduplicates copies and excludes unrelated "
              "branches");
        auto plain = action(sender, "export", {{"lfs", false}});
        check(plain.at("version") == 1 && !plain.contains("lfs"),
              "Legacy export remains explicit pointer-only transport");
        auto bundle = cli(sender, {"git", "export", "game"});
        check(bundle.at("version") == 2 && bundle.at("lfs") == inventory,
              "CLI includes complete LFS history by default, as automatic synchronization does");
        rejects([&] { cli(sender, {"git", "export", "game", "--lfs", "invalid"}); },
                "CLI rejects invalid LFS export choices");
        auto peer = bundle.at("peer").get<std::string>(), id = bundle.at("id").get<std::string>();
        auto directory = exchange / "peers" / peer;
        auto manifestPath = directory / (id + ".lfs.json");
        auto sharedStore = directory / "lfs/objects";
        check(fs::exists(manifestPath) && !fs::exists(directory / (id + ".json")),
              "Old clients ignore the distinct LFS manifest suffix");
        check(read_file(object(sharedStore, second)) == second,
              "Export preserves binary payload bytes");
        check(!fs::exists(object(sharedStore, secret)),
              "Unrelated branch objects are never exported");
        auto incoming = action(receiver, "inbox");
        check(incoming.size() == 2 && incoming[0].contains("id"),
              "Mixed legacy and LFS bundles are listed");
        fs::remove(object(sharedStore, second));
        incoming = action(receiver, "inbox");
        bool incomplete = false;
        for (const auto &item : incoming)
            if (item.at("id") == id)
                incomplete = !item.at("lfs_status").at("complete").get<bool>() &&
                             item.at("lfs_status").at("missing") == 1;
        check(incomplete, "Inbox distinguishes partially transported LFS data");
        Json selection = {{"peer", peer}, {"bundle", id}};
        rejects([&] { action(receiver, "import", selection); }, "Missing LFS object blocks import");
        atomic_write(object(sharedStore, second), std::string(second.size(), 'x'));
        rejects([&] { action(receiver, "import", selection); },
                "Same-size corrupt transport object blocks import");
        atomic_write(object(sharedStore, second), second);
        auto malformed = bundle;
        malformed["lfs"]["objects"].push_back(malformed["lfs"]["objects"][0]);
        atomic_write(manifestPath, malformed.dump());
        rejects([&] { action(receiver, "import", selection); },
                "Duplicated manifest objects are rejected");
        malformed = bundle;
        malformed["lfs"]["objects"].erase(malformed["lfs"]["objects"].begin());
        malformed["lfs"]["bytes"] = malformed["lfs"]["objects"][0]["size"];
        atomic_write(manifestPath, malformed.dump());
        rejects([&] { action(receiver, "import", selection); },
                "Inventory omissions are detected against bundle history");
        check(git(b, {"for-each-ref", "refs/remotes/cysynck/"}).empty(),
              "Rejected LFS import publishes no tracking ref");
        atomic_write(manifestPath, bundle.dump());
        atomic_write(object(localStore, second), "preserve corrupt existing cache");
        rejects([&] { action(receiver, "import", selection); },
                "Existing corrupt local objects are never overwritten");
        check(read_file(object(localStore, second)) == "preserve corrupt existing cache",
              "Local damaged cache is preserved for review");
        fs::remove(object(localStore, second));
        auto imported = action(receiver, "import", selection);
        check(imported.at("lfs").at("verified") && imported.at("lfs").at("objects") == 2,
              "Import verifies and installs all LFS history");
        check(sha256_file(object(localStore, first)) == sha256(first) &&
                  read_file(object(localStore, second)) == second,
              "Local LFS cache has exact exported assets");
        check(git(b, {"rev-parse", "HEAD"}) == oldHead &&
                  git(b, {"status", "--porcelain"}) == oldStatus &&
                  sha256_file(rootB / ".git/index") == oldIndex &&
                  read_file(rootB / "local.txt") == "local dirty\n",
              "Import preserves HEAD, working tree and staged index exactly");
        auto tracking = imported.at("tracking_ref").get<std::string>();
        check(git(b, {"rev-parse", tracking}) == bundle.at("head").get<std::string>(),
              "Tracking ref is published after asset verification");
        check(action(receiver, "import", selection).at("lfs").at("verified"),
              "Repeated import reuses verified content");
        malformed = bundle;
        malformed["version"] = 3;
        atomic_write(manifestPath, malformed.dump());
        rejects([&] { action(receiver, "import", selection); },
                "Future exchange versions fail closed");
        malformed = bundle;
        malformed["lfs"]["objects"][0]["sha256"] = "../escape";
        atomic_write(manifestPath, malformed.dump());
        rejects([&] { action(receiver, "import", selection); },
                "Manifest object paths cannot escape the content store");
        atomic_write(manifestPath, bundle.dump());
        auto before = action(sender, "inbox").size();
        fs::remove(object(sourceStore, first));
        rejects([&] { action(sender, "export", {{"lfs", true}}); },
                "Missing historical object prevents publication");
        check(action(sender, "inbox").size() == before, "Failed export publishes no manifest");
        atomic_write(object(sourceStore, first), first, true);
        git(a, {"config", "lfs.storage", path_string(run / "custom-store")});
        rejects([&] { action(sender, "export", {{"lfs", true}}); },
                "Custom LFS storage is refused explicitly");
        git(a, {"config", "--unset", "lfs.storage"});
        atomic_write(object(sharedStore, second), std::string(second.size(), 'z'));
        rejects([&] { action(sender, "export", {{"lfs", true}}); },
                "Corrupt existing exchange object cannot be silently replaced");
        check(read_file(object(sharedStore, second)) == std::string(second.size(), 'z'),
              "Damaged shared object is preserved for review");
        atomic_write(object(sharedStore, second), second);
        auto savedStore = sourceStore.parent_path() / "objects.saved";
        fs::rename(sourceStore, savedStore);
        auto outside = run / "outside-store";
        fs::create_directories(outside);
        atomic_write(outside / "keep.txt", "preserve outside");
        std::error_code linkError;
        fs::create_directory_symlink(outside, sourceStore, linkError);
#ifdef _WIN32
        if (linkError) {
            auto script = run / "junction.ps1";
            atomic_write(
                script, "param([string]$LinkPath,[string]$TargetPath)\nNew-Item -ItemType Junction "
                        "-Path $LinkPath -Value $TargetPath -ErrorAction Stop | Out-Null\n");
            auto created =
                process({"powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                         path_string(script), path_string(sourceStore), path_string(outside)});
            if (created.code)
                throw std::runtime_error("Cannot create isolated LFS store junction fixture");
        }
#else
        if (linkError)
            throw std::runtime_error("Cannot create isolated LFS store link fixture");
#endif
        rejects([&] { action(sender, "export", {{"lfs", true}}); },
                "Linked LFS store cannot be exported");
        check(read_file(outside / "keep.txt") == "preserve outside",
              "Linked store rejection preserves outside content");
        fs::remove(sourceStore);
        fs::rename(savedStore, sourceStore);
        auto badSize = inventory;
        badSize["objects"][0]["size"] = uint64_t(-1);
        rejects([&] { validate_lfs_manifest(badSize); }, "Manifest integer overflow is rejected");
        badSize = inventory;
        badSize["bytes"] = 1;
        rejects([&] { validate_lfs_manifest(badSize); }, "Inventory total must match its objects");
        badSize = inventory;
        badSize["objects"] = Json::array();
        for (char c = 'a'; c <= 'e'; ++c)
            badSize["objects"].push_back(
                {{"sha256", std::string(64, c)}, {"size", 4ull * 1024 * 1024 * 1024}});
        badSize["bytes"] = 20ull * 1024 * 1024 * 1024;
        rejects([&] { validate_lfs_manifest(badSize); },
                "Total LFS payload bound is enforced before object access");
        atomic_write(rootA / "extended.uasset",
                     "version https://git-lfs.github.com/spec/v1\next-0-test sha256:" +
                         sha256(second) + "\noid sha256:" + sha256(second) + "\nsize 14\n");
        commit(a, "Unsupported pointer extension");
        rejects([&] { action(sender, "export", {{"lfs", true}}); },
                "Pointer extensions fail explicitly");
        // Git worktrees share the common LFS object directory.
        git(b, {"worktree", "add", "--detach", path_string(run / "linked worktree"), initial});
        auto worktree = b;
        worktree["root"] = path_string(run / "linked worktree");
        check(lfs_local_store(worktree) == localStore,
              "Linked Git worktree resolves the shared LFS store");
        auto probe = process({"git", "lfs", "version"});
        if (probe.code == 0) {
            git(worktree, {"lfs", "install", "--local", "--skip-smudge", "--skip-repo"});
            git(worktree, {"lfs", "checkout"});
            check(sha256_file(run / "linked worktree/asset.uasset") == sha256(first),
                  "Official Git LFS hydrates imported historical assets without a server");
            std::cout << "PASS official Git LFS checkout interoperability\n";
        } else
            std::cout << "SKIP optional Git LFS CLI hydration (tool not installed)\n";
        std::cout << "PASS " << checks << " Git LFS checks; fixtures: " << path_string(run) << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what()
                  << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
