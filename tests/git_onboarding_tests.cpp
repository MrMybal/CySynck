#include <cysynck/core.hpp>
#include "git_lfs.hpp"
#include <functional>
#include <cstdlib>
#include <iostream>
using namespace cysynck;
class GitEnvironment {
    std::string previous;
    bool existed;
    void set(const char *value) {
#ifdef _WIN32
        _putenv_s("GIT_DIR", value ? value : "");
#else
        if (value)
            setenv("GIT_DIR", value, 1);
        else
            unsetenv("GIT_DIR");
#endif
    }

  public:
    GitEnvironment(const std::string &value) {
        auto original = std::getenv("GIT_DIR");
        existed = original != nullptr;
        if (original)
            previous = original;
        set(value.c_str());
    }
    ~GitEnvironment() { set(existed ? previous.c_str() : nullptr); }
};
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
static Json project(Core &core, const fs::path &root, const fs::path &exchange,
                    const std::string &id = "team") {
    fs::create_directories(root);
    return core.request("POST", "/api/projects",
                        {{"id", id},
                         {"name", id},
                         {"root", path_string(root)},
                         {"exchange_path", path_string(exchange)},
                         {"mode", "git-bundles"},
                         {"git_lfs", false},
                         {"backup_interval_minutes", 0}});
}
int main() {
    auto run = fs::current_path() / "git-onboarding-test-data" / random_id();
    fs::create_directories(run);
    try {
        Core source(run / "source"), target(run / "target");
        auto a = project(source, run / fs::u8path("source été"), run / "exchange");
        auto b = project(target, run / fs::u8path("target été"), run / "exchange");
        auto root = fs::u8path(a.at("root").get<std::string>()),
             receiver = fs::u8path(b.at("root").get<std::string>());
        atomic_write(root / "keep.txt", "present before setup\n");
        auto setup = cli(source, {"git", "setup", "team"});
        check(setup.at("can_initialize") && !fs::exists(root / ".git"),
              "Setup preview does not create metadata");
        rejects([&] { cli(source, {"git", "setup", "team", "--apply", "true"}); },
                "Setup application requires a preview token");
        rejects([&] { cli(source, {"git", "setup", "team", "--branch", "../bad"}); },
                "Invalid initial branches are refused");
        rejects([&] { cli(source, {"git", "setup", "team", "--object-format", "unknown"}); },
                "Unsupported object formats are refused");
        rejects([&] { cli(source, {"git", "setup", "team", "--apply", "yes"}); },
                "Setup rejects invalid CLI booleans");
        rejects(
            [&] {
                cli(source, {"git", "setup", "team", "--branch", "other", "--apply", "true",
                             "--plan", setup.at("plan")});
            },
            "Changing setup options invalidates the plan");
        auto initialized =
            cli(source, {"git", "setup", "team", "--apply", "true", "--plan", setup.at("plan")});
        check(initialized.at("applied") &&
                  read_file(root / "keep.txt") == "present before setup\n" &&
                  !fs::exists(root / ".git/index"),
              "Initialization preserves existing work files and leaves the index absent");
        auto headBytes = read_file(root / ".git/HEAD"),
             configBytes = read_file(root / ".git/config");
        rejects(
            [&] {
                cli(source,
                    {"git", "setup", "team", "--apply", "true", "--plan", setup.at("plan")});
            },
            "Existing metadata is never reinitialized");
        check(read_file(root / ".git/HEAD") == headBytes &&
                  read_file(root / ".git/config") == configBytes,
              "Refused setup leaves repository metadata unchanged");
        auto commit = [&](const std::string &message) {
            git(a, {"add", "--all"});
            git(a, {"-c", "user.name=Onboarding", "-c", "user.email=test@example.invalid", "-c",
                    "commit.gpgsign=false", "commit", "-m", message});
            return git(a, {"rev-parse", "HEAD"});
        };
        atomic_write(root / "first.txt", "old version\n");
        auto first = commit("First");
        atomic_write(root / "first.txt", "new version\n");
        fs::create_directories(root / "dir");
        atomic_write(root / "dir/nested.txt", "nested\n");
        auto newest = commit("Second");
        auto exported = cli(source, {"git", "export", "team"});
        auto peer = exported.at("peer").get<std::string>();
        auto receiverSetup = cli(target, {"git", "setup", "team", "--branch", "start/here"});
        cli(target, {"git", "setup", "team", "--branch", "start/here", "--apply", "true", "--plan",
                     receiverSetup.at("plan")});
        auto status = cli(target, {"git", "status", "team"});
        check(status.at("head").is_null() && status.at("unborn") &&
                  status.at("branch") == "start/here",
              "Git status distinguishes an unborn branch from a missing repository");
        atomic_write(receiver / "local.txt", "local staged content\n");
        git(b, {"add", "local.txt"});
        auto index = sha256_file(receiver / ".git/index");
        auto localHead = read_file(receiver / ".git/HEAD");
        cli(target, {"git", "import", "team", peer, exported.at("id")});
        auto review = cli(target, {"git", "review", "team", peer, "main"});
        check(review.at("relation") == "unborn" && review.at("base").is_null() &&
                  review.at("local_ref") == "refs/heads/start/here",
              "Imported branches are reviewable before the first local commit");
        check(
            review.at("files").size() == 3 && review.at("peer_only_commits") == 2 &&
                review.at("local_only_commits") == 0,
            "Unborn branch comparison includes the entire imported tree, not only the last commit");
        auto file =
            cli(target, {"git", "review-file", "team", peer, "main", "first.txt", "--base",
                         "unborn", "--head", newest, "--local-ref", "refs/heads/start/here"});
        check(file.at("before") == "" && file.at("after") == "new version\n" &&
                  file.at("added_lines") == 1,
              "Unborn file preview compares against an empty tree");
        auto page = cli(target, {"git", "history", "team", peer, "main", "--limit", "1"});
        auto next = cli(target, {"git", "history", "team", peer, "main", "--limit", "1", "--offset",
                                 "1", "--base", "unborn", "--head", newest, "--local-ref",
                                 "refs/heads/start/here"});
        check(page.at("base").is_null() && next.at("commits")[0].at("oid") == first,
              "History continuation accepts an explicit unborn base");
        check(cli(target, {"git", "commit-file", "team", peer, "main", first, "first.txt"})
                      .at("after") == "old version\n",
              "Root commit review is available without local history");
        check(sha256_file(receiver / ".git/index") == index &&
                  read_file(receiver / ".git/HEAD") == localHead &&
                  read_file(receiver / "local.txt") == "local staged content\n" &&
                  !fs::exists(receiver / "first.txt"),
              "Import and unborn review preserve HEAD, staged work and the local directory");
        git(b, {"symbolic-ref", "HEAD", "refs/heads/other"});
        rejects(
            [&] {
                cli(target, {"git", "history", "team", peer, "main", "--offset", "1", "--base",
                             "unborn", "--head", newest, "--local-ref", "refs/heads/start/here"});
            },
            "Switching between unborn branches invalidates a pinned review");
        git(b, {"symbolic-ref", "HEAD", "refs/heads/start/here"});
        fs::create_directories(receiver / ".git/refs/heads/start");
        atomic_write(receiver / ".git/refs/heads/start/here", "broken reference\n");
        rejects([&] { cli(target, {"git", "review", "team", peer, "main"}); },
                "A corrupt local reference cannot be mistaken for an unborn branch");
        fs::remove(receiver / ".git/refs/heads/start/here");
        git(b, {"update-ref", "refs/heads/start/here", newest});
        rejects(
            [&] {
                cli(target,
                    {"git", "review", "team", peer, "main", "--base", "unborn", "--head", newest});
            },
            "Creating the first local commit invalidates an unborn pin");
        check(cli(target, {"git", "review", "team", peer, "main"}).at("relation") == "identical",
              "A born branch resumes ordinary comparison");
        git(b, {"checkout", "--detach", newest});
        check(cli(target, {"git", "history", "team", peer, "main"}).at("local_ref").is_null(),
              "Detached HEAD is represented separately from an unborn branch");
        status = cli(target, {"git", "status", "team"});
        check(status.at("branch").is_null() && !status.at("unborn").get<bool>() &&
                  status.at("head") == newest,
              "Git status reports a detached committed HEAD");
        auto c = project(target, run / "sha256", run / "sha256-exchange", "sha256");
        auto shaPlan = cli(target, {"git", "setup", "sha256", "--object-format", "sha256"});
        cli(target, {"git", "setup", "sha256", "--object-format", "sha256", "--apply", "true",
                     "--plan", shaPlan.at("plan")});
        check(offline_git(c, {"rev-parse", "--show-object-format"}) == "sha256\n",
              "Repository setup supports the requested SHA-256 format");
        auto shaRoot = fs::u8path(c.at("root").get<std::string>());
        atomic_write(shaRoot / "sha.txt", "SHA-256 tree\n");
        git(c, {"add", "sha.txt"});
        git(c, {"-c", "user.name=Onboarding", "-c", "user.email=test@example.invalid", "-c",
                "commit.gpgsign=false", "commit", "-m", "SHA-256 root"});
        auto shaHead = git(c, {"rev-parse", "HEAD"});
        git(c, {"update-ref", "refs/remotes/cysynck/sha-peer/main", shaHead});
        git(c, {"update-ref", "-d", "refs/heads/main"});
        auto shaReview = cli(target, {"git", "review", "sha256", "sha-peer", "main"});
        auto shaFile = cli(target, {"git", "review-file", "sha256", "sha-peer", "main", "sha.txt"});
        check(shaReview.at("base").is_null() &&
                  shaReview.at("head").get<std::string>().size() == 64 &&
                  shaReview.at("files").size() == 1 && shaFile.at("before") == "" &&
                  shaFile.at("after") == "SHA-256 tree\n",
              "Unborn comparisons also use the correct empty tree for SHA-256 repositories");
        auto d = project(target, run / "broken", run / "broken-exchange", "broken");
        auto broken = fs::u8path(d.at("root").get<std::string>());
        atomic_write(broken / ".git", "preserved marker\n");
        check(!cli(target, {"git", "setup", "broken"}).at("can_initialize").get<bool>() &&
                  read_file(broken / ".git") == "preserved marker\n",
              "A pre-existing Git metadata file is preserved even if invalid");
        auto e = project(target, run / "appeared", run / "appeared-exchange", "appeared");
        auto appeared = fs::u8path(e.at("root").get<std::string>());
        auto before = cli(target, {"git", "setup", "appeared"});
        {
            GitEnvironment environment(path_string(root / ".git"));
            rejects([&] { cli(target, {"git", "setup", "appeared"}); },
                    "Setup refuses inherited Git repository redirection");
            rejects(
                [&] {
                    cli(target, {"git", "setup", "appeared", "--apply", "true", "--plan",
                                 before.at("plan")});
                },
                "Inherited Git redirection cannot apply a previously reviewed setup");
        }
        check(!fs::exists(appeared / ".git") && read_file(root / ".git/config") == configBytes,
              "Refused Git redirection preserves the project and external repository");
        fs::create_directories(appeared / ".git");
        atomic_write(appeared / ".git/marker", "external metadata\n");
        rejects(
            [&] {
                cli(target,
                    {"git", "setup", "appeared", "--apply", "true", "--plan", before.at("plan")});
            },
            "Metadata created after preview prevents initialization");
        check(read_file(appeared / ".git/marker") == "external metadata\n" &&
                  !fs::exists(appeared / ".git/HEAD"),
              "Setup preserves metadata created between preview and application");
        auto f = project(target, run / "bare", run / "bare-exchange", "bare");
        git(f, {"init", "--bare"});
        check(!cli(target, {"git", "setup", "bare"}).at("can_initialize").get<bool>() &&
                  !fs::exists(run / "bare/.git"),
              "Bare repositories are not wrapped inside a new working repository");
        std::cout << "PASS " << checks << " Git onboarding checks; fixtures: " << path_string(run)
                  << '\n';
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what()
                  << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
