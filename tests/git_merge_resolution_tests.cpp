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
static std::string commit(const Json &project, const std::string &message) {
    git(project, {"add", "-A"});
    git(project, {"-c",
                  "core.hooksPath="
#ifdef _WIN32
                  "NUL",
#else
                  "/dev/null",
#endif
                  "-c", "commit.gpgsign=false", "commit", "-m", message});
    return trim(git(project, {"rev-parse", "HEAD"}));
}
int main() {
    auto run = fs::current_path() / "git-merge-resolution-data" / random_id();
    fs::create_directories(run);
    try {
        for (auto format : {"sha1", "sha256"}) {
            auto work = run / format, root = work / fs::u8path("repo été");
            fs::create_directories(root);
            Core core(work / "profile");
            auto p = core.request("POST", "/api/projects",
                                  {{"id", "team"},
                                   {"name", "Resolution team"},
                                   {"root", path_string(root)},
                                   {"mode", "git-bundles"},
                                   {"git_lfs", false},
                                   {"exchange_path", path_string(work / "exchange")},
                                   {"backup_interval_minutes", 0}});
            auto setup = cli(core, {"git", "setup", "team", "--object-format", format});
            cli(core, {"git", "setup", "team", "--object-format", format, "--apply", "true",
                       "--plan", setup.at("plan")});
            git(p, {"config", "core.autocrlf", "false"});
            git(p, {"config", "user.name", "Resolution Tester"});
            git(p, {"config", "user.email", "resolution@example.invalid"});
            for (auto path : {"conflict.txt", "delete-local.txt", "delete-peer.txt"})
                atomic_write(root / path, "ancestor\n");
            std::string original;
            for (int i = 0; i < 24; ++i)
                original += "line " + std::to_string(i) + "\n";
            atomic_write(root / "auto.txt", original);
            auto common = commit(p, "Common");
            atomic_write(root / "conflict.txt", "local conflict\n");
            fs::remove(root / "delete-local.txt");
            atomic_write(root / "delete-peer.txt", "modified locally\n");
            auto added_name = std::string("added été [x], --file.txt");
            atomic_write(root / fs::u8path(added_name), "local addition\n");
            atomic_write(root / "auto.txt",
                         "local first\n" + original.substr(original.find('\n') + 1));
            auto local = commit(p, "Local");
            git(p, {"checkout", "-b", "peer", common});
            atomic_write(root / "conflict.txt", "peer conflict\n");
            atomic_write(root / "delete-local.txt", "modified by peer\n");
            fs::remove(root / "delete-peer.txt");
            atomic_write(root / fs::u8path(added_name), "peer addition\n");
            atomic_write(root / "auto.txt",
                         original.substr(0, original.rfind("line 23\n")) + "peer last\n");
            atomic_write(root / "peer-only.txt", "peer only\n");
            auto peer = commit(p, "Peer");
            git(p, {"checkout", "main"});
            git(p, {"update-ref", "refs/remotes/cysynck/alice/main", peer});
            atomic_write(root / "Build/cache", "ignored bytes\n");
            atomic_write(root / "keep.txt", "untracked bytes\n");
            atomic_write(root / ".git/info/exclude", "Build/\n");
            auto query = Json{{"peer", "alice"}, {"branch", "main"}};
            auto request = [&](const Json &q) {
                return core.request("POST", "/api/projects/team/git-merge", q);
            };
            auto baseline = hashes(root);
            auto blocked = request(query);
            check(!blocked.at("can_integrate").get<bool>() &&
                      !blocked.at("result_ready").get<bool>() &&
                      blocked.at("conflicts").size() == 4 &&
                      blocked.at("unresolved_conflicts").size() == 4 &&
                      blocked.at("plan").is_null(),
                  "Every reported conflict initially requires an explicit choice");
            bool local_deletion = false, peer_deletion = false;
            for (const auto &file : blocked.at("conflict_files")) {
                if (file.at("path") == "delete-local.txt")
                    local_deletion = file.at("local").is_null() && file.at("peer").is_object();
                if (file.at("path") == "delete-peer.txt")
                    peer_deletion = file.at("peer").is_null() && file.at("local").is_object();
            }
            check(local_deletion && peer_deletion,
                  "Conflict choices distinguish a deleted side from an empty file");
            auto selected = query;
            selected["resolutions"] = Json::array({{{"path", "conflict.txt"}, {"side", "local"}}});
            auto partial = request(selected);
            check(!partial.at("can_integrate").get<bool>() &&
                      partial.at("unresolved_conflicts").size() == 3,
                  "Partial resolution cannot produce an applicable token");
            selected["resolutions"] =
                Json::array({{{"path", "delete-peer.txt"}, {"side", "peer"}},
                             {{"path", "conflict.txt"}, {"side", "local"}},
                             {{"path", "delete-local.txt"}, {"side", "local"}},
                             {{"path", added_name}, {"side", "peer"}}});
            auto ready = request(selected);
            check(ready.at("can_integrate") && ready.at("result_ready") &&
                      !ready.at("merge_clean").get<bool>() &&
                      ready.at("unresolved_conflicts").empty() &&
                      ready.at("files_after").size() == 4,
                  "Explicit whole-file choices produce a complete resolved inventory");
            check(
                hashes(root) == baseline && fs::is_empty(core.state() / "git-merge-work"),
                "Unresolved, partial and resolved previews preserve all original repository bytes");
            auto choices_file = work / fs::u8path("choices été.json");
            atomic_write(choices_file, selected.at("resolutions").dump());
            auto from_cli = cli(core, {"git", "merge", "team", "alice", "main", "--resolutions",
                                       path_string(choices_file)});
            check(from_cli.at("plan") == ready.at("plan") &&
                      from_cli.at("resolutions").at(0).at("path") == added_name,
                  "CLI JSON choices are canonicalized independently of request ordering");
            auto changed = selected;
            changed["resolutions"][1]["side"] = "peer";
            auto alternative = request(changed);
            check(alternative.at("can_integrate") && alternative.at("plan") != ready.at("plan") &&
                      alternative.at("result_tree") != ready.at("result_tree"),
                  "Each changed choice changes both result tree and review token");
            auto retained = selected;
            retained["resolutions"][0]["side"] = "local";
            retained["resolutions"][2]["side"] = "peer";
            auto retained_preview = request(retained);
            check(retained_preview.at("can_integrate") &&
                      retained_preview.at("files_after").size() == 6,
                  "Choosing the surviving modified sides retains both modify/delete files");
            changed.update({{"apply", true}, {"plan", ready.at("plan")}});
            rejects([&] { request(changed); },
                    "A token cannot be reused after changing a whole-file choice");
            auto unknown = selected;
            unknown["resolutions"].push_back({{"path", "auto.txt"}, {"side", "local"}});
            check(!request(unknown).at("can_integrate").get<bool>(),
                  "Choices cannot override nonconflicting automatic merge results");
            for (const auto &bad :
                 {Json(nullptr), Json::object(),
                  Json::array({{{"path", "../outside"}, {"side", "local"}}}),
                  Json::array({{{"path", "conflict.txt"}, {"side", "ours"}}}),
                  Json::array(
                      {{{"path", "conflict.txt"}, {"side", "local"}, {"content", "injection"}}}),
                  Json::array({{{"path", "conflict.txt"}, {"side", "local"}},
                               {{"path", "conflict.txt"}, {"side", "peer"}}}),
                  Json::array({{{"path", "conflict.txt"}, {"side", "local"}},
                               {{"path", "CONFLICT.txt"}, {"side", "peer"}}})}) {
                auto q = query;
                q["resolutions"] = bad;
                rejects([&] { request(q); },
                        "Malformed, duplicate and unsafe resolution records are refused");
            }
            rejects([&] { core.request("POST", "/api/projects/team/git-integrate", selected); },
                    "Fast-forward integration cannot silently ignore conflict choices");
            atomic_write(root / "conflict.txt", "uncommitted\n");
            check(!request(selected).at("can_integrate").get<bool>(),
                  "Resolved previews still protect modified tracked work");
            atomic_write(root / "conflict.txt", "local conflict\n");
            check(hashes(root) == baseline,
                  "Blocked applications and malformed choices preserve all source bytes");
            auto apply = selected;
            apply.update({{"apply", true}, {"plan", ready.at("plan")}});
            auto done = request(apply);
            auto merged = done.at("result_commit").get<std::string>();
            check(done.at("applied") && done.at("status") == "complete" &&
                      done.at("result_tree") == ready.at("result_tree"),
                  "Reviewed conflict resolution creates the exact planned tree");
            check(trim(git(p, {"rev-list", "--parents", "-n", "1", "HEAD"})) ==
                      merged + " " + local + " " + peer,
                  "Resolved merge retains both ordered history parents");
            check(read_file(root / "conflict.txt") == "local conflict\n" &&
                      read_file(root / fs::u8path(added_name)) == "peer addition\n" &&
                      !fs::exists(root / "delete-local.txt") &&
                      !fs::exists(root / "delete-peer.txt"),
                  "Local, peer, add/add and modify/delete choices apply exact committed content or "
                  "absence");
            auto combined = read_file(root / "auto.txt");
            check(combined.starts_with("local first\n") && combined.ends_with("peer last\n") &&
                      read_file(root / "peer-only.txt") == "peer only\n",
                  "Nonconflicting changes on both branches remain automatically merged");
            check(read_file(root / "Build/cache") == "ignored bytes\n" &&
                      read_file(root / "keep.txt") == "untracked bytes\n" &&
                      trim(git(p, {"status", "--porcelain=v1", "--untracked-files=no"})).empty() &&
                      !fs::exists(root / ".git/MERGE_HEAD") &&
                      !fs::exists(root / ".git/cysynck-integration.lock") &&
                      fs::is_empty(core.state() / "git-merge-work"),
                  "Resolved integration preserves local payloads and leaves no pending merge or "
                  "private workspace");
            auto receipt = fs::u8path(done.at("receipt_path").get<std::string>());
            auto record = Json::parse(read_file(receipt));
            check(record.at("resolutions") == ready.at("resolutions"),
                  "Receipt records the exact canonical whole-file choices");
            auto reviewed = cli(core, {"git", "integration-check", "team", done.at("id")});
            check(reviewed.at("position") == "after" && reviewed.at("tracked_matches") &&
                      reviewed.at("local_data_matches") && reviewed.at("errors").empty(),
                  "Receipt verification recomputes all resolved objects from immutable parents");
            auto catalog = cli(core, {"git", "integrations", "team"});
            check(catalog.at("records").at(0).at("resolved_conflicts") == 4,
                  "Receipt summaries disclose the conflict choice count");
            auto tampered = record;
            tampered["resolutions"][1]["side"] = "peer";
            atomic_write(receipt, tampered.dump(2), true);
            check(!cli(core, {"git", "integration-check", "team", done.at("id")})
                       .at("errors")
                       .empty(),
                  "Changed receipt choices cannot validate the actual merged tree");
            record["status"] = "incomplete";
            record["applied"] = false;
            record["error"] = "Synthetic interrupted receipt fixture";
            atomic_write(receipt, record.dump(2), true);
            auto checked = cli(core, {"git", "integration-check", "team", done.at("id")});
            check(checked.at("can_acknowledge"),
                  "An interrupted resolved merge can be reviewed at its exact integrated state");
            auto before_ack = hashes(root);
            cli(core, {"git", "integration-acknowledge", "team", done.at("id"), "--plan",
                       checked.at("plan")});
            check(hashes(root) == before_ack,
                  "Resolved receipt acknowledgement preserves all repository files and objects");
            // The same receipt also supports an interrupted operation before checkout.
            git(p, {"reset", "--hard", local}); // Only this disposable test repository.
            atomic_write(receipt, record.dump(2), true);
            check(cli(core, {"git", "integration-check", "team", done.at("id")})
                      .at("can_acknowledge"),
                  "Resolved receipts can be recomputed without checking out the result");
            git(p, {"reset", "--hard", merged});
            // Unsupported modify/delete content must not bypass the overlapping-text guard.
            git(p, {"checkout", "-b", "unsupported-peer", common});
            for (const auto &content : {std::string("\0binary", 7), std::string("\xff", 1),
                                        std::string(512 * 1024 + 1, 'x')}) {
                atomic_write(root / "delete-local.txt", content);
                auto unsupported = commit(p, "Unsupported modify/delete content");
                git(p, {"checkout", "main"});
                git(p, {"update-ref", "refs/remotes/cysynck/alice/main", unsupported});
                auto before_unsupported = hashes(root);
                auto q = query;
                q["resolutions"] = Json::array({{{"path", "delete-local.txt"}, {"side", "peer"}}});
                check(!request(q).at("can_integrate").get<bool>() &&
                          hashes(root) == before_unsupported &&
                          fs::is_empty(core.state() / "git-merge-work"),
                      "Binary, non-UTF-8 and oversized modify/delete conflicts preserve the source "
                      "and stay blocked");
                git(p, {"checkout", "unsupported-peer"});
            }
            git(p, {"checkout", "-b", "structural-peer", common});
            fs::remove(root / "conflict.txt");
            atomic_write(root / "conflict.txt/child", "directory replaces a file\n");
            auto structural = commit(p, "File directory conflict");
            git(p, {"checkout", "main"});
            git(p, {"update-ref", "refs/remotes/cysynck/alice/main", structural});
            auto before_structural = hashes(root);
            check(!request(query).at("can_integrate").get<bool>() &&
                      hashes(root) == before_structural,
                  "Structural and synthetic conflict paths cannot become ordinary whole-file "
                  "choices");
        }
        std::cout << "PASS " << checks
                  << " Git conflict resolution checks; fixtures: " << path_string(run) << '\n';
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what()
                  << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
