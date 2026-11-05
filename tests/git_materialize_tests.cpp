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
static std::string trim(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
        value.pop_back();
    return value;
}
static Json hashes(const fs::path &root) {
    Json result = Json::object();
    for (const auto &e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file())
            result[path_string(e.path().lexically_relative(root))] = sha256_file(e.path());
    return result;
}
static std::string pointer(const std::string &bytes) {
    return "version https://git-lfs.github.com/spec/v1\noid sha256:" + sha256(bytes) + "\nsize " +
           std::to_string(bytes.size()) + "\n";
}
int main() {
    auto run = fs::current_path() / "git-materialize-data" / random_id();
    fs::create_directories(run);
    try {
        for (auto format : {"sha1", "sha256"}) {
            auto work = run / format, root = work / fs::u8path("repo été");
            fs::create_directories(root);
            Core core(work / "profile");
            auto p = core.request("POST", "/api/projects",
                                  {{"id", "team"},
                                   {"name", "LFS assets"},
                                   {"root", path_string(root)},
                                   {"mode", "git-bundles"},
                                   {"git_lfs", true},
                                   {"exchange_path", path_string(work / "exchange")},
                                   {"backup_interval_minutes", 0}});
            auto setup = cli(core, {"git", "setup", "team", "--object-format", format});
            cli(core, {"git", "setup", "team", "--object-format", format, "--apply", "true",
                       "--plan", setup.at("plan")});
            git(p, {"config", "core.autocrlf", "false"});
            git(p, {"config", "user.name", "LFS Materialize"});
            git(p, {"config", "user.email", "lfs@example.invalid"});
            git(p, {"config", "core.attributesFile",
#ifdef _WIN32
                    "NUL"
#else
                    "/dev/null"
#endif
                   });
            std::string bytes(2 * 1024 * 1024 + 31, '\0');
            for (size_t i = 0; i < bytes.size(); ++i)
                bytes[i] = static_cast<char>((i * 37 + i / 257) % 256);
            auto text = pointer(bytes), empty = pointer("");
            atomic_write(root / ".gitattributes", "*.uasset filter=lfs diff=lfs merge=lfs -text\n");
            for (auto name : {"a.uasset", "modified.uasset", "missing.uasset", "staged.uasset",
                              "flag.uasset", "readonly.uasset", "already.uasset", "pointer.txt"})
                atomic_write(root / name, text);
            atomic_write(root / fs::u8path("dossier/été.uasset"), text);
            atomic_write(root / "empty.uasset", empty);
            atomic_write(root / "ordinary.txt", "ordinary committed\n");
            git(p, {"-c", "filter.lfs.process=", "-c", "filter.lfs.clean=", "-c",
                    "filter.lfs.smudge=", "-c", "filter.lfs.required=false", "add", "-A"});
            git(p, {"-c", "commit.gpgsign=false", "-c",
                    "core.hooksPath="
#ifdef _WIN32
                    "NUL",
#else
                    "/dev/null",
#endif
                    "commit", "-m", "LFS pointers"});
            atomic_write(root / "modified.uasset", "local asset changes\n");
            fs::remove(root / "missing.uasset");
            atomic_write(root / "already.uasset", bytes);
            atomic_write(root / "untracked.txt", "local untracked\n");
            atomic_write(root / "ordinary.txt", "ordinary local edits\n");
            auto staged_id = trim(offline_git(p, {"hash-object", "-w", "--no-filters", "--stdin"},
                                              "staged user edit\n"));
            git(p, {"update-index", "--cacheinfo", "100644," + staged_id + ",staged.uasset"});
            git(p, {"update-index", "--assume-unchanged", "flag.uasset"});
            fs::permissions(root / "readonly.uasset", fs::perms::owner_read);
            git(p, {"config", "filter.lfs.process", "cysynck-filter-must-not-run"});
            git(p, {"config", "filter.lfs.clean", "cysynck-filter-must-not-run"});
            auto request = [&](const Json &q) {
                return core.request("POST", "/api/projects/team/git-materialize", q);
            };
            auto baseline = hashes(root);
            auto blocked = request(Json::object());
            check(!blocked.at("can_materialize").get<bool>() && !blocked.at("errors").empty() &&
                      blocked.at("pending") == 3,
                  "Missing cache blocks publication without executing installed filters");
            check(hashes(root) == baseline && !fs::exists(root / ".cysynck-lfs"),
                  "Inspection preserves all Git and working-tree bytes");
            auto source = work / "asset-source";
            atomic_write(source, bytes);
            auto store = lfs_local_store(p);
            auto asset = git_lfs_pointer(text);
            copy_lfs_object(source, store, asset);
            atomic_write(work / "empty-source", "");
            copy_lfs_object(work / "empty-source", store, git_lfs_pointer(empty));
            auto ready = request(Json::object());
            check(ready.at("can_materialize") && ready.at("pending") == 3 &&
                      ready.at("materialized") == 1 && ready.at("skipped") == 6,
                  "Only exact committed pointers are selected; materialized, missing, modified, "
                  "staged and flagged paths are preserved");
            std::map<std::string, std::string> states;
            for (const auto &file : ready.at("files"))
                states[file.at("path")] = file.at("state");
            check(states.at("readonly.uasset") == "read_only" &&
                      states.at("pointer.txt") == "attributes" &&
                      states.at("staged.uasset") == "staged" &&
                      states.at("flag.uasset") == "index_flags",
                  "Read-only files, absent LFS attributes and special index flags are explicit "
                  "skips");
            check(cli(core, {"git", "materialize", "team"}).at("plan") == ready.at("plan") &&
                      request(Json::object()).at("plan") == ready.at("plan"),
                  "CLI and API share a stable materialization plan");
            rejects([&] { request({{"apply", true}}); }, "Application without a plan accepted");
            atomic_write(root / "a.uasset", "changed after preview\n");
            rejects([&] { request({{"apply", true}, {"plan", ready.at("plan")}}); },
                    "Stale pointer preview accepted");
            check(read_file(root / "a.uasset") == "changed after preview\n" &&
                      !fs::exists(root / ".cysynck-lfs"),
                  "Stale application preserves changed local bytes");
            atomic_write(root / "a.uasset", text);
            auto cache_path = lfs_object_path(store, asset.at("sha256"));
            atomic_write(cache_path, std::string(bytes.size(), 'x'), true);
            check(!request(Json::object()).at("can_materialize").get<bool>(),
                  "Same-size corrupted cache object is refused");
            atomic_write(cache_path, bytes, true);
            fs::create_directory(root / ".CySynck-LFS");
            rejects([&] { request(Json::object()); }, "Recovery namespace case alias accepted");
            fs::remove(root / ".CySynck-LFS");
            baseline = hashes(root);
            ready = request(Json::object());
            auto index = sha256_file(root / ".git/index"),
                 head = trim(offline_git(p, {"rev-parse", "HEAD"}));
            fs::create_directory(root / ".git/cysynck-integration.lock");
            rejects([&] { request({{"apply", true}, {"plan", ready.at("plan")}}); },
                    "Existing integration lock was ignored");
            fs::remove(root / ".git/cysynck-integration.lock");
            auto result = request({{"apply", true}, {"plan", ready.at("plan")}});
            check(result.at("applied") && result.at("status") == "complete" &&
                      result.at("files_written") == 3,
                  "Three verified assets are materialized explicitly");
            check(read_file(root / "a.uasset", bytes.size() + 1) == bytes &&
                      read_file(root / fs::u8path("dossier/été.uasset"), bytes.size() + 1) ==
                          bytes &&
                      fs::file_size(root / "empty.uasset") == 0,
                  "Shared binary assets and the empty asset have exact bytes");
            check(sha256_file(root / ".git/index") == index &&
                      trim(offline_git(p, {"rev-parse", "HEAD"})) == head,
                  "HEAD and the index, including staged user work, remain unchanged");
            auto after = hashes(root);
            for (auto it = baseline.begin(); it != baseline.end(); ++it)
                if (it.key() != "a.uasset" && it.key() != "dossier/été.uasset" &&
                    it.key() != "empty.uasset")
                    check(after.at(it.key()) == it.value(),
                          "An unrelated or Git metadata file was changed");
            auto recovery = root / fs::u8path(result.at("recovery_directory").get<std::string>());
            check(read_file(recovery / "before/a.uasset") == text &&
                      read_file(recovery / "before/empty.uasset") == empty,
                  "Original pointer files remain available in the private recovery directory");
            auto saved = Json::parse(read_file(
                fs::u8path(result.at("receipt_path").get<std::string>()), 8 * 1024 * 1024));
            check(saved == result && saved.at("index_hash").is_string(),
                  "Private receipt records the exact operation and preserved index inventory");
            check(!request(Json::object()).at("can_materialize").get<bool>() &&
                      request(Json::object()).at("pending") == 0,
                  "Repeated inspection never rehydrates already materialized files");
            rejects([&] { request({{"apply", true}, {"plan", ready.at("plan")}}); },
                    "A completed plan was reused");
            fs::permissions(root / "readonly.uasset",
                            fs::perms::owner_read | fs::perms::owner_write);
        }
        std::cout << "PASS " << checks << " LFS materialization checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\nFixture: " << path_string(run) << '\n';
        return 1;
    }
}
