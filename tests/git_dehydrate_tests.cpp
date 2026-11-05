#include <cysynck/core.hpp>
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
static std::string pointer(const std::string &bytes) {
    return "version https://git-lfs.github.com/spec/v1\noid sha256:" + sha256(bytes) + "\nsize " +
           std::to_string(bytes.size()) + "\n";
}
int main() {
    auto run = fs::current_path() / "git-dehydrate-data" / random_id();
    try {
        for (auto format : {"sha1", "sha256"}) {
            auto work = run / format, root = work / fs::u8path("repo été");
            fs::create_directories(root);
            Core core(work / "profile");
            auto p = core.request("POST", "/api/projects",
                                  {{"id", "team"},
                                   {"name", "LFS pointers"},
                                   {"root", path_string(root)},
                                   {"mode", "git-bundles"},
                                   {"exchange_path", path_string(work / "exchange")},
                                   {"backup_interval_minutes", 0}});
            git(p, {"init", "--initial-branch=main", std::string("--object-format=") + format});
            git(p, {"config", "core.autocrlf", "false"});
            for (auto key : {"process", "clean", "smudge"})
                git(p, {"config", std::string("filter.lfs.") + key, ""});
            git(p, {"config", "filter.lfs.required", "false"});
            auto commit = [&] {
                git(p, {"-c", "filter.lfs.process=", "-c", "filter.lfs.clean=", "-c",
                        "filter.lfs.required=false", "add", "-A"});
                git(p, {"-c", "filter.lfs.process=", "-c", "filter.lfs.clean=", "-c",
                        "filter.lfs.required=false", "-c", "user.name=Fixture", "-c",
                        "user.email=fixture@example.invalid", "-c", "commit.gpgsign=false",
                        "commit", "-m", "Pointers"});
                return git(p, {"rev-parse", "HEAD"});
            };
            std::string asset(128 * 1024 + 17, '\0');
            for (size_t i = 0; i < asset.size(); ++i)
                asset[i] = static_cast<char>(i * 31);
            auto text = pointer(asset), empty = pointer("");
            auto name = std::string("Assets/\xc3\xa9t\xc3\xa9 [1].uasset");
            auto target = root / fs::u8path(name), zero = root / "Assets/empty.uasset";
            atomic_write(root / ".gitattributes", "*.uasset filter=lfs diff=lfs merge=lfs -text\n");
            atomic_write(target, text);
            atomic_write(zero, empty);
            atomic_write(root / "Assets/pointer.uasset", pointer("uncached"));
            atomic_write(root / "outside.uasset", pointer("outside"));
            atomic_write(root / "ordinary.txt", "base\n");
            auto base = commit();
            atomic_write(root / "ordinary.txt", "next\n");
            auto next = commit();
            git(p, {"update-ref", "refs/remotes/cysynck/peer-a/main", next});
            git(p, {"-c", "filter.lfs.process=", "-c", "filter.lfs.smudge=", "-c",
                    "filter.lfs.required=false", "reset", "--hard", base});
            for (auto key : {"process", "clean", "smudge"})
                git(p, {"config", std::string("filter.lfs.") + key, "cysynck-forbidden-helper"});
            git(p, {"config", "filter.lfs.required", "true"});
            git(p, {"config", "lfs.storage", path_string(work / "unapproved-cache")});
            atomic_write(target, asset);
            atomic_write(zero, "");
            auto stamp = fs::last_write_time(target);
            auto perms = fs::status(target).permissions();
            auto index = read_file(root / ".git/index"), config = read_file(root / ".git/config");
            auto outside = read_file(root / "outside.uasset");
            if (fs::exists(root / ".git/lfs") || fs::exists(root / ".cysynck-lfs"))
                throw std::runtime_error("Git fixture created cache or recovery before preview");
            Json query{{"operation", "dehydrate"}, {"paths", Json::array({"Assets/"})}};
            auto preview = [&] {
                return core.request("POST", "/api/projects/team/git-materialize", query);
            };
            auto apply = [&](const Json &r) {
                auto q = query;
                q["apply"] = true;
                q["plan"] = r.at("plan");
                return core.request("POST", "/api/projects/team/git-materialize", q);
            };
            auto ready = preview();
            check(ready.at("can_dehydrate") && !ready.at("can_materialize").get<bool>() &&
                      ready.at("pending") == 2 && ready.at("pointers") == 1 &&
                      ready.at("excluded") == 1,
                  "Dehydration selection/counts incorrect");
            check(ready.at("lfs_storage").is_null() && !fs::exists(root / ".git/lfs") &&
                      !fs::exists(root / ".cysynck-lfs"),
                  "Preview touched cache or recovery storage");
            check(read_file(target, asset.size() + 1) == asset && read_file(zero).empty() &&
                      read_file(root / ".git/index") == index,
                  "Preview modified working files or index");
            rejects(
                [&] {
                    auto q = query;
                    q["operation"] = "invalid";
                    core.request("POST", "/api/projects/team/git-materialize", q);
                },
                "Invalid operation accepted");
            rejects(
                [&] {
                    auto q = query;
                    q["apply"] = true;
                    core.request("POST", "/api/projects/team/git-materialize", q);
                },
                "Unplanned dehydration accepted");
            rejects([&] { cli(core, {"git", "dehydrate", "team", "--apply", "yes"}); },
                    "Invalid CLI boolean accepted");
            rejects(
                [&] {
                    auto q = query;
                    q.erase("operation");
                    q["apply"] = true;
                    q["plan"] = ready.at("plan");
                    core.request("POST", "/api/projects/team/git-materialize", q);
                },
                "Operation not bound to plan");
            atomic_write(target, std::string(asset.size(), 'x'));
            fs::last_write_time(target, stamp);
            check(preview().at("pending") == 1 && preview().at("skipped") == 1,
                  "Modified same-size asset was not preserved");
            rejects([&] { apply(ready); }, "Changed asset did not stale plan");
            check(read_file(target, asset.size() + 1) == std::string(asset.size(), 'x'),
                  "Refusal changed modified data");
            atomic_write(target, asset);
            fs::last_write_time(target, stamp);
            fs::permissions(target, fs::perms::owner_read);
            check(preview().at("pending") == 1 && preview().at("skipped") == 1,
                  "Read-only source was not preserved");
            fs::permissions(target, perms);
            fs::create_directory(root / ".git/cysynck-integration.lock");
            rejects([&] { apply(preview()); }, "Existing integration lock ignored");
            fs::remove(root / ".git/cysynck-integration.lock");
            ready = preview();
            core.request("PUT", "/api/projects/team", {{"name", "Renamed pointers"}});
            rejects([&] { apply(ready); }, "Project settings did not stale plan");
            auto paths = work / "selection.json";
            atomic_write(paths, "[\"Assets/\"]");
            ready = cli(core, {"git", "dehydrate", "team", "--paths-file", path_string(paths)});
            check(ready.at("plan") == preview().at("plan"), "CLI/API dehydration plans differ");
            auto done = cli(core, {"git", "dehydrate", "team", "--paths-file", path_string(paths),
                                   "--apply", "true", "--plan", ready.at("plan")});
            check(done.at("applied") && done.at("schema") == 2 &&
                      done.at("operation") == "dehydrate" && done.at("files_written") == 2,
                  "Dehydration did not complete");
            auto recovery = root / fs::u8path(done.at("recovery_directory").get<std::string>());
            auto saved = recovery / "before" / fs::u8path(name);
            check(read_file(target) == text && read_file(zero) == empty &&
                      read_file(saved, asset.size() + 1) == asset &&
                      fs::file_size(recovery / "before/Assets/empty.uasset") == 0,
                  "Pointers or preserved asset bytes differ");
            check(fs::last_write_time(saved) == stamp && fs::status(saved).permissions() == perms,
                  "Preserved asset metadata changed");
            check(read_file(root / ".git/index") == index &&
                      read_file(root / ".git/config") == config &&
                      git(p, {"rev-parse", "HEAD"}) == base &&
                      read_file(root / "outside.uasset") == outside,
                  "Git metadata or unselected files changed");
            check(!fs::exists(root / ".git/lfs") && !fs::exists(work / "unapproved-cache"),
                  "Dehydration used or created LFS storage");
            rejects([&] { apply(ready); }, "Dehydration plan replay accepted");
            auto input = Json{{"receipt", done.at("id")}};
            auto inspect = [&] {
                return core.request("POST", "/api/projects/team/git-materialization-check", input);
            };
            check(inspect().at("position") == "after" && inspect().at("errors").empty(),
                  "Dehydration receipt did not verify");
            check(cli(core, {"git", "materializations", "team"}).at("records")[0].at("operation") ==
                      "dehydrate",
                  "Catalog lost operation identity");
            auto receipt = fs::u8path(done.at("receipt_path").get<std::string>());
            auto stopped = done;
            stopped.update({{"status", "incomplete"},
                            {"applied", false},
                            {"files_written", 0},
                            {"error", "Simulated interruption"}});
            atomic_write(receipt, stopped.dump());
            auto checked = inspect();
            check(checked.at("can_acknowledge") && !checked.at("can_resume").get<bool>(),
                  "Interrupted dehydration has unsafe recovery actions");
            check(!cli(core, {"git", "materialization-resume", "team", done.at("id")})
                       .at("can_resume")
                       .get<bool>(),
                  "Publication resume accepted inverse receipt");
            auto staged = recovery / "after" / fs::u8path(name);
            fs::rename(target, staged);
            checked = inspect();
            check(checked.at("position") == "interrupted" &&
                      !checked.at("can_acknowledge").get<bool>() &&
                      !checked.at("can_resume").get<bool>(),
                  "Interrupted inverse publication offered an unsafe action");
            atomic_write(staged, "damaged staged pointer");
            check(!inspect().at("errors").empty() && read_file(saved, asset.size() + 1) == asset,
                  "Changed staged pointer ignored or preserved asset damaged");
            atomic_write(staged, text);
            fs::rename(staged, target);
            atomic_write(saved, std::string(asset.size(), 'x'));
            check(!inspect().at("errors").empty(), "Changed preserved asset was accepted");
            atomic_write(saved, asset);
            fs::permissions(saved, perms);
            fs::last_write_time(saved, stamp);
            atomic_write(recovery / "unknown", "preserve");
            check(!inspect().at("errors").empty() && read_file(recovery / "unknown") == "preserve",
                  "Unknown recovery entry ignored or removed");
            fs::remove(recovery / "unknown");
            auto tampered = stopped;
            tampered["files"][0]["asset"]["sha256"] = std::string(64, '0');
            atomic_write(receipt, tampered.dump());
            rejects(inspect, "Forged inverse receipt asset accepted");
            atomic_write(receipt, stopped.dump());
            checked = inspect();
            auto ack = core.request("POST", "/api/projects/team/git-materialization-acknowledge",
                                    {{"receipt", done.at("id")}, {"plan", checked.at("plan")}});
            check(ack.at("acknowledged") && ack.at("status") == "incomplete" &&
                      ack.at("operation") == "dehydrate",
                  "Acknowledgement lost inverse history");
            auto integration =
                Json{{"peer", "peer-a"}, {"branch", "main"}, {"lfs_policy", "pointers"}};
            auto next_preview =
                core.request("POST", "/api/projects/team/git-integrate", integration);
            check(next_preview.at("can_integrate"),
                  "Restored pointers did not enable Git integration");
            integration["apply"] = true;
            integration["plan"] = next_preview.at("plan");
            auto integrated = core.request("POST", "/api/projects/team/git-integrate", integration);
            check(integrated.at("applied") && git(p, {"rev-parse", "HEAD"}) == next &&
                      read_file(saved, asset.size() + 1) == asset &&
                      read_file(root / "ordinary.txt") == "next\n",
                  "Git integration lost preserved assets");
        }
        std::cout << "PASS " << checks << " LFS dehydration checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\nFixture: " << path_string(run) << '\n';
        return 1;
    }
}
