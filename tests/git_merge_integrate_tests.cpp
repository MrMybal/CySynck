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
    for (const auto &entry : fs::recursive_directory_iterator(root))
        if (entry.is_regular_file())
            result[path_string(entry.path().lexically_relative(root))] = sha256_file(entry.path());
    return result;
}
static std::string commit(const Json &p, const std::string &message) {
    git(p, {"-c",
            "core.hooksPath="
#ifdef _WIN32
            "NUL",
#else
            "/dev/null",
#endif
            "-c", "commit.gpgsign=false", "commit", "-m", message});
    return trim(git(p, {"rev-parse", "HEAD"}));
}
int main() {
    auto run = fs::current_path() / "git-merge-integrate-data" / random_id();
    fs::create_directories(run);
    try {
        for (auto format : {"sha1", "sha256"}) {
            auto work = run / format, root = work / fs::u8path("repo été");
            fs::create_directories(root);
            Core core(work / "profile");
            auto project = core.request("POST", "/api/projects",
                                        {{"id", "team"},
                                         {"name", "Merge team"},
                                         {"root", path_string(root)},
                                         {"mode", "git-bundles"},
                                         {"git_lfs", false},
                                         {"exchange_path", path_string(work / "exchange")},
                                         {"backup_interval_minutes", 0}});
            auto setup = cli(core, {"git", "setup", "team", "--object-format", format});
            cli(core, {"git", "setup", "team", "--object-format", format, "--apply", "true",
                       "--plan", setup.at("plan")});
            for (auto [key, value] : {std::pair{"core.autocrlf", "false"},
                                      {"user.name", "Merge Tester"},
                                      {"user.email", "merge@example.invalid"}})
                git(project, {"config", key, value});
            std::string original;
            for (int i = 0; i < 24; ++i)
                original += "line " + std::to_string(i) + "\n";
            atomic_write(root / "combined.txt", original);
            atomic_write(root / "delete.txt", "deleted on peer\n");
            atomic_write(root / "conflict.txt", "ancestor\n");
            git(project, {"add", "-A"});
            auto common = commit(project, "Common");
            atomic_write(root / "combined.txt",
                         "local first\n" + original.substr(original.find('\n') + 1));
            atomic_write(root / "local.txt", "local only\n");
            git(project, {"add", "-A"});
            auto local = commit(project, "Local");
            git(project, {"checkout", "-b", "peer", common});
            atomic_write(root / "combined.txt",
                         original.substr(0, original.rfind("line 23\n")) + "peer last\n");
            atomic_write(root / fs::u8path("peer/été [x].txt"), "peer only\n");
            fs::remove(root / "delete.txt");
            git(project, {"add", "-A"});
            auto peer = commit(project, "Peer");
            git(project, {"checkout", "main"});
            git(project, {"update-ref", "refs/remotes/cysynck/alice/main", peer});
            atomic_write(root / "keep.txt", "local untracked\n");
            atomic_write(root / "Build/cache", "ignored output\n");
            atomic_write(root / ".git/info/exclude", "Build/\n");
            auto query = Json{{"peer", "alice"}, {"branch", "main"}};
            auto request = [&](Json q) {
                return core.request("POST", "/api/projects/team/git-merge", q);
            };
            auto baseline = hashes(root);
            auto preview = request(query);
            check(preview.at("can_integrate") && preview.at("integration_kind") == "merge" &&
                      preview.at("merge_clean") && preview.at("merge_base") == common &&
                      preview.at("result_commit").is_null(),
                  "Divergent merge preview computes the complete clean result");
            check(preview.at("files_after").size() == 4 &&
                      preview.at("local_data").at("files") == 2 &&
                      preview.at("merge_identity").at("name") == "Merge Tester",
                  "Preview includes merged inventory, local data and author");
            check(hashes(root) == baseline && fs::is_empty(core.state() / "git-merge-work"),
                  "Preview preserves all repository bytes, objects, refs, index and working files");
            auto again = cli(core, {"git", "merge", "team", "alice", "main"});
            check(preview.at("plan") == again.at("plan"),
                  "Preview tokens exclude temporary paths and commit timestamps");
            rejects(
                [&] {
                    auto q = query;
                    q["apply"] = true;
                    request(q);
                },
                "Merge requires the reviewed token");
            rejects(
                [&] { cli(core, {"git", "merge", "team", "alice", "main", "--strategy", "ours"}); },
                "CLI refuses unreviewed strategy flags");
            rejects(
                [&] {
                    auto q = query;
                    q["apply"] = true;
                    q["plan"] = preview.at("plan");
                    core.request("POST", "/api/projects/team/git-integrate", q);
                },
                "Fast-forward route cannot reuse a merge token");
            atomic_write(root / "combined.txt", "dirty working file\n");
            check(!request(query).at("can_integrate").get<bool>(),
                  "Dirty tracked work blocks merging");
            rejects(
                [&] {
                    auto q = query;
                    q["apply"] = true;
                    q["plan"] = preview.at("plan");
                    request(q);
                },
                "Stale preview preserves dirty working data");
            atomic_write(root / "combined.txt",
                         "local first\n" + original.substr(original.find('\n') + 1));
            atomic_write(root / "staged.txt", "staged work\n");
            git(project, {"add", "staged.txt"});
            check(!request(query).at("can_integrate").get<bool>(),
                  "Staged work is preserved by refusal");
            git(project, {"rm", "--cached", "staged.txt"});
            fs::remove(root / "staged.txt");
            atomic_write(root / fs::u8path("peer/été [x].txt"), "untracked collision\n");
            check(!request(query).at("can_integrate").get<bool>(),
                  "Incoming merged paths cannot overwrite local data");
            fs::remove(root / fs::u8path("peer/été [x].txt"));
            fs::remove(root / "peer");
            atomic_write(root / ".git/info/attributes", "*.txt merge=spy\n");
            check(!request(query).at("can_integrate").get<bool>(),
                  "Local attributes require manual Git merging");
            fs::remove(root / ".git/info/attributes");
            git(project, {"config", "merge.default", "spy"});
            check(!request(query).at("can_integrate").get<bool>(),
                  "Custom default drivers are refused");
            git(project, {"config", "--unset", "merge.default"});
            preview = request(query);
            // Git ignores post-merge hook exit status; check an explicit marker instead.
            git(project, {"config", "core.hooksPath", path_string(work / "hooks")});
            atomic_write(work / "hooks/post-merge",
                         "#!/bin/sh\nprintf ran > .cysynck-hook-ran\nexit 93\n");
            fs::permissions(work / "hooks/post-merge", fs::perms::owner_all);
            git(project, {"config", "filter.spy.process", "command-that-must-not-run"});
            git(project, {"config", "branch.main.mergeOptions", "--squash --autostash"});
            rejects(
                [&] {
                    auto q = query;
                    q["apply"] = true;
                    q["plan"] = preview.at("plan");
                    request(q);
                },
                "Configuration changes invalidate the plan");
            preview = request(query);
            auto done = cli(core, {"git", "merge", "team", "alice", "main", "--apply", "true",
                                   "--plan", preview.at("plan")});
            auto merged = done.at("result_commit").get<std::string>();
            check(done.at("applied") && done.at("status") == "complete" && merged != local &&
                      merged != peer,
                  "Apply creates a new merge commit and completes its receipt");
            check(trim(git(project, {"rev-list", "--parents", "-n", "1", merged})) ==
                      merged + " " + local + " " + peer,
                  "Merge commit retains the exact local and imported parents");
            check(trim(git(project, {"rev-parse", merged + "^{tree}"})) ==
                          preview.at("result_tree").get<std::string>() &&
                      read_file(root / "combined.txt").starts_with("local first\n") &&
                      read_file(root / "combined.txt").ends_with("peer last\n"),
                  "Checked-out bytes match the reviewed complete merged tree");
            check(!fs::exists(root / "delete.txt") &&
                      read_file(root / "local.txt") == "local only\n" &&
                      read_file(root / "keep.txt") == "local untracked\n" &&
                      read_file(root / "Build/cache") == "ignored output\n",
                  "Merge preserves one-sided changes, deletions and unrelated local data");
            check(!fs::exists(root / ".git/MERGE_HEAD") &&
                      !fs::exists(root / ".git/cysynck-integration.lock") &&
                      fs::is_empty(core.state() / "git-merge-work"),
                  "Successful merge leaves no Git operation or private workspace");
            check(!fs::exists(root / ".cysynck-hook-ran"), "Configured post-merge hooks never run");
            auto checked = cli(core, {"git", "integration-check", "team", done.at("id")});
            check(checked.at("position") == "after" && checked.at("tracked_matches") &&
                      checked.at("local_data_matches"),
                  "Receipt checks verify the actual two-parent merge commit and merged files");
            auto record = done;
            record["id"] = record["receipt"] = random_id();
            record["status"] = "incomplete";
            record["applied"] = false;
            auto receipt = core.state() / "git-integrations/team" /
                           (record.at("id").get<std::string>() + ".json");
            atomic_write(receipt, record.dump(2), true);
            auto incomplete = cli(core, {"git", "integration-check", "team", record.at("id")});
            check(incomplete.at("can_acknowledge"),
                  "Interrupted merge receipt can be reviewed at its exact integrated state");
            auto acknowledged = cli(core, {"git", "integration-acknowledge", "team",
                                           record.at("id"), "--plan", incomplete.at("plan")});
            check(!acknowledged.at("needs_attention").get<bool>() &&
                      acknowledged.at("status") == "incomplete",
                  "Acknowledgement preserves the interrupted merge receipt status");
            auto tampered = record;
            tampered["merge_identity"]["name"] = "Different author";
            atomic_write(receipt, tampered.dump(2), true);
            check(!cli(core, {"git", "integration-check", "team", record.at("id")})
                       .at("can_acknowledge")
                       .get<bool>(),
                  "Receipt checks refuse a changed reviewed author");
            tampered = record;
            tampered["result_tree"] = trim(git(project, {"rev-parse", peer + "^{tree}"}));
            atomic_write(receipt, tampered.dump(2), true);
            check(!cli(core, {"git", "integration-check", "team", record.at("id")})
                       .at("can_acknowledge")
                       .get<bool>(),
                  "Receipt checks recompute and refuse a changed merged tree");
            record["result_commit"] = nullptr;
            atomic_write(receipt, record.dump(2), true);
            auto discovered = cli(core, {"git", "integration-check", "team", record.at("id")});
            check(
                discovered.at("can_acknowledge") &&
                    discovered.at("resolved_result_commit") == merged,
                "An interrupted receipt can identify its exact merge commit from parents and tree");
            git(project, {"reset", "--hard", local});
            auto original_check = cli(core, {"git", "integration-check", "team", record.at("id")});
            check(original_check.at("position") == "before" && original_check.at("can_acknowledge"),
                  "Interrupted merge receipts also verify the preserved original repository state");
            git(project, {"reset", "--hard", merged});
            auto before_preview = hashes(root);
            check(!request(query).at("can_integrate").get<bool>(),
                  "An already merged peer cannot create another merge commit");
            check(hashes(root) == before_preview,
                  "Blocked merge previews preserve repository contents");
            // Add a genuinely conflicting peer branch on the existing common ancestor.
            git(project, {"checkout", "-b", "conflicting", common});
            atomic_write(root / "conflict.txt", "peer conflicting\n");
            git(project, {"add", "conflict.txt"});
            auto conflicting = commit(project, "Conflicting peer");
            git(project, {"checkout", "main"});
            atomic_write(root / "conflict.txt", "local conflicting\n");
            git(project, {"add", "conflict.txt"});
            commit(project, "Conflicting local");
            git(project, {"update-ref", "refs/remotes/cysynck/alice/main", conflicting});
            auto conflict_before = hashes(root);
            auto conflict = request(query);
            check(!conflict.at("can_integrate").get<bool>() &&
                      !conflict.at("merge_clean").get<bool>() &&
                      conflict.at("conflicts") == Json::array({"conflict.txt"}),
                  "Whole-repository merge conflicts block application and list affected paths");
            check(hashes(root) == conflict_before && fs::is_empty(core.state() / "git-merge-work"),
                  "Conflicted previews preserve the source repository and clean private objects");
            rejects(
                [&] {
                    auto q = query;
                    q["apply"] = true;
                    q["plan"] = preview.at("plan");
                    request(q);
                },
                "A previous clean token cannot apply a conflicted merge");
            // Validate unchanged paths as well as the files changed on either branch.
            git(project, {"checkout", "-b", "unsafe-peer", common});
            atomic_write(root / "LOCAL.txt", "case alias\n");
            git(project, {"add", "LOCAL.txt"});
            auto unsafe = commit(project, "Case alias");
            git(project, {"checkout", "main"});
            git(project, {"update-ref", "refs/remotes/cysynck/alice/main", unsafe});
            check(!request(query).at("can_integrate").get<bool>(),
                  "Whole-repository validation catches a case alias with an unchanged local path");
            git(project, {"checkout", "unsafe-peer"});
            git(project, {"rm", "LOCAL.txt"});
            atomic_write(root / ".gitattributes", "*.txt merge=spy\n");
            git(project, {"add", ".gitattributes"});
            unsafe = commit(project, "Custom merge rules");
            git(project, {"checkout", "main"});
            git(project, {"config", "merge.spy.driver", "command-that-must-not-run"});
            git(project, {"update-ref", "refs/remotes/cysynck/alice/main", unsafe});
            check(!request(query).at("can_integrate").get<bool>(),
                  "Committed custom merge rules are refused before any driver could run");
            git(project, {"checkout", "-b", "binary-peer", common});
            atomic_write(root / "conflict.txt", std::string("\0peer", 5));
            git(project, {"add", "conflict.txt"});
            auto binary = commit(project, "Binary overlap");
            git(project, {"checkout", "main"});
            git(project, {"update-ref", "refs/remotes/cysynck/alice/main", binary});
            check(!request(query).at("can_integrate").get<bool>(),
                  "Overlapping binary changes require manual merging");
            git(project, {"checkout", "binary-peer"});
            atomic_write(root / "conflict.txt", std::string(512 * 1024 + 1, 'x'));
            git(project, {"add", "conflict.txt"});
            auto large = commit(project, "Large overlap");
            git(project, {"checkout", "main"});
            git(project, {"update-ref", "refs/remotes/cysynck/alice/main", large});
            check(!request(query).at("can_integrate").get<bool>(),
                  "Overlapping text is bounded before repository merging");
            git(project, {"checkout", "-b", "asset-peer", common});
            atomic_write(root / "Asset.uasset",
                         "version https://git-lfs.github.com/spec/v1\noid sha256:" +
                             std::string(64, 'a') + "\nsize 1\n");
            git(project, {"add", "Asset.uasset"});
            auto asset = commit(project, "LFS asset");
            git(project, {"checkout", "main"});
            git(project, {"update-ref", "refs/remotes/cysynck/alice/main", asset});
            check(!request(query).at("can_integrate").get<bool>(),
                  "LFS pointers never reach divergent integration");
            git(project, {"checkout", "-b", "one-sided-binary", common});
            atomic_write(root / "binary.bin", std::string("\0\xffpeer", 6));
            git(project, {"add", "binary.bin"});
            auto one_sided = commit(project, "One-sided binary");
            git(project, {"checkout", "main"});
            git(project, {"update-ref", "refs/remotes/cysynck/alice/main", one_sided});
            auto binary_preview = request(query);
            check(binary_preview.at("can_integrate"),
                  "One-sided binary objects can be merged without text conversion");
            auto binary_apply = query;
            binary_apply.update({{"apply", true}, {"plan", binary_preview.at("plan")}});
            check(request(binary_apply).at("applied") &&
                      read_file(root / "binary.bin") == std::string("\0\xffpeer", 6),
                  "One-sided binary integration preserves exact bytes");
        }
        std::cout << "PASS " << checks
                  << " divergent Git integration checks; fixtures: " << path_string(run) << '\n';
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what()
                  << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
