#include <cysynck/core.hpp>
#include "git_lfs.hpp"
#include <functional>
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
static Json setup(Core &core, const fs::path &root, const fs::path &exchange) {
    fs::create_directories(root);
    return core.request("POST", "/api/projects",
                        {{"id", "team"},
                         {"name", "Team"},
                         {"root", path_string(root)},
                         {"exchange_path", path_string(exchange)},
                         {"mode", "git-bundles"},
                         {"git_lfs", false},
                         {"backup_interval_minutes", 0}});
}
static void commit(const Json &project, const std::string &message) {
    git(project, {"-c", "user.name=Review", "-c", "user.email=review@example.invalid", "commit",
                  "-m", message});
}
static void blob(const Json &project, const std::string &path, const std::string &bytes,
                 const std::string &mode = "100644") {
    auto hash = offline_git(project, {"hash-object", "-w", "--stdin"}, bytes);
    hash.erase(hash.find_first_of("\r\n"));
    git(project, {"update-index", "--add", "--cacheinfo", mode + "," + hash + "," + path});
}
int main() {
    auto run = fs::current_path() / "git-review-test-data" / random_id();
    fs::create_directories(run);
    try {
        Core source(run / "source"), target(run / "target");
        auto a = setup(source, run / "repo a été", run / "exchange");
        auto rootA = fs::u8path(a.at("root").get<std::string>());
        git(a, {"init", "--initial-branch=main"});
        atomic_write(rootA / "text.txt", "base\n--counter\n");
        atomic_write(rootA / "delete.txt", "remove\n");
        atomic_write(rootA / "mode.txt", "same bytes\n");
        git(a, {"add", "--all"});
        commit(a, "Base");
        auto base = git(a, {"rev-parse", "HEAD"});
        auto rootB = run / "repo b été";
        git(Json{{"root", path_string(run)}},
            {"clone", "--no-local", path_string(rootA), path_string(rootB)});
        auto b = setup(target, rootB, run / "exchange");
        git(a, {"checkout", "-b", "feature/review"});
        atomic_write(rootA / "text.txt", "peer\n++counter\n");
        fs::remove(rootA / "delete.txt");
        atomic_write(rootA / "binary.bin", std::string("\0binary\xff", 8));
        atomic_write(rootA / "invalid.txt", std::string(1, '\xff'));
        atomic_write(rootA / "large.txt", std::string(512 * 1024 + 1, 'a'));
        atomic_write(rootA / "empty.txt", "");
        atomic_write(rootA / fs::u8path("data [x] été.txt"), "<script>display as text</script>\n");
        git(a, {"add", "--all"});
        auto lfsHash = sha256("asset payload");
        blob(a, "Asset.uasset",
             "version https://git-lfs.github.com/spec/v1\noid sha256:" + lfsHash + "\nsize 13\n");
        blob(a, "link.txt", "../../outside.txt", "120000");
        blob(a, "mode.txt", "same bytes\n", "100755");
        git(a, {"update-index", "--add", "--cacheinfo", "160000," + base + ",submodule"});
        commit(a, "Peer changes");
        auto exported = cli(source, {"git", "export", "team"});
        auto peer = exported.at("peer").get<std::string>();
        cli(target, {"git", "import", "team", peer, exported.at("id")});
        atomic_write(rootB / "text.txt", "local dirty content\n");
        atomic_write(rootB / "staged.txt", "local staged\n");
        git(b, {"add", "staged.txt"});
        git(b, {"config", "diff.external", "command-that-must-not-run"});
        git(b, {"config", "diff.spy.textconv", "command-that-must-not-run"});
        atomic_write(rootB / ".gitattributes", "*.txt diff=spy\n");
        auto status = git(b, {"status", "--porcelain"}), index = sha256_file(rootB / ".git/index");
        auto branches = cli(target, {"git", "branches", "team"});
        check(branches.size() == 1 && branches[0].at("branch") == "feature/review",
              "Imported branches are listed without parsing display text");
        auto review = cli(target, {"git", "review", "team", peer, "feature/review"});
        check(review.at("relation") == "fast_forward" && review.at("local_only_commits") == 0 &&
                  review.at("peer_only_commits") == 1,
              "Branch review reports fast-forward ancestry");
        check(review.at("merge_base") == base && review.at("files").size() == 11,
              "Review includes exact changed files and common ancestor");
        auto file = [&](const std::string &path) {
            return target.request("POST", "/api/projects/team/git-review-file",
                                  {{"peer", peer},
                                   {"branch", "feature/review"},
                                   {"base", review.at("base")},
                                   {"head", review.at("head")},
                                   {"path", path}});
        };
        auto text = file("text.txt");
        check(text.at("before") == "base\n--counter\n" && text.at("after") == "peer\n++counter\n",
              "Text review uses committed blobs rather than dirty work files");
        check(text.at("added_lines") == 2 && text.at("removed_lines") == 2 && text.at("hunks") == 1,
              "Patch statistics count content that resembles diff headers");
        check(text.at("patch").get<std::string>().find("+++counter") != std::string::npos,
              "Unified patch retains exact added text");
        check(file("binary.bin").at("binary") && file("invalid.txt").at("binary"),
              "Binary and non-UTF-8 blobs are bounded metadata previews");
        check(file("large.txt").at("reason") == "large",
              "Oversized blobs are not loaded for text previews");
        check(file("delete.txt").at("after") == "" && file("delete.txt").at("removed_lines") == 1,
              "Deleted file preview preserves its old content");
        check(!file("empty.txt").at("identical").get<bool>(),
              "An added empty file remains a change");
        check(!file("mode.txt").at("identical").get<bool>() &&
                  file("mode.txt").at("patch").get<std::string>().find("new mode 100755") !=
                      std::string::npos,
              "Executable-mode changes remain visible");
        check(file("data [x] été.txt").at("after") == "<script>display as text</script>\n",
              "Unicode and pathspec metacharacters are handled literally");
        check(file("link.txt").at("after") == "../../outside.txt",
              "Symlink review displays the Git blob without following a filesystem path");
        check(file("submodule").at("reason") == "submodule",
              "Submodules are inspected as commit identities");
        check(file("Asset.uasset").at("reason") == "lfs_pointer" &&
                  file("Asset.uasset").at("lfs_after").at("sha256") == lfsHash,
              "LFS pointers are identified without reading or fetching assets");
        rejects([&] { file("../outside"); },
                "Review cannot request a path outside its changed-file list");
        rejects([&] { cli(target, {"git", "review", "team", "../bad", "main"}); },
                "Peer identities cannot escape the tracking namespace");
        rejects([&] { cli(target, {"git", "review", "team", peer, "../../heads/main"}); },
                "Invalid branch ref names are refused");
        check(git(b, {"rev-parse", "HEAD"}) == base &&
                  git(b, {"status", "--porcelain"}) == status &&
                  sha256_file(rootB / ".git/index") == index,
              "Review preserves HEAD, staged changes, dirty files and index bytes");
        commit(b, "Local staged change");
        rejects([&] { file("text.txt"); }, "A changed local HEAD invalidates a pinned file review");
        review = cli(target, {"git", "review", "team", peer, "feature/review"});
        check(review.at("relation") == "diverged" && review.at("local_only_commits") == 1 &&
                  review.at("peer_only_commits") == 1,
              "Diverged branches report both sides of history");
        auto tracking = review.at("tracking_ref").get<std::string>();
        git(b, {"update-ref", tracking, base});
        rejects([&] { file("text.txt"); },
                "A changed imported ref invalidates a pinned file review");
        check(cli(target, {"git", "review", "team", peer, "feature/review"}).at("relation") ==
                  "local_ahead",
              "An older peer branch is distinguished from a fast-forward");
        git(b, {"update-ref", tracking, git(b, {"rev-parse", "HEAD"})});
        auto equal = cli(target, {"git", "review", "team", peer, "feature/review"});
        check(equal.at("relation") == "identical" && equal.at("files").empty(),
              "Identical branches produce an empty review");
        git(b, {"checkout", "--orphan", "unrelated"});
        git(b, {"-c", "user.name=Review", "-c", "user.email=review@example.invalid", "commit", "-m",
                "Orphan"});
        auto unrelated = cli(target, {"git", "review", "team", peer, "feature/review"});
        check(unrelated.at("relation") == "unrelated" && unrelated.at("merge_base").is_null(),
              "Unrelated histories are reported explicitly");
        std::cout << "PASS " << checks << " Git review checks; fixtures: " << path_string(run)
                  << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what()
                  << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
