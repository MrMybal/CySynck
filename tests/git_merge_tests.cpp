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
static std::string commit(const Json &p, const std::string &message) {
    git(p, {"-c",
            "core.hooksPath="
#ifdef _WIN32
            "NUL",
#else
            "/dev/null",
#endif
            "-c", "user.name=Merge review", "-c", "user.email=merge@example.invalid", "-c",
            "commit.gpgsign=false", "commit", "-m", message});
    return trim(git(p, {"rev-parse", "HEAD"}));
}
static void virtual_file(const Json &p, const std::string &name, const std::string &bytes,
                         const std::string &mode = "120000") {
    auto hash =
        mode == "160000" ? bytes : trim(offline_git(p, {"hash-object", "-w", "--stdin"}, bytes));
    git(p, {"update-index", "--add", "--cacheinfo", mode + "," + hash + "," + name});
}
static Json fingerprints(const fs::path &root) {
    Json result = Json::object();
    for (const auto &entry : fs::recursive_directory_iterator(root))
        if (entry.is_regular_file())
            result[path_string(entry.path().lexically_relative(root))] = sha256_file(entry.path());
    return result;
}
int main() {
    auto run = fs::current_path() / "git-merge-test-data" / random_id();
    fs::create_directories(run);
    try {
        for (auto format : {"sha1", "sha256"}) {
            auto work = run / format, root = work / fs::u8path("repo été");
            fs::create_directories(root);
            Core core(work / "profile");
            auto p = core.request("POST", "/api/projects",
                                  {{"id", "team"},
                                   {"name", "Merge team"},
                                   {"root", path_string(root)},
                                   {"mode", "git-bundles"},
                                   {"exchange_path", path_string(work / "exchange")},
                                   {"git_lfs", false},
                                   {"backup_interval_minutes", 0}});
            auto setup = cli(core, {"git", "setup", "team", "--object-format", format});
            cli(core, {"git", "setup", "team", "--object-format", format, "--apply", "true",
                       "--plan", setup.at("plan")});
            git(p, {"config", "core.autocrlf", "false"});
            git(p, {"config", "core.attributesFile",
#ifdef _WIN32
                    "NUL"
#else
                    "/dev/null"
#endif
                   });
            git(p, {"config", "merge.default", ""});
            std::string lines;
            for (int i = 0; i < 24; ++i)
                lines += "line " + std::to_string(i) + "\n";
            for (auto name : {"clean.txt", "conflict.txt", "same.txt", "delete.txt",
                              "both-delete.txt", "modify-delete.txt", "driver.special"})
                atomic_write(root / name, name == std::string("clean.txt") ? lines : "base\n");
            atomic_write(root / "binary.bin", std::string("\0base", 5));
            atomic_write(root / "encoding.bin", std::string("\xff", 1));
            atomic_write(root / "large.txt", std::string(512 * 1024 + 1, 'l'));
            auto pointer = [](char c) {
                return "version https://git-lfs.github.com/spec/v1\noid sha256:" +
                       std::string(64, c) + "\nsize 1\n";
            };
            atomic_write(root / "asset.uasset", pointer('a'));
            atomic_write(root / fs::u8path("dossier/été [x].txt"), "ancestor\n");
            git(p, {"add", "-A"});
            virtual_file(p, "link", "../../outside");
            auto common = commit(p, "Common");
            atomic_write(root / "clean.txt", "LOCAL first\n" + lines.substr(lines.find('\n') + 1));
            atomic_write(root / "conflict.txt", "local\n");
            atomic_write(root / "same.txt", "same edit\n");
            atomic_write(root / "local.txt", "local only\n");
            atomic_write(root / "modify-delete.txt", "modified locally\n");
            atomic_write(root / "add.txt", "local addition\n");
            atomic_write(root / "binary.bin", std::string("\0local", 6));
            atomic_write(root / "encoding.bin", std::string("\xfflocal", 6));
            atomic_write(root / "large.txt", std::string(512 * 1024 + 1, 'm'));
            atomic_write(root / "asset.uasset", pointer('b'));
            atomic_write(root / "folder", "file on local side\n");
            atomic_write(root / "Case.txt", "local case\n");
            atomic_write(root / ".gitattributes", "*.special merge=spy\n");
            atomic_write(root / "driver.special", "local driver\n");
            atomic_write(root / fs::u8path("dossier/été [x].txt"), "local unicode\n");
            fs::remove(root / "both-delete.txt");
            git(p, {"add", "-A"});
            virtual_file(p, "link", "local target");
            virtual_file(p, "module", common, "160000");
            auto local = commit(p, "Local branch");
            // These owned fixtures contain deliberately index-only link/submodule entries.
            git(p, {"checkout", "--force", "-b", "peer", common});
            atomic_write(root / "clean.txt",
                         lines.substr(0, lines.rfind("line 23\n")) + "PEER last\n");
            atomic_write(root / "conflict.txt", "peer\n");
            atomic_write(root / "same.txt", "same edit\n");
            atomic_write(root / "peer.txt", "peer only\n");
            atomic_write(root / "add.txt", "peer addition\n");
            atomic_write(root / "binary.bin", std::string("\0peer", 5));
            atomic_write(root / "encoding.bin", std::string("\xfepeer", 5));
            atomic_write(root / "large.txt", "small peer\n");
            atomic_write(root / "asset.uasset", pointer('c'));
            atomic_write(root / "folder/file.txt", "directory on peer side\n");
            atomic_write(root / "case.txt", "peer case\n");
            atomic_write(root / "driver.special", "peer driver\n");
            atomic_write(root / fs::u8path("dossier/été [x].txt"), "peer unicode\n");
            for (auto name : {"delete.txt", "both-delete.txt", "modify-delete.txt"})
                fs::remove(root / name);
            git(p, {"add", "-A"});
            virtual_file(p, "link", "peer target");
            virtual_file(p, "module", common, "160000");
            auto peer = commit(p, "Peer branch");
            git(p, {"checkout", "--force", "main"});
            git(p, {"update-ref", "refs/remotes/cysynck/alice/main", peer});
            git(p, {"config", "merge.spy.driver", "command-that-must-not-run"});
            git(p, {"config", "core.fsmonitor", "command-that-must-not-run"});
            atomic_write(root / "conflict.txt", "uncommitted user edit\n");
            atomic_write(root / "staged.txt", "staged user edit\n");
            git(p, {"-c", "core.fsmonitor=false", "add", "staged.txt"});
            auto baseline = fingerprints(root);
            auto review = cli(core, {"git", "merge-review", "team", "alice", "main"});
            check(review.at("can_review") && review.at("merge_base") == common &&
                      review.at("base") == local && review.at("head") == peer &&
                      review.at("read_only") && !review.at("working_tree_included").get<bool>(),
                  "Three-way review pins both commits and their common ancestor");
            auto query = Json{{"peer", "alice"},
                              {"branch", "main"},
                              {"base", local},
                              {"head", peer},
                              {"local_ref", "refs/heads/main"},
                              {"merge_base", common}};
            auto file = [&](const std::string &path) {
                auto q = query;
                q["path"] = path;
                return core.request("POST", "/api/projects/team/git-merge-file", q);
            };
            auto classification = [&](const std::string &path) {
                for (const auto &entry : review.at("files"))
                    if (entry.at("path") == path)
                        return entry.at("classification").get<std::string>();
                throw std::runtime_error("Missing review path");
            };
            check(classification("local.txt") == "local_only" &&
                      classification("peer.txt") == "peer_only" &&
                      classification("same.txt") == "identical" &&
                      classification("clean.txt") == "both_changed",
                  "Overview distinguishes independent, identical and overlapping paths");
            auto clean = file("clean.txt");
            check(clean.at("kind") == "clean" &&
                      clean.at("candidate").get<std::string>().starts_with("LOCAL first\n") &&
                      clean.at("candidate").get<std::string>().ends_with("PEER last\n") &&
                      clean.at("conflicts") == 0,
                  "Separate line edits produce a combined text proposal");
            auto conflict = file("conflict.txt");
            auto candidate = conflict.at("candidate").get<std::string>();
            check(conflict.at("kind") == "conflict" && conflict.at("conflicts").get<int>() > 0 &&
                      candidate.find("<<<<<<< LOCAL") != candidate.npos &&
                      candidate.find("||||||| ANCESTOR") != candidate.npos &&
                      candidate.find(">>>>>>> PEER") != candidate.npos,
                  "Overlapping edits show stable diff3 labels including the ancestor");
            check(conflict.at("texts").at("local") == "local\n" &&
                      conflict.at("texts").at("ancestor") == "base\n" &&
                      conflict.at("texts").at("peer") == "peer\n",
                  "Previews read committed blobs and exclude dirty working edits");
            check(file("add.txt").at("kind") == "conflict",
                  "Different add/add content is treated as a conflict");
            check(file("same.txt").at("candidate") == "same edit\n" && file("same.txt").at("clean"),
                  "Identical edits produce one clean proposal");
            check(file("local.txt").at("candidate") == "local only\n" &&
                      file("peer.txt").at("candidate") == "peer only\n",
                  "One-sided additions preserve the changed side");
            check(file("delete.txt").at("kind") == "delete" &&
                      !file("delete.txt").at("result_exists").get<bool>() &&
                      file("both-delete.txt").at("kind") == "delete",
                  "One-sided and identical deletions are represented without writing files");
            check(file("modify-delete.txt").at("reason") == "modify_delete",
                  "Modify/delete conflicts require manual handling");
            check(file("binary.bin").at("reason") == "binary" &&
                      file("encoding.bin").at("reason") == "binary",
                  "Binary and invalid UTF-8 blobs are not merged as text");
            check(file("large.txt").at("reason") == "large" &&
                      file("asset.uasset").at("reason") == "lfs_pointer",
                  "Large blobs and LFS pointers remain bounded metadata reviews");
            check(file("link").at("reason") == "symlink" &&
                      file("module").at("reason") == "submodule",
                  "Symlinks and submodules never reach the text merge tool");
            check(file("driver.special").at("reason") == "attributes",
                  "Configured repository merge drivers are not executed");
            check(classification("folder") == "structural" &&
                      classification("folder/file.txt") == "structural" &&
                      file("folder").at("reason") == "structure",
                  "File/directory conflicts are marked for structural review");
            check(classification("Case.txt") == "structural" &&
                      classification("case.txt") == "structural",
                  "Case aliases remain explicit across platforms");
            check(file("dossier/été [x].txt").at("kind") == "conflict",
                  "Unicode and wildcard path characters are handled literally");
            check(fingerprints(root) == baseline, "All previews preserve working bytes, index, "
                                                  "refs, configuration and Git object storage");
            check(fs::is_empty(core.state() / "git-file-previews"),
                  "Successful and conflicted drafts remove only their temporary inputs");
            rejects([&] { file("../outside"); }, "Unknown paths cannot escape the review");
            rejects(
                [&] {
                    cli(core, {"git", "merge-file", "team", "alice", "main", "clean.txt", "--apply",
                               "true"});
                },
                "CLI explicitly refuses application flags");
            rejects(
                [&] {
                    auto q = query;
                    q["apply"] = false;
                    core.request("POST", "/api/projects/team/git-merge-review", q);
                },
                "API review cannot be mistaken for an apply action");
            rejects(
                [&] {
                    auto q = query;
                    q["merge_base"] = peer;
                    core.request("POST", "/api/projects/team/git-merge-review", q);
                },
                "Pinned ancestor mismatches require a refreshed review");
            auto cliFile = cli(core, {"git", "merge-file", "team", "alice", "main", "conflict.txt",
                                      "--base", local, "--head", peer, "--merge-base", common,
                                      "--local-ref", "refs/heads/main"});
            check(cliFile.at("kind") == "conflict",
                  "CLI and API share the same pinned file proposal");
            git(p, {"config", "merge.default", "spy"});
            check(file("clean.txt").at("reason") == "attributes",
                  "A configured default merge driver also requires manual review");
            git(p, {"config", "merge.default", ""});
            git(p, {"update-ref", "refs/remotes/cysynck/alice/main", common});
            rejects([&] { file("clean.txt"); },
                    "A moved imported branch invalidates existing previews");
            git(p, {"update-ref", "refs/remotes/cysynck/alice/main", peer});
            git(p, {"symbolic-ref", "HEAD", "refs/heads/unborn"});
            auto unborn = cli(core, {"git", "merge-review", "team", "alice", "main"});
            check(!unborn.at("can_review").get<bool>() && unborn.at("reason") == "unborn",
                  "Unborn branches disclose the missing common-ancestor review");
            git(p, {"symbolic-ref", "HEAD", "refs/heads/main"});
            auto tree = trim(offline_git(p, {"rev-parse", common + "^{tree}"}));
            auto newCommit = [&](const std::vector<std::string> &parents,
                                 const std::string &message) {
                std::vector<std::string> args{"-c",          "user.name=Merge review",
                                              "-c",          "user.email=merge@example.invalid",
                                              "commit-tree", tree};
                for (const auto &parent : parents) {
                    args.push_back("-p");
                    args.push_back(parent);
                }
                return trim(offline_git(p, args, message + "\n"));
            };
            auto orphan = newCommit({}, "Unrelated");
            git(p, {"update-ref", "refs/remotes/cysynck/alice/main", orphan});
            check(cli(core, {"git", "merge-review", "team", "alice", "main"}).at("reason") ==
                      "unrelated",
                  "Unrelated history does not invent an empty common ancestor");
            auto one = newCommit({common}, "one"), two = newCommit({common}, "two");
            auto crissLocal = newCommit({one, two}, "left"),
                 crissPeer = newCommit({two, one}, "right");
            git(p, {"update-ref", "refs/heads/main", crissLocal});
            git(p, {"update-ref", "refs/remotes/cysynck/alice/main", crissPeer});
            check(cli(core, {"git", "merge-review", "team", "alice", "main"}).at("reason") ==
                      "multiple_bases",
                  "Criss-cross ancestry requires a real repository merge instead of choosing one "
                  "base");
        }
        std::cout << "PASS " << checks
                  << " three-way Git review checks; fixtures: " << path_string(run) << '\n';
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what()
                  << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
