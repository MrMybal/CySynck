#include <cysynck/core.hpp>
#include "git_lfs.hpp"
#include <chrono>
#include <functional>
#include <iostream>
using namespace cysynck;
static int checks = 0;
static std::string trim(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
        value.pop_back();
    return value;
}
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
static Json project(Core &core, const fs::path &root, const fs::path &exchange,
                    const std::string &format) {
    fs::create_directories(root);
    auto p = core.request("POST", "/api/projects",
                          {{"id", "team"},
                           {"name", "Team"},
                           {"root", path_string(root)},
                           {"exchange_path", path_string(exchange)},
                           {"mode", "git-bundles"},
                           {"git_lfs", false},
                           {"backup_interval_minutes", 0}});
    auto plan = cli(core, {"git", "setup", "team", "--object-format", format});
    cli(core, {"git", "setup", "team", "--object-format", format, "--apply", "true", "--plan",
               plan.at("plan")});
    return p;
}
static std::string commit(const Json &p, const std::string &message) {
    git(p, {"add", "-A"});
    git(p, {"-c", "user.name=Integration", "-c", "user.email=test@example.invalid", "-c",
            "commit.gpgsign=false", "commit", "-m", message});
    return git(p, {"rev-parse", "HEAD"});
}
int main() {
    auto run = fs::current_path() / "git-integrate-test-data" / random_id();
    fs::create_directories(run);
    try {
        for (auto format : {"sha1", "sha256"}) {
            auto work = run / format;
            Core source(work / "source"), target(work / "target");
            auto a = project(source, work / fs::u8path("source été"), work / "exchange", format);
            auto b = project(target, work / fs::u8path("target été"), work / "exchange", format);
            auto root = fs::u8path(a.at("root").get<std::string>()),
                 receiver = fs::u8path(b.at("root").get<std::string>());
            atomic_write(root / "first.txt", "first\n");
            atomic_write(root / fs::u8path("dir/été [x].bin"), std::string("\0bin\xff", 5));
            auto first = commit(a, "First");
            auto exported = cli(source, {"git", "export", "team", "--lfs", "false"});
            auto peer = exported.at("peer").get<std::string>();
            cli(target, {"git", "import", "team", peer, exported.at("id")});
            auto query = Json{{"peer", peer}, {"branch", "main"}};
            auto request = [&](Json q) {
                return target.request("POST", "/api/projects/team/git-integrate", q);
            };
            auto preview = request(query);
            check(preview.at("can_integrate") && preview.at("base").is_null() &&
                      preview.at("file_count") == 2 && !fs::exists(receiver / "first.txt"),
                  "First-import preview preserves the empty working tree");
            rejects(
                [&] {
                    auto q = query;
                    q["apply"] = true;
                    request(q);
                },
                "Integration requires a preview token");
            rejects(
                [&] { cli(target, {"git", "integrate", "team", peer, "main", "--apply", "yes"}); },
                "CLI rejects invalid booleans");
            auto apply = [&](const Json &plan) {
                auto q = query;
                q.update({{"apply", true}, {"plan", plan.at("plan")}});
                return request(q);
            };
            atomic_write(receiver / "keep.txt", "keep\n");
            check(request(query).at("can_integrate").get<bool>(),
                  "Unrelated untracked files permit first integration");
            rejects([&] { apply(preview); },
                    "A local file added after preview invalidates application");
            check(read_file(receiver / "keep.txt") == "keep\n" &&
                      !fs::exists(receiver / "first.txt"),
                  "Blocked integration preserves every local file");
            auto withLocal = request(query);
            check(withLocal.at("local_data").at("files") == 1 &&
                      withLocal.at("local_data").at("bytes") == 5,
                  "Preview reports local data without including it in the imported tree");
            atomic_write(receiver / "keep.txt", "changed local content\n");
            rejects([&] { apply(withLocal); },
                    "Changed local metadata invalidates the reviewed inventory");
            atomic_write(receiver / "keep.txt", "keep\n");
            auto sameSize = request(query);
            auto oldTime = fs::last_write_time(receiver / "keep.txt");
            atomic_write(receiver / "keep.txt", "edit\n");
            fs::last_write_time(receiver / "keep.txt", oldTime + std::chrono::seconds(2));
            rejects([&] { apply(sameSize); },
                    "A same-size local edit with a new timestamp invalidates the preview");
            atomic_write(receiver / "keep.txt", "keep\n");
            atomic_write(receiver / "dir", "local file blocks incoming directory\n");
            check(!request(query).at("can_integrate").get<bool>(),
                  "A local file cannot replace an incoming parent directory");
            fs::remove(receiver / "dir");
            fs::create_directories(receiver / "first.txt");
            check(!request(query).at("can_integrate").get<bool>(),
                  "An empty local directory cannot be replaced by an imported file");
            fs::remove(receiver / "first.txt");
            atomic_write(receiver / ".gitattributes", "* text\n");
            check(!request(query).at("can_integrate").get<bool>(),
                  "Untracked attributes cannot change the meaning of checkout");
            fs::remove(receiver / ".gitattributes");
            atomic_write(receiver / "First.txt", "case collision\n");
            check(!request(query).at("can_integrate").get<bool>(),
                  "Local files cannot alias incoming names by case");
            fs::remove(receiver / "First.txt");
            git(b, {"config", "core.fsmonitor", "command-that-must-not-run"});
            auto hook = receiver / ".git/hooks/post-merge";
            atomic_write(hook, "#!/bin/sh\necho ran > hook-ran\n");
            fs::permissions(hook, fs::perms::owner_all);
            git(b, {"config", "core.hooksPath", path_string(hook.parent_path())});
            git(b, {"config", "filter.spy.process", "command-that-must-not-run"});
            git(b, {"config", "filter.spy.required", "true"});
            preview = request(query);
            check(preview.at("can_integrate"),
                  "Configured hooks, fsmonitor and unused filters are disabled during inspection");
            git(b, {"config", "integration.changed", "yes"});
            rejects([&] { apply(preview); },
                    "Configuration changes invalidate a reviewed integration");
            preview = request(query);
            auto done = cli(target, {"git", "integrate", "team", peer, "main", "--apply", "true",
                                     "--plan", preview.at("plan"), "--base", "unborn", "--head",
                                     first, "--local-ref", "refs/heads/main"});
            check(done.at("applied") && done.at("status") == "complete" &&
                      git(b, {"rev-parse", "HEAD"}) == first,
                  "First integration advances the local branch to the exact imported commit");
            check(read_file(receiver / "first.txt") == "first\n" &&
                      read_file(receiver / fs::u8path("dir/été [x].bin")) ==
                          std::string("\0bin\xff", 5),
                  "First integration preserves binary bytes and Unicode paths");
            check(read_file(receiver / "keep.txt") == "keep\n" &&
                      done.at("local_data").at("files") == 1,
                  "First integration preserves local data and records its inventory");
            check(Json::parse(read_file(fs::u8path(done.at("receipt_path").get<std::string>())))
                              .at("status") == "complete" &&
                      !fs::exists(receiver / "hook-ran"),
                  "Private receipt records completion and repository hooks never run");
            check(!request(query).at("can_integrate").get<bool>(),
                  "Already-integrated branch does not apply again");
            rejects([&] { apply(preview); }, "Consumed first-import preview becomes stale");
            atomic_write(root / "first.txt", "second\n");
            fs::remove(root / fs::u8path("dir/été [x].bin"));
            atomic_write(root / "new.bin", std::string(9 * 1024 * 1024 + 7, 'z'));
            auto second = commit(a, "Second");
            auto newer = cli(source, {"git", "export", "team", "--lfs", "false"});
            cli(target, {"git", "import", "team", peer, newer.at("id")});
            preview = request(query);
            check(preview.at("can_integrate") && preview.at("relation") == "fast_forward" &&
                      preview.at("base") == first,
                  "Descendant branch previews a fast-forward");
            auto unchangedIndex = sha256_file(receiver / ".git/index");
            request(query);
            check(sha256_file(receiver / ".git/index") == unchangedIndex,
                  "Preview does not refresh or rewrite the index");
            atomic_write(receiver / "first.txt", "local\n");
            check(!request(query).at("can_integrate").get<bool>(),
                  "Modified tracked files block integration");
            rejects([&] { apply(preview); }, "Modified bytes invalidate an already reviewed plan");
            check(git(b, {"rev-parse", "HEAD"}) == first &&
                      read_file(receiver / "first.txt") == "local\n",
                  "Refused application preserves HEAD and unstaged work");
            git(b, {"update-index", "--assume-unchanged", "first.txt"});
            check(!request(query).at("can_integrate").get<bool>(),
                  "Assume-unchanged cannot conceal modified files");
            git(b, {"update-index", "--no-assume-unchanged", "first.txt"});
            atomic_write(receiver / "first.txt", "first\n");
            atomic_write(receiver / "staged.txt", "staged\n");
            git(b, {"add", "staged.txt"});
            check(!request(query).at("can_integrate").get<bool>(),
                  "Staged additions block integration");
            git(b, {"reset", "--", "staged.txt"});
            fs::remove(receiver / "staged.txt");
            atomic_write(receiver / ".git/info/exclude", "new.bin\n");
            atomic_write(receiver / "new.bin", "ignored local payload\n");
            check(!request(query).at("can_integrate").get<bool>(),
                  "Ignored local payloads block checkout");
            rejects([&] { apply(preview); }, "An ignored path added after preview is preserved");
            check(read_file(receiver / "new.bin") == "ignored local payload\n",
                  "Ignored payload is never overwritten");
            fs::remove(receiver / "new.bin");
            fs::create_directories(receiver / "new.bin");
            atomic_write(receiver / "new.bin/keep.bin", "nested local payload\n");
            check(!request(query).at("can_integrate").get<bool>(),
                  "Local descendants prevent an imported file from replacing their directory");
            fs::remove(receiver / "new.bin/keep.bin");
            fs::remove(receiver / "new.bin");
            atomic_write(receiver / "Build/cache.bin", std::string(9 * 1024 * 1024 + 17, 'k'));
            atomic_write(receiver / "dir/local.txt", "local file in a tracked directory\n");
            fs::create_directories(receiver / "Scratch/empty");
            fs::create_directories(receiver / "dir/empty");
            atomic_write(receiver / ".git/info/exclude", "new.bin\nBuild/\n");
            auto localDigest = sha256_file(receiver / "Build/cache.bin");
            auto localModified = fs::last_write_time(receiver / "Build/cache.bin");
            auto preserved = request(query);
            check(preserved.at("can_integrate") && preserved.at("local_data").at("files") == 3 &&
                      preserved.at("local_data").at("directories") == 4,
                  "Ignored build files, shared directories and empty local directories are "
                  "permitted");
            atomic_write(receiver / "Build/new.cache", "new build file\n");
            rejects([&] { apply(preserved); },
                    "Adding an unrelated ignored file invalidates an older preview");
            fs::remove(receiver / "Build/new.cache");
            rejects(
                [&] { cli(target, {"git", "integrate", "team", peer, "main", "--commit", first}); },
                "Selected-commit flags cannot silently apply a branch head");
            git(b, {"config", "branch.main.mergeOptions", "--invalid-integration-test-option"});
            preview = request(query);
            rejects([&] { apply(preview); },
                    "A native Git command failure is reported without automatic retry");
            bool incomplete = false;
            for (const auto &entry :
                 fs::directory_iterator(work / "target/git-integrations/team")) {
                auto receipt = Json::parse(read_file(entry.path()));
                if (receipt.at("status") == "incomplete" && receipt.contains("error"))
                    incomplete = true;
            }
            check(incomplete && git(b, {"rev-parse", "HEAD"}) == first &&
                      read_file(receiver / "first.txt") == "first\n",
                  "Native failure retains an incomplete receipt and preserves the unchanged "
                  "repository");
            check(!fs::exists(receiver / ".git/cysynck-integration.lock"),
                  "Completed or failed calls release only their own integration lock");
            git(b, {"config", "branch.main.mergeOptions", "--squash --autostash"});
            preview = request(query);
            done = apply(preview);
            check(done.at("applied") && git(b, {"rev-parse", "HEAD"}) == second &&
                      git(b, {"rev-list", "--count", "HEAD"}) == "2",
                  "Fast-forward overrides branch squash/autostash options and creates no extra "
                  "commit");
            check(read_file(receiver / "first.txt") == "second\n" &&
                      !fs::exists(receiver / fs::u8path("dir/été [x].bin")) &&
                      sha256_file(receiver / "new.bin") == sha256_file(root / "new.bin"),
                  "Fast-forward applies modifications, deletions and files beyond 8 MiB");
            check(sha256_file(receiver / "Build/cache.bin") == localDigest &&
                      fs::last_write_time(receiver / "Build/cache.bin") == localModified &&
                      read_file(receiver / "dir/local.txt") ==
                          "local file in a tracked directory\n" &&
                      read_file(receiver / "keep.txt") == "keep\n" &&
                      fs::is_directory(receiver / "Scratch/empty") &&
                      fs::is_directory(receiver / "dir/empty"),
                  "Fast-forward retains ignored bytes, timestamps, untracked files and empty "
                  "directories");
            check(done.at("local_data") == preserved.at("local_data"),
                  "Completed receipt retains the reviewed metadata fingerprint");
            git(b, {"checkout", "--detach", second});
            check(!request(query).at("can_integrate").get<bool>(), "Detached HEAD is preserved");
            git(b, {"checkout", "main"});
            atomic_write(receiver / "local-only.txt", "local commit\n");
            auto local = commit(b, "Local");
            check(!request(query).at("can_integrate").get<bool>(),
                  "Older imported branches cannot rewind local work");
            atomic_write(root / "peer-only.txt", "peer commit\n");
            commit(a, "Peer");
            auto diverged = cli(source, {"git", "export", "team", "--lfs", "false"});
            cli(target, {"git", "import", "team", peer, diverged.at("id")});
            check(!request(query).at("can_integrate").get<bool>() &&
                      git(b, {"rev-parse", "HEAD"}) == local,
                  "Divergent histories are refused without creating conflicts");
            git(b, {"reset", "--hard", second});
            atomic_write(receiver / ".git/MERGE_HEAD", second + "\n");
            rejects([&] { request(query); }, "An unfinished Git operation blocks integration");
            fs::remove(receiver / ".git/MERGE_HEAD");
            git(b, {"config", "core.sparseCheckout", "true"});
            rejects([&] { request(query); }, "Sparse working trees require manual integration");
            git(b, {"config", "core.sparseCheckout", "false"});
            auto clean = request(query);
            fs::create_directory(receiver / ".git/cysynck-integration.lock");
            rejects([&] { apply(clean); }, "Another CySynck integration owns the repository lock");
            fs::remove(receiver / ".git/cysynck-integration.lock");
            atomic_write(receiver / ".git/index.lock", "external Git operation\n");
            rejects([&] { apply(clean); }, "An external Git index lock blocks application");
            check(read_file(receiver / ".git/index.lock") == "external Git operation\n",
                  "External Git locks are preserved");
            fs::remove(receiver / ".git/index.lock");
            git(b, {"update-ref", clean.at("tracking_ref"), second});
            rejects([&] { apply(clean); }, "Moving the imported branch invalidates the preview");
            git(b, {"update-ref", clean.at("tracking_ref"), clean.at("head")});
            atomic_write(root / ".gitattributes", "*.txt filter=spy\n");
            commit(a, "Filtered");
            auto filtered = cli(source, {"git", "export", "team", "--lfs", "false"});
            cli(target, {"git", "import", "team", peer, filtered.at("id")});
            check(!request(query).at("can_integrate").get<bool>() &&
                      !fs::exists(receiver / "hook-ran"),
                  "Checkout filters are blocked without invoking their commands");
            fs::remove(root / ".gitattributes");
            auto pointer =
                "version https://git-lfs.github.com/spec/v1\noid sha256:" + std::string(64, 'a') +
                "\nsize 1\n";
            atomic_write(root / "asset.bin", pointer);
            commit(a, "LFS pointer");
            auto lfs = cli(source, {"git", "export", "team", "--lfs", "false"});
            cli(target, {"git", "import", "team", peer, lfs.at("id")});
            check(!request(query).at("can_integrate").get<bool>(),
                  "LFS pointers require verified separate extraction");
            auto tracking = clean.at("tracking_ref").get<std::string>();
            auto hash = trim(offline_git(b, {"hash-object", "-w", "--stdin"}, "unsafe\n"));
            for (const auto &spec : std::vector<std::string>{
                     "100644 blob " + hash + "\t.git\n", "120000 blob " + hash + "\tlink\n",
                     "160000 commit " + second + "\tmodule\n",
                     "100644 blob " + hash + "\tCase\n100644 blob " + hash + "\tcase\n"}) {
                auto badTree = trim(offline_git(b, {"mktree"}, spec));
                auto badCommit = trim(offline_git(b,
                                                  {"-c", "user.name=Integration", "-c",
                                                   "user.email=test@example.invalid", "commit-tree",
                                                   badTree, "-p", second},
                                                  "Unsafe integration fixture\n"));
                git(b, {"update-ref", tracking, badCommit});
                check(!request(query).at("can_integrate").get<bool>(),
                      "Unsafe paths, links, submodules and case collisions cannot reach checkout");
            }
            check(git(b, {"rev-parse", "HEAD"}) == second,
                  "All later refusals preserve the local commit");
        }
        std::cout << "PASS " << checks << " Git integration checks; fixtures: " << path_string(run)
                  << '\n';
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what()
                  << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
