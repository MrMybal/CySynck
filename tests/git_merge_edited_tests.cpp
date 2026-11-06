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
    git(p, {"-c", "commit.gpgsign=false", "-c",
            "core.hooksPath="
#ifdef _WIN32
            "NUL",
#else
            "/dev/null",
#endif
            "commit", "-m", message});
    return trim(git(p, {"rev-parse", "HEAD"}));
}
int main() {
    auto run = fs::current_path() / "git-merge-edited-data" / random_id();
    fs::create_directories(run);
    try {
        for (auto format : {"sha1", "sha256"}) {
            auto work = run / format, root = work / fs::u8path("repo été");
            fs::create_directories(root);
            Core core(work / "profile");
            auto p = core.request("POST", "/api/projects",
                                  {{"id", "team"},
                                   {"name", "Edited merge"},
                                   {"root", path_string(root)},
                                   {"mode", "git-bundles"},
                                   {"git_lfs", false},
                                   {"exchange_path", path_string(work / "exchange")},
                                   {"backup_interval_minutes", 0}});
            auto setup = cli(core, {"git", "setup", "team", "--object-format", format});
            cli(core, {"git", "setup", "team", "--object-format", format, "--apply", "true",
                       "--plan", setup.at("plan")});
            git(p, {"config", "core.autocrlf", "false"});
            git(p, {"config", "user.name", "Edited Merge Tester"});
            git(p, {"config", "user.email", "edited@example.invalid"});
            atomic_write(root / "conflict.txt", "ancestor\n");
            atomic_write(root / "delete.txt", "ancestor\n");
            git(p, {"add", "-A"});
            auto common = commit(p, "Common");
            atomic_write(root / "conflict.txt", "local\n");
            fs::remove(root / "delete.txt");
            git(p, {"add", "-A"});
            auto local = commit(p, "Local");
            git(p, {"checkout", "-b", "peer", common});
            atomic_write(root / "conflict.txt", "peer\n");
            atomic_write(root / "delete.txt", "peer modification\n");
            git(p, {"add", "-A"});
            auto peer = commit(p, "Peer");
            git(p, {"checkout", "main"});
            git(p, {"update-ref", "refs/remotes/cysynck/alice/main", peer});
            atomic_write(root / "keep.txt", "local untracked\n");
            atomic_write(root / "Build/cache", std::string("ignored\0bytes", 13));
            atomic_write(root / ".git/info/exclude", "Build/\n");
            auto query = Json{{"peer", "alice"}, {"branch", "main"}};
            auto request = [&](const Json &q) {
                return core.request("POST", "/api/projects/team/git-merge", q);
            };
            auto initial = request(query);
            check(initial.at("conflict_files").size() == 2 &&
                      initial.at("conflict_files")[0].at("editable") &&
                      initial.at("conflict_files")[1].at("editable"),
                  "Text and modify/delete conflicts expose manual editing");
            auto draft = cli(core, {"git", "merge-file", "team", "alice", "main", "conflict.txt"});
            check(draft.at("kind") == "conflict" &&
                      draft.at("candidate").get<std::string>().find("<<<<<<<") != std::string::npos,
                  "Editor seed includes committed versions and labelled proposal markers");
            auto baseline = hashes(root);
            auto selected = query;
            std::string text = "local + peer été\r\nfinal without newline";
            selected["resolutions"] =
                Json::array({{{"path", "conflict.txt"}, {"side", "edited"}, {"content", text}}});
            auto partial = request(selected);
            check(!partial.at("can_integrate").get<bool>() &&
                      partial.at("unresolved_conflicts") == Json::array({"delete.txt"}),
                  "An edited file does not implicitly resolve other conflicts");
            selected["resolutions"].push_back(
                {{"path", "delete.txt"}, {"side", "edited"}, {"content", ""}});
            auto ready = request(selected);
            check(ready.at("can_integrate") && ready.at("result_ready") &&
                      ready.at("files_after").size() == 2,
                  "Edited modify/delete text can explicitly create an empty regular file");
            auto expected = trim(offline_git(p, {"hash-object", "--no-filters", "--stdin"}, text));
            bool exact = false;
            for (const auto &file : ready.at("files_after"))
                if (file.at("path") == "conflict.txt")
                    exact = file.at("oid") == expected && file.at("bytes") == text.size();
            check(
                exact && hashes(root) == baseline && fs::is_empty(core.state() / "git-merge-work"),
                "Edited previews hash exact UTF-8/CRLF bytes only in a cleaned private workspace");
            auto choices = work / fs::u8path("edited choices été.json");
            atomic_write(choices, selected.at("resolutions").dump());
            check(cli(core, {"git", "merge", "team", "alice", "main", "--resolutions",
                             path_string(choices)})
                          .at("plan") == ready.at("plan"),
                  "CLI JSON edited content shares the stable API plan");
            auto altered = selected;
            altered["resolutions"][0]["content"] = text + "!";
            auto changed = request(altered);
            check(changed.at("can_integrate") && changed.at("plan") != ready.at("plan") &&
                      changed.at("result_tree") != ready.at("result_tree"),
                  "Any edited byte changes the complete result tree and token");
            altered.update({{"apply", true}, {"plan", ready.at("plan")}});
            rejects([&] { request(altered); },
                    "Edited content cannot be applied with the previous text's token");
            for (const auto &invalid :
                 {Json(nullptr), Json(17), Json(std::string(512 * 1024 + 1, 'x')),
                  Json(std::string("bad\0text", 8)), Json(std::string("\xff", 1)),
                  Json("<<<<<<< LOCAL\ntext\n=======\npeer\n>>>>>>> PEER\n"),
                  Json("\t||||||| ANCESTOR\r\n"), Json(" ========\n"), Json("  >>>>>>> PEER"),
                  Json("version https://git-lfs.github.com/spec/v1\noid sha256:" +
                       std::string(64, 'a') + "\nsize 1\n")}) {
                auto bad = selected;
                bad["resolutions"][0]["content"] = invalid;
                rejects([&] { request(bad); },
                        "Invalid text, leftover conflict markers and LFS pointers are refused");
            }
            auto missing = selected;
            missing["resolutions"][0].erase("content");
            rejects([&] { request(missing); },
                    "Edited resolutions require explicit string content");
            auto extra = selected;
            extra["resolutions"][0]["mode"] = "100755";
            rejects([&] { request(extra); },
                    "Edited resolutions cannot smuggle permissions or arbitrary fields");
            check(hashes(root) == baseline,
                  "Invalid and stale edited requests preserve every original repository byte");
            auto apply = selected;
            apply.update({{"apply", true}, {"plan", ready.at("plan")}});
            auto done = request(apply);
            auto merged = done.at("result_commit").get<std::string>();
            check(done.at("applied") && read_file(root / "conflict.txt") == text &&
                      fs::file_size(root / "delete.txt") == 0,
                  "Application installs the exact edited bytes, including CRLF and an empty file");
            check(trim(git(p, {"rev-list", "--parents", "-n", "1", "HEAD"})) ==
                          merged + " " + local + " " + peer &&
                      trim(git(p, {"rev-parse", "HEAD^{tree}"})) ==
                          ready.at("result_tree").get<std::string>(),
                  "Edited integration retains ordered parents and the reviewed tree");
            check(read_file(root / "keep.txt") == "local untracked\n" &&
                      read_file(root / "Build/cache") == std::string("ignored\0bytes", 13) &&
                      trim(git(p, {"status", "--porcelain=v1", "--untracked-files=no"})).empty(),
                  "Edited integration preserves unrelated local files and a clean tracked index");
            auto receipt = fs::u8path(done.at("receipt_path").get<std::string>());
            auto record = Json::parse(read_file(receipt));
            check(record.at("resolutions") == ready.at("resolutions"),
                  "Private receipts retain exact edited text for independent recomputation");
            auto checked = cli(core, {"git", "integration-check", "team", done.at("id")});
            check(checked.at("position") == "after" && checked.at("tracked_matches") &&
                      checked.at("errors").empty(),
                  "Receipt checks regenerate edited blobs and verify the actual merge");
            check(cli(core, {"git", "integrations", "team"}).at("records")[0].at("edited_files") ==
                      2,
                  "Receipt summaries count edited files without disclosing their text");
            auto tampered = record;
            tampered["resolutions"][0]["content"] = "different text";
            atomic_write(receipt, tampered.dump(2), true);
            check(!cli(core, {"git", "integration-check", "team", done.at("id")})
                       .at("errors")
                       .empty(),
                  "Tampered receipt text cannot verify the original merge tree");
            record["status"] = "incomplete";
            record["applied"] = false;
            atomic_write(receipt, record.dump(2), true);
            git(p, {"reset", "--hard", local}); // Disposable fixture only.
            auto before = cli(core, {"git", "integration-check", "team", done.at("id")});
            check(before.at("position") == "before" && before.at("can_acknowledge"),
                  "Interrupted edited receipts also recompute the original repository state");
            git(p, {"reset", "--hard", merged});
            checked = cli(core, {"git", "integration-check", "team", done.at("id")});
            auto before_ack = hashes(root);
            cli(core, {"git", "integration-acknowledge", "team", done.at("id"), "--plan",
                       checked.at("plan")});
            check(hashes(root) == before_ack && fs::is_empty(core.state() / "git-merge-work"),
                  "Acknowledgement changes no repository data and leaves no private workspace");
            // A permission disagreement cannot be hidden by supplying text.
            git(p, {"checkout", "-b", "permission-peer", common});
            atomic_write(root / "conflict.txt", "permission peer\n");
            git(p, {"add", "conflict.txt"});
            git(p, {"update-index", "--chmod=+x", "--", "conflict.txt"});
#ifndef _WIN32
            fs::permissions(root / "conflict.txt",
                            fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                            fs::perm_options::add);
#endif
            auto permissions = commit(p, "Executable peer");
            git(p, {"checkout", "main"});
            git(p, {"update-ref", "refs/remotes/cysynck/alice/main", permissions});
            auto ordinary = request(query);
            check(!ordinary.at("conflict_files")[0].at("editable").get<bool>(),
                  "Conflicting file permissions disable the text editor");
            auto bad_mode = query;
            bad_mode["resolutions"] = Json::array(
                {{{"path", "conflict.txt"}, {"side", "edited"}, {"content", "resolved"}}});
            check(!request(bad_mode).at("can_integrate").get<bool>(),
                  "Edited text cannot bypass a committed file-mode disagreement");
        }
        std::cout << "PASS " << checks << " edited Git merge checks; fixtures: " << path_string(run)
                  << '\n';
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what()
                  << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
