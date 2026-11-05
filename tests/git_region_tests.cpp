#include <cysynck/core.hpp>
#include "git_lfs.hpp"
#include "git_regions.hpp"
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
static std::string block(const std::string &local, const std::string &peer) {
    return "<<<<<<< LOCAL\n" + local + "||||||| ANCESTOR\nbase\n=======\n" + peer +
           ">>>>>>> PEER\n";
}
static void parser_cases() {
    Json draft{{"kind", "conflict"},
               {"conflicts", 1},
               {"path", "text.txt"},
               {"base", "local"},
               {"head", "peer"},
               {"merge_base", "ancestor"},
               {"local_ref", "refs/heads/main"},
               {"tracking_ref", "refs/remotes/cysynck/a/main"},
               {"texts", {{"local", "local\n"}, {"peer", "peer\n"}, {"ancestor", "base\n"}}},
               {"candidate", "prefix\r\n" + block("", "peer\r\n") + "last without newline"}};
    auto model = git_conflict_regions(draft);
    check(model.at("available") && model.at("count") == 1 &&
              compose_git_regions(model, Json::array({"local"})) ==
                  "prefix\r\nlast without newline",
          "Empty region selection preserves exact surrounding CRLF and missing final newline");
    check(compose_git_regions(model, Json::array({"both"})) ==
              "prefix\r\npeer\r\nlast without newline",
          "Both concatenates exact region bytes, including an empty local region");
    for (auto invalid : {Json(nullptr), Json::object(), Json::array(), Json::array({"ancestor"}),
                         Json::array({1}), Json::array({"local", "peer"}), Json::array({nullptr})})
        rejects([&] { compose_git_regions(model, invalid); }, "Invalid region choices accepted");
    auto marked = draft;
    marked["texts"]["local"] = "======= tutorial\n";
    check(git_conflict_regions(marked).at("reason") == "source_markers",
          "Source marker-like lines disable ambiguous region parsing");
    auto malformed = draft;
    malformed["candidate"] = "<<<<<<< LOCAL\n=======\n>>>>>>> PEER\n";
    check(!git_conflict_regions(malformed).at("available").get<bool>(),
          "Malformed marker order accepted");
    malformed["candidate"] = "<<<<<<< LOCAL\nlocal\n";
    check(!git_conflict_regions(malformed).at("available").get<bool>(), "Unclosed region accepted");
    malformed["conflicts"] = 2;
    malformed["candidate"] = draft.at("candidate");
    check(!git_conflict_regions(malformed).at("available").get<bool>(),
          "Conflict count mismatch accepted");
    std::string many;
    for (int i = 0; i < 256; ++i)
        many += block("local\n", "peer\n");
    draft["candidate"] = many;
    draft["conflicts"] = 127;
    check(git_conflict_regions(draft).at("count") == 256,
          "Git's capped conflict status lost region count");
    draft["candidate"] = many + block("local\n", "peer\n");
    check(git_conflict_regions(draft).at("reason") == "region_limit",
          "Region count bound not enforced");
    draft["conflicts"] = 1;
    draft["candidate"] = block(std::string(300000, 'l') + "\n", std::string(300000, 'p') + "\n");
    model = git_conflict_regions(draft);
    rejects([&] { compose_git_regions(model, Json::array({"both"})); },
            "Oversized composed text accepted");
    draft["candidate"] =
        "<<<<<<< LOCAL\r\nlocal\r\n||||||| ANCESTOR\r\nbase\r\n=======\r\npeer\r\n>>>>>>> PEER\r\n";
    check(compose_git_regions(git_conflict_regions(draft), Json::array({"local"})) == "local\r\n",
          "CRLF marker lines preserve CRLF content");
}
int main() {
    auto run = fs::current_path() / "git-region-data" / random_id();
    fs::create_directories(run);
    try {
        parser_cases();
        for (auto format : {"sha1", "sha256"}) {
            auto work = run / format, root = work / fs::u8path("repo été");
            fs::create_directories(root);
            Core core(work / "profile");
            auto p = core.request("POST", "/api/projects",
                                  {{"id", "team"},
                                   {"name", "Regions"},
                                   {"root", path_string(root)},
                                   {"mode", "git-bundles"},
                                   {"git_lfs", false},
                                   {"exchange_path", path_string(work / "exchange")},
                                   {"backup_interval_minutes", 0}});
            auto setup = cli(core, {"git", "setup", "team", "--object-format", format});
            cli(core, {"git", "setup", "team", "--object-format", format, "--apply", "true",
                       "--plan", setup.at("plan")});
            git(p, {"config", "core.autocrlf", "false"});
            git(p, {"config", "user.name", "Region Tester"});
            git(p, {"config", "user.email", "regions@example.invalid"});
            auto lines = [](bool local, bool peer, bool resolved = false) {
                std::string result;
                for (int i = 0; i < 36; ++i) {
                    if ((local || resolved) && (i == 0 || i == 8 || i == 20))
                        result += "local " + std::to_string(i) + " été\n";
                    else if ((peer || resolved) && (i == 8 || i == 20 || i == 35))
                        result += "peer " + std::to_string(i) + " été\n";
                    else
                        result += "line " + std::to_string(i) + "\n";
                    if (resolved && i == 20)
                        result += "peer 20 été\n";
                }
                return result;
            };
            atomic_write(root / "conflict.txt", lines(false, false));
            atomic_write(root / "end.txt", "start\r\nancestor without newline");
            atomic_write(root / "delete.txt", "base\n");
            git(p, {"add", "-A"});
            auto common = commit(p, "Common");
            atomic_write(root / "conflict.txt", lines(true, false));
            atomic_write(root / "end.txt", "start\r\nlocal without newline");
            atomic_write(root / "add.txt", "new local\n");
            fs::remove(root / "delete.txt");
            git(p, {"add", "-A"});
            auto local = commit(p, "Local");
            git(p, {"checkout", "-b", "peer", common});
            atomic_write(root / "conflict.txt", lines(false, true));
            atomic_write(root / "end.txt", "start\r\npeer without newline");
            atomic_write(root / "add.txt", "new peer\n");
            atomic_write(root / "delete.txt", "peer modification\n");
            git(p, {"add", "-A"});
            auto peer = commit(p, "Peer");
            git(p, {"checkout", "main"});
            git(p, {"update-ref", "refs/remotes/cysynck/alice/main", peer});
            atomic_write(root / "untracked.txt", "preserve me\n");
            auto before = hashes(root);
            auto query = Json{{"peer", "alice"},
                              {"branch", "main"},
                              {"base", local},
                              {"head", peer},
                              {"local_ref", "refs/heads/main"},
                              {"merge_base", common},
                              {"path", "conflict.txt"}};
            auto request = [&](const Json &data) {
                return core.request("POST", "/api/projects/team/git-merge-file", data);
            };
            auto draft = request(query);
            check(draft.at("regions").at("available") && draft.at("regions").at("count") == 2 &&
                      draft.at("composed_text").is_null(),
                  "Real committed file exposes two independently selectable regions");
            check(request(query).at("regions").at("token") == draft.at("regions").at("token"),
                  "Region token is stable");
            auto selected = query;
            selected["region_token"] = draft.at("regions").at("token");
            selected["region_choices"] = Json::array({"local", "both"});
            auto composed = request(selected);
            check(composed.at("composed_text") == lines(false, false, true),
                  "Region choices keep automatic changes on both sides and combine selected "
                  "conflicting lines");
            auto choices = work / fs::u8path("region choices été.json");
            atomic_write(choices, selected.at("region_choices").dump());
            check(cli(core,
                      {"git", "merge-file", "team", "alice", "main", "conflict.txt", "--regions",
                       path_string(choices), "--region-token", selected.at("region_token")})
                          .at("composed_text") == composed.at("composed_text"),
                  "CLI composes the same draft using a Unicode JSON choices path");
            auto forged = selected;
            forged["regions"] = {
                {"segments", Json::array({{{"kind", "context"}, {"text", "attacker"}}})}};
            check(request(forged).at("composed_text") == composed.at("composed_text"),
                  "Client-supplied proposal segments are never trusted");
            auto bad = selected;
            bad["region_token"] = std::string(64, '0');
            rejects([&] { request(bad); }, "Stale region token accepted");
            for (auto value : {Json::array({"local"}), Json::array({"local", "ancestor"}),
                               Json(nullptr), Json::array({"local", 5})}) {
                bad = selected;
                bad["region_choices"] = value;
                rejects([&] { request(bad); }, "Incomplete or invalid region choices accepted");
            }
            bad = selected;
            bad.erase("region_token");
            rejects([&] { request(bad); }, "Missing region token accepted");
            bad = selected;
            bad["region_choices"] = Json::array();
            for (int i = 0; i < 257; ++i)
                bad["region_choices"].push_back("local");
            rejects([&] { request(bad); }, "Oversized region choices array accepted");
            rejects([&] { core.request("POST", "/api/projects/team/git-merge-review", selected); },
                    "Overview accepted file region choices");
            rejects(
                [&] {
                    cli(core, {"git", "merge-file", "team", "alice", "main", "conflict.txt",
                               "--regions", path_string(choices)});
                },
                "CLI accepted missing region token");
            auto other = query;
            other["path"] = "add.txt";
            auto added = request(other);
            check(added.at("regions").at("available") && added.at("regions").at("count") == 1,
                  "Add/add exposes a region with an empty ancestor");
            other["region_token"] = selected.at("region_token");
            other["region_choices"] = Json::array({"both"});
            rejects([&] { request(other); }, "Region token reused across file paths");
            other["region_token"] = added.at("regions").at("token");
            auto added_text = request(other).at("composed_text");
            check(added_text == "new local\nnew peer\n", "Add/add both keeps local then peer");
            other = query;
            other["path"] = "end.txt";
            auto ending = request(other);
            other["region_token"] = ending.at("regions").at("token");
            other["region_choices"] = Json::array({"peer"});
            auto ending_text = request(other).at("composed_text").get<std::string>();
            check(ending_text.starts_with("start\r\n") &&
                      ending_text.find("peer without newline") != ending_text.npos &&
                      ending_text.find("<<<<<<<") == ending_text.npos,
                  "CRLF and unterminated input follow Git's explicit diff3 proposal bytes");
            other = query;
            other["path"] = "delete.txt";
            check(!request(other).at("regions").at("available").get<bool>(),
                  "Modify/delete keeps manual and whole-file resolution");
            check(hashes(root) == before && fs::is_empty(core.state() / "git-file-previews"),
                  "Region composition preserves source objects, index and working data and cleans "
                  "private copies");
            auto merge = query;
            merge.erase("path");
            merge["resolutions"] =
                Json::array({{{"path", "conflict.txt"},
                              {"side", "edited"},
                              {"content", composed.at("composed_text")}},
                             {{"path", "add.txt"}, {"side", "edited"}, {"content", added_text}},
                             {{"path", "end.txt"}, {"side", "edited"}, {"content", ending_text}},
                             {{"path", "delete.txt"}, {"side", "peer"}}});
            auto preview = core.request("POST", "/api/projects/team/git-merge", merge);
            check(preview.at("can_integrate") && hashes(root) == before,
                  "Composed draft goes through the existing reviewed edited-text plan");
            merge["apply"] = true;
            merge["plan"] = preview.at("plan");
            auto result = core.request("POST", "/api/projects/team/git-merge", merge);
            check(result.at("applied") &&
                      read_file(root / "conflict.txt") ==
                          composed.at("composed_text").get<std::string>() &&
                      read_file(root / "add.txt") == added_text.get<std::string>() &&
                      read_file(root / "end.txt") == ending_text,
                  "Real two-parent merge installs exact region-composed text");
            auto receipt = core.request("POST", "/api/projects/team/git-integration-check",
                                        {{"receipt", result.at("id")}});
            check(receipt.at("position") == "after" && receipt.at("errors").empty() &&
                      receipt.at("record").at("edited_files") == 3,
                  "Recovery receipt recomputes all three region-composed edits");
            rejects([&] { request(selected); },
                    "Old branch context still composes after integration");
        }
        std::cout << checks << " Git conflict-region checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\nFixture: " << path_string(run) << '\n';
        return 1;
    }
}
