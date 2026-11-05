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
static std::string pointer(const std::string &bytes) {
    return "version https://git-lfs.github.com/spec/v1\noid sha256:" + sha256(bytes) + "\nsize " +
           std::to_string(bytes.size()) + "\n";
}
int main() {
    auto run = fs::current_path() / "git-materialize-resume-data" / random_id();
    fs::create_directories(run);
    try {
        for (auto format : {"sha1", "sha256"}) {
            auto work = run / format, root = work / fs::u8path("repo été");
            fs::create_directories(root);
            Core core(work / "profile");
            auto p = core.request("POST", "/api/projects",
                                  {{"id", "team"},
                                   {"name", "Resume assets"},
                                   {"root", path_string(root)},
                                   {"mode", "git-bundles"},
                                   {"git_lfs", true},
                                   {"exchange_path", path_string(work / "exchange")},
                                   {"backup_interval_minutes", 0}});
            auto setup = cli(core, {"git", "setup", "team", "--object-format", format});
            cli(core, {"git", "setup", "team", "--object-format", format, "--apply", "true",
                       "--plan", setup.at("plan")});
            git(p, {"config", "core.autocrlf", "false"});
            std::string asset(128 * 1024 + 17, '\0');
            for (size_t i = 0; i < asset.size(); ++i)
                asset[i] = static_cast<char>(i * 37 + i / 31);
            auto text = pointer(asset), empty = pointer("");
            auto name = std::string("Selected/\xc3\xa9t\xc3\xa9 [1].uasset");
            atomic_write(root / ".gitattributes", "*.uasset filter=lfs diff=lfs merge=lfs -text\n");
            atomic_write(root / fs::u8path(name), text);
            atomic_write(root / "Selected/empty.uasset", empty);
            atomic_write(root / "outside.uasset", pointer("unavailable"));
            git(p, {"-c", "filter.lfs.process=", "-c", "filter.lfs.clean=", "-c",
                    "filter.lfs.required=false", "add", "-A"});
            git(p, {"-c", "user.name=Resume", "-c", "user.email=resume@example.invalid", "-c",
                    "commit.gpgsign=false", "commit", "-m", "Pointers"});
            for (auto key : {"process", "clean", "smudge"})
                git(p, {"config", std::string("filter.lfs.") + key, "cysynck-helper-must-not-run"});
            atomic_write(work / "asset", asset);
            atomic_write(work / "empty", "");
            auto store = lfs_local_store(p);
            copy_lfs_object(work / "asset", store, git_lfs_pointer(text));
            copy_lfs_object(work / "empty", store, git_lfs_pointer(empty));
            auto query = Json{{"paths", Json::array({"Selected/"})}};
            auto ready = core.request("POST", "/api/projects/team/git-materialize", query);
            query["apply"] = true;
            query["plan"] = ready.at("plan");
            auto complete = core.request("POST", "/api/projects/team/git-materialize", query);
            auto receipt = fs::u8path(complete.at("receipt_path").get<std::string>());
            auto recovery = root / fs::u8path(complete.at("recovery_directory").get<std::string>());
            auto target = root / fs::u8path(name), staged = recovery / "after" / fs::u8path(name);
            fs::create_directories(staged.parent_path());
            fs::rename(target, staged);
            auto original = recovery / "before" / fs::u8path(name);
            auto original_time = fs::last_write_time(original);
            auto original_permissions = fs::status(original).permissions();
            auto interrupted = complete;
            interrupted.update({{"status", "incomplete"},
                                {"applied", false},
                                {"phase", "publishing"},
                                {"files_written", 0},
                                {"error", "Simulated publication interruption"}});
            auto save = [&](const Json &r) { atomic_write(receipt, r.dump(), true); };
            save(interrupted);
            auto input = Json{{"receipt", complete.at("id")}};
            auto preview = [&] {
                return core.request("POST", "/api/projects/team/git-materialization-resume", input);
            };
            auto apply = [&](const Json &r) {
                auto q = input;
                q["apply"] = true;
                q["plan"] = r.at("plan");
                return core.request("POST", "/api/projects/team/git-materialization-resume", q);
            };
            auto inspected = preview();
            check(inspected.at("can_resume") && inspected.at("already_published") == 1 &&
                      inspected.at("pending_publications") == 1,
                  "Mixed published and interrupted files did not offer resume");
            check(!inspected.at("check").at("can_acknowledge").get<bool>() && !fs::exists(target) &&
                      read_file(original) == text,
                  "Interrupted preview wrote files or allowed acknowledgement");
            check(cli(core, {"git", "materialization-resume", "team", complete.at("id")})
                          .at("plan") == inspected.at("plan"),
                  "CLI and API resume plans differ");
            rejects(
                [&] {
                    auto q = input;
                    q["apply"] = true;
                    core.request("POST", "/api/projects/team/git-materialization-resume", q);
                },
                "Resume without a plan accepted");
            rejects(
                [&] {
                    cli(core, {"git", "materialization-resume", "team", complete.at("id"),
                               "--apply", "yes"});
                },
                "Invalid CLI boolean accepted");
            fs::remove(lfs_object_path(store, sha256(asset)));
            fs::remove(lfs_object_path(store, sha256("")));
            check(preview().at("plan") == inspected.at("plan"),
                  "Verified staging resume depended on the LFS cache");
            atomic_write(staged, std::string(asset.size(), 'x'));
            check(!preview().at("can_resume").get<bool>(), "Same-size staged corruption accepted");
            rejects([&] { apply(inspected); }, "Stale corrupted staging plan accepted");
            atomic_write(staged, asset);
            inspected = preview();
            atomic_write(target, "new local destination");
            check(!preview().at("can_resume").get<bool>(),
                  "Appeared local destination was ignored");
            rejects([&] { apply(inspected); }, "Resume overwrote an appeared destination");
            check(read_file(target) == "new local destination",
                  "Refused resume changed local destination bytes");
            fs::remove(target);
            atomic_write(original, "changed preserved pointer");
            check(!preview().at("can_resume").get<bool>(), "Changed original pointer accepted");
            atomic_write(original, text);
            fs::permissions(original, original_permissions);
            fs::last_write_time(original, original_time);
            atomic_write(recovery / "unknown", "preserve");
            check(!preview().at("can_resume").get<bool>() &&
                      read_file(recovery / "unknown") == "preserve",
                  "Unexpected recovery entry was removed or ignored");
            fs::remove(recovery / "unknown");
            auto index_bytes = read_file(root / ".git/index");
            git(p, {"update-index", "--assume-unchanged", "Selected/empty.uasset"});
            check(!preview().at("can_resume").get<bool>(), "Changed index flags accepted");
            atomic_write(root / ".git/index", index_bytes);
            fs::create_directory(root / ".git/cysynck-integration.lock");
            check(!preview().at("can_resume").get<bool>(), "Existing integration lock ignored");
            fs::remove(root / ".git/cysynck-integration.lock");
            auto bad = interrupted;
            bad["acknowledged_at"] = timestamp();
            bad["acknowledgement"] = Json::object();
            save(bad);
            check(!preview().at("can_resume").get<bool>(), "Acknowledged receipt offered resume");
            bad = interrupted;
            bad["resume"] = {
                {"attempt", size_t{1000}}, {"started", timestamp()}, {"status", "incomplete"}};
            save(bad);
            check(!preview().at("can_resume").get<bool>(), "Resume attempt bound ignored");
            bad["resume"]["attempt"] = -1;
            save(bad);
            rejects(preview, "Invalid resume metadata accepted");
            auto legacy = interrupted;
            for (auto key : {"selection", "excluded", "project_hash"})
                legacy.erase(key);
            save(legacy);
            check(preview().at("can_resume"), "Legacy receipt cannot resume verified staging");
            save(interrupted);
            inspected = preview();
            core.request("PUT", "/api/projects/team", {{"name", "Renamed recovery"}});
            rejects([&] { apply(inspected); }, "Project settings did not invalidate resume token");
            inspected = preview();
            atomic_write(root / "outside.uasset", "unselected local edit");
            check(preview().at("plan") == inspected.at("plan"),
                  "Unselected working edit invalidated the recovery plan");
            auto head = git(p, {"rev-parse", "HEAD"}), config = sha256_file(root / ".git/config");
            auto empty_time = fs::last_write_time(root / "Selected/empty.uasset");
            auto result = cli(core, {"git", "materialization-resume", "team", complete.at("id"),
                                     "--apply", "true", "--plan", inspected.at("plan")});
            check(result.at("resumed") && result.at("status") == "complete" &&
                      result.at("files_written") == 2 &&
                      result.at("resume").at("status") == "complete",
                  "Resumed receipt did not record completion");
            check(read_file(target, asset.size() + 1) == asset && !fs::exists(staged) &&
                      fs::file_size(root / "Selected/empty.uasset") == 0 &&
                      fs::last_write_time(root / "Selected/empty.uasset") == empty_time,
                  "Resume changed already published bytes or failed to install staging");
            check(read_file(original) == text && fs::last_write_time(original) == original_time &&
                      read_file(root / "outside.uasset") == "unselected local edit",
                  "Resume changed original pointers or unselected files");
            check(read_file(root / ".git/index") == index_bytes &&
                      git(p, {"rev-parse", "HEAD"}) == head &&
                      sha256_file(root / ".git/config") == config,
                  "Resume changed Git metadata");
            auto saved = Json::parse(read_file(receipt, 16 * 1024 * 1024));
            check(saved.at("error") == interrupted.at("error") &&
                      saved.at("resume").at("attempt") == 1 &&
                      saved.at("selection") == complete.at("selection"),
                  "Resume lost original interruption evidence or scope");
            rejects([&] { apply(inspected); }, "Completed resume token replay accepted");
            auto checked =
                core.request("POST", "/api/projects/team/git-materialization-check", input);
            check(checked.at("position") == "after" && checked.at("errors").empty() &&
                      !checked.at("can_resume").get<bool>(),
                  "Completed resume does not verify");
            auto journal_only = saved;
            journal_only.update(
                {{"status", "incomplete"}, {"applied", false}, {"files_written", 0}});
            journal_only["resume"]["status"] = "incomplete";
            journal_only["resume"].erase("completed");
            save(journal_only);
            auto unchanged = sha256_file(target);
            auto target_time = fs::last_write_time(target);
            inspected = preview();
            check(inspected.at("can_resume") && inspected.at("pending_publications") == 0 &&
                      inspected.at("already_published") == 2,
                  "Post-publication interruption cannot be finalized");
            result = apply(inspected);
            check(result.at("resume").at("attempt") == 2 && sha256_file(target) == unchanged &&
                      fs::last_write_time(target) == target_time,
                  "Journal-only completion rewrote an asset");
            auto history = cli(core, {"git", "materializations", "team"});
            check(history.at("records")[0].at("resume").at("status") == "complete" &&
                      !history.at("records")[0].at("needs_attention").get<bool>(),
                  "History does not disclose resumed completion");
        }
        std::cout << "PASS " << checks << " LFS publication resume checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\nFixture: " << path_string(run) << '\n';
        return 1;
    }
}
