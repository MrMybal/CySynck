#include <cysynck/core.hpp>
#include "git_lfs.hpp"
#include <functional>
#include <iostream>
#include <set>
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
    auto run = fs::current_path() / "git-history-test-data" / random_id();
    fs::create_directories(run);
    try {
        Core source(run / "source"), target(run / "target");
        auto root = run / fs::u8path("repo été");
        fs::create_directories(root);
        Json settings = {{"id", "team"},
                         {"name", "Team"},
                         {"root", path_string(root)},
                         {"exchange_path", path_string(run / "exchange")},
                         {"mode", "git-bundles"},
                         {"git_lfs", false},
                         {"backup_interval_minutes", 0}};
        auto a = source.request("POST", "/api/projects", settings);
        git(a, {"init", "--initial-branch=main"});
        git(a, {"config", "commit.gpgsign", "false"});
        auto commit = [&](const std::string &message) {
            git(a, {"-c", "user.name=Contributor été", "-c", "user.email=author@example.invalid",
                    "commit", "-m", message});
            return git(a, {"rev-parse", "HEAD"});
        };
        atomic_write(root / "text.cpp", "root\n");
        atomic_write(root / "binary.bin", std::string("\0\xff", 2));
        fs::create_directories(root / "dir");
        atomic_write(root / "dir/nested.txt", "nested root\n");
        git(a, {"add", "--all"});
        auto initial = commit("Root <script> & été");
        auto receiver = run / fs::u8path("receiver été");
        git(Json{{"root", path_string(run)}},
            {"clone", "--no-local", path_string(root), path_string(receiver)});
        settings["root"] = path_string(receiver);
        auto b = target.request("POST", "/api/projects", settings);
        atomic_write(root / "text.cpp", "next\n");
        atomic_write(root / fs::u8path("data [x] été.txt"), "literal path\n");
        git(a, {"add", "--all"});
        auto next = commit("Next\n\nA multi-line body does not break record framing");
        auto initialTree = git(a, {"rev-parse", initial + "^{tree}"});
        auto side = offline_git(a,
                                {"-c", "user.name=Side contributor", "-c",
                                 "user.email=side@example.invalid", "-c", "commit.gpgsign=false",
                                 "commit-tree", initialTree, "-p", initial},
                                "Side branch commit\n");
        side.erase(side.find_first_of("\r\n"));
        git(a, {"update-ref", "refs/heads/side", side});
        auto tree = git(a, {"rev-parse", "HEAD^{tree}"});
        auto merged = offline_git(
            a,
            {"-c", "user.name=Integrator", "-c", "user.email=integrator@example.invalid", "-c",
             "commit.gpgsign=false", "commit-tree", tree, "-p", next, "-p", side},
            "Merge fixture\n");
        merged.erase(merged.find_first_of("\r\n"));
        git(a, {"update-ref", "refs/heads/main", merged});
        for (int i = 0; i < 4; ++i) {
            git(a, {"-c", "user.name=Contributor", "-c", "user.email=author@example.invalid",
                    "commit", "--allow-empty", "-m", "Empty " + std::to_string(i)});
        }
        auto exported = cli(source, {"git", "export", "team"});
        auto peer = exported.at("peer").get<std::string>();
        cli(target, {"git", "import", "team", peer, exported.at("id")});
        git(b, {"config", "log.showSignature", "true"});
        git(b, {"config", "gpg.program", "must-not-launch-a-signature-program"});
        git(b, {"config", "diff.external", "must-not-launch-an-external-diff"});
        git(b, {"config", "diff.spy.textconv", "must-not-launch-a-textconv"});
        atomic_write(receiver / ".gitattributes", "*.cpp diff=spy\n");
        atomic_write(receiver / "text.cpp", "dirty\n");
        atomic_write(receiver / "staged.txt", "staged\n");
        git(b, {"add", "staged.txt"});
        auto status = git(b, {"status", "--porcelain"}),
             index = sha256_file(receiver / ".git/index");
        auto objectStats = git(b, {"count-objects", "-v"});
        auto page = cli(target, {"git", "history", "team", peer, "main", "--limit", "2"});
        check(page.at("commits").size() == 2 && page.at("offset") == 0 &&
                  page.at("next_offset") == 2,
              "History is paginated with a stable ref context");
        check(page.at("base") == initial && page.at("head") == exported.at("head"),
              "History pins the local and imported branch identities");
        auto query = Json{{"peer", peer},
                          {"branch", "main"},
                          {"base", page.at("base")},
                          {"head", page.at("head")}};
        auto history = [&](Json data) {
            data.update(query);
            return target.request("POST", "/api/projects/team/git-history", data);
        };
        auto review = [&](const std::string &oid, unsigned parent = 999) {
            auto input = query;
            input["commit"] = oid;
            if (parent != 999)
                input["parent"] = parent;
            return target.request("POST", "/api/projects/team/git-commit", input);
        };
        auto file = [&](const std::string &oid, const std::string &path, unsigned parent = 999) {
            auto input = query;
            input["commit"] = oid;
            input["path"] = path;
            if (parent != 999)
                input["parent"] = parent;
            return target.request("POST", "/api/projects/team/git-commit-file", input);
        };
        std::set<std::string> seen;
        for (unsigned offset = 0; offset < 8; offset += 2) {
            auto current = history({{"offset", offset}, {"limit", 2}});
            for (const auto &entry : current.at("commits"))
                check(seen.insert(entry.at("oid")).second, "Pages have no repeated commits");
        }
        check(seen.size() == 8 && history({{"offset", 8}, {"limit", 2}}).at("commits").empty(),
              "History reaches its end without fabricating a next page");
        check(seen.contains(side),
              "History includes commits reached through secondary merge parents");
        check(history({{"offset", 6}, {"limit", 2}}).at("next_offset").is_null(),
              "The final full page has no next cursor");
        auto rootReview = review(initial);
        check(rootReview.at("comparison_base").is_null() && rootReview.at("parent") == 0 &&
                  rootReview.at("files").size() == 3,
              "Root commits compare against an empty tree without writing Git objects");
        check(rootReview.at("commit").at("author").at("name") == "Contributor été" &&
                  rootReview.at("commit").at("subject") == "Root <script> & été",
              "Commit authors and subjects preserve Unicode and literal markup");
        check(file(initial, "text.cpp").at("before") == "" &&
                  file(initial, "text.cpp").at("after") == "root\n",
              "Root file review shows added committed content");
        check(file(initial, "binary.bin").at("reason") == "binary",
              "Root binary files remain metadata previews");
        check(file(initial, "dir/nested.txt").at("after") == "nested root\n",
              "Root commit review recursively includes nested files");
        check(file(next, "text.cpp").at("before") == "root\n" &&
                  file(next, "text.cpp").at("after") == "next\n",
              "Commit diff compares its parent rather than the dirty local workspace");
        check(file(next, "data [x] été.txt").at("after") == "literal path\n",
              "Commit previews handle Unicode and literal pathspec characters");
        check(review(merged).at("commit").at("parents").size() == 2 &&
                  review(merged).at("files").empty(),
              "Merge commits default to the first parent");
        check(review(merged, 2).at("files").size() == 2 &&
                  file(merged, "text.cpp", 2).at("after") == "next\n",
              "A second merge parent can be reviewed explicitly");
        check(review(exported.at("head")).at("files").empty(), "Empty commits remain inspectable");
        rejects([&] { review(merged, 0); }, "Non-root commits cannot use parent zero");
        rejects([&] { review(merged, 3); }, "A missing merge parent is refused");
        rejects([&] { review(initial, 1); }, "Root commits cannot pretend to have a parent");
        rejects([&] { review("HEAD"); }, "Commit selection requires a full object ID");
        rejects([&] { file(next, "../escape"); }, "A commit preview must select a changed path");
        rejects([&] { cli(target, {"git", "history", "team", peer, "main", "--offset", "2"}); },
                "Continuation pages require both pinned refs");
        for (const auto &limit : {Json(0), Json(101), Json(-1), Json(1.5), Json(true), Json("2")})
            rejects([&] { history({{"limit", limit}}); }, "Invalid API page sizes are rejected");
        rejects([&] { cli(target, {"git", "history", "team", peer, "main", "--limit", "2oops"}); },
                "CLI page sizes reject trailing junk");
        check(git(b, {"status", "--porcelain"}) == status &&
                  sha256_file(receiver / ".git/index") == index &&
                  git(b, {"rev-parse", "HEAD"}) == initial,
              "History and commit review preserve HEAD, the index and staged or dirty files");
        check(git(b, {"count-objects", "-v"}) == objectStats,
              "History and root commit previews do not add objects to the Git database");
        auto outside =
            offline_git(b,
                        {"-c", "user.name=Outside", "-c", "user.email=outside@example.invalid",
                         "-c", "commit.gpgsign=false", "commit-tree", tree},
                        "Unrelated\n");
        outside.erase(outside.find_first_of("\r\n"));
        rejects([&] { review(outside); },
                "Unrelated repository objects cannot be selected through branch history");
        git(b, {"update-ref", page.at("tracking_ref"), next});
        rejects([&] { history({{"offset", 2}}); }, "A changed imported ref invalidates pagination");
        rejects([&] { review(initial); }, "A changed imported ref invalidates commit review");
        git(b, {"update-ref", page.at("tracking_ref"), page.at("head")});
        git(b, {"update-ref", "refs/heads/main", next});
        rejects([&] { history({{"offset", 2}}); }, "A changed local HEAD invalidates pagination");
        auto shaRoot = run / "sha256";
        fs::create_directories(shaRoot);
        settings["id"] = "sha256";
        settings["root"] = path_string(shaRoot);
        settings["exchange_path"] = path_string(run / "sha256-exchange");
        auto shaProject = target.request("POST", "/api/projects", settings);
        git(shaProject, {"init", "--object-format=sha256", "--initial-branch=main"});
        atomic_write(shaRoot / "root.txt", "SHA-256\n");
        git(shaProject, {"add", "--all"});
        git(shaProject, {"-c", "user.name=SHA", "-c", "user.email=sha@example.invalid", "-c",
                         "commit.gpgsign=false", "commit", "-m", "SHA root"});
        auto shaHead = git(shaProject, {"rev-parse", "HEAD"});
        git(shaProject, {"update-ref", "refs/remotes/cysynck/peer/main", shaHead});
        auto shaHistory = cli(target, {"git", "history", "sha256", "peer", "main"});
        check(shaHead.size() == 64 && shaHistory.at("commits")[0].at("oid") == shaHead,
              "History supports Git SHA-256 repositories");
        check(cli(target, {"git", "commit-file", "sha256", "peer", "main", shaHead, "root.txt"})
                      .at("after") == "SHA-256\n",
              "SHA-256 root commit previews use their repository object format");
        atomic_write(receiver / ".git/shallow", next + "\n");
        rejects([&] { cli(target, {"git", "history", "team", peer, "main"}); },
                "Shallow boundaries cannot masquerade as root commits in history");
        rejects([&] { cli(target, {"git", "commit", "team", peer, "main", next}); },
                "Shallow commit previews require a complete repository");
        fs::remove(receiver / ".git/shallow");
        std::cout << "PASS " << checks << " Git history checks; fixtures: " << path_string(run)
                  << '\n';
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what()
                  << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
