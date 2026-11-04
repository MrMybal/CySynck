#include <cysynck/core.hpp>
#include <atomic>
#include <functional>
#include <iostream>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

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
static Json act(Core &core, const std::string &action, const Json &data = Json::object()) {
    return core.request("POST", "/api/projects/team/" + action, data);
}
static std::string copy_name(const std::string &stem) {
    return stem + ".sync-conflict-20261104-120000-ABCDEFG.txt";
}
static Json request(const std::string &stem, const std::string &choice) {
    return {{"path", copy_name(stem)}, {"choice", choice}};
}
static Json apply(Core &core, Json data) {
    data["plan"] = act(core, "resolution-plan", data).at("plan");
    return act(core, "resolve-conflict", data);
}
int main() {
    auto run = fs::current_path() / "resolution-test-data" / random_id();
    auto root = run / fs::u8path("Projet été");
    fs::create_directories(root);
    try {
        Core core(run / "state");
        core.request("POST", "/api/projects",
                     {{"id", "team"},
                      {"name", "Team"},
                      {"mode", "backup"},
                      {"root", path_string(root)},
                      {"backup_interval_minutes", 0}});
        atomic_write(root / "notes.txt", "current\n");
        atomic_write(root / copy_name("notes"), "conflict\n");
        auto input = request("notes", "conflict");
        auto reviewed = act(core, "conflict-diff", {{"path", copy_name("notes")}});
        auto reviewed_input = input;
        reviewed_input["expected_current"] = reviewed.at("before_info");
        reviewed_input["expected_conflict"] = reviewed.at("after_info");
        auto browser_input = reviewed_input;
        for (const auto *field : {"expected_current", "expected_conflict"})
            browser_input[field]["modified_ns"] =
                static_cast<double>(browser_input[field]["modified_ns"].get<int64_t>());
        check(act(core, "resolution-plan", browser_input).contains("plan"),
              "Exact timestamp text survives a JavaScript numeric round trip");
        browser_input["expected_current"]["modified_ns_exact"] = "0";
        rejects([&] { act(core, "resolution-plan", browser_input); },
                "Exact timestamp mismatches still refuse preparation");
        auto plan = act(core, "resolution-plan", input);
        check(plan.at("requires_pause") && plan.at("result").at("sha256") == sha256("conflict\n"),
              "Preview identifies result and requires pause");
        check(!fs::exists(root / ".cysynck-recovery") && !fs::exists(core.state() / "resolutions"),
              "Preview creates no recovery data or directories");
        input["plan"] = plan.at("plan");
        rejects([&] { act(core, "resolve-conflict", input); },
                "Unpaused project refuses resolution");
        act(core, "pause", {{"paused", true}});
        auto noPlan = request("notes", "conflict");
        rejects([&] { act(core, "resolve-conflict", noPlan); },
                "Resolution requires a concrete preview token");
        auto time = fs::last_write_time(root / "notes.txt");
        atomic_write(root / "notes.txt", "changed\n");
        fs::last_write_time(root / "notes.txt", time);
        rejects([&] { act(core, "resolution-plan", reviewed_input); },
                "Preparation refuses files changed since the displayed comparison");
        rejects([&] { act(core, "resolve-conflict", input); },
                "Content changes invalidate preview even when size and time match");
        check(read_file(root / "notes.txt") == "changed\n" && fs::exists(root / copy_name("notes")),
              "Stale plan modifies neither source");
        auto done = apply(core, request("notes", "conflict"));
        check(done.at("phase") == "applied" && read_file(root / "notes.txt") == "conflict\n",
              "Conflict choice publishes exact selected bytes");
        check(!fs::exists(root / copy_name("notes")), "Resolved conflict leaves the active folder");
        auto recovery = root / fs::u8path(done.at("recovery_directory").get<std::string>());
        check(read_file(recovery / "current") == "changed\n" &&
                  read_file(recovery / "conflict") == "conflict\n",
              "Both actual original files remain in recovery");
        auto directory = core.state() / "resolutions/team" / done.at("id").get<std::string>();
        check(read_file(directory / "current.bin") == "changed\n" &&
                  read_file(directory / "conflict.bin") == "conflict\n",
              "Private copies preserve both reviewed versions independently");
        check(core.request("GET", "/api/overview").at("projects").at(0).at("paused"),
              "Resolution never resumes a project automatically");
        check(act(core, "conflicts").at("files").empty(),
              "Recovery files never appear as live conflicts");
        check(act(core, "backup").at("file_count") == 1,
              "Recovery data is excluded from ordinary backups in existing profiles");
        core.request("PUT", "/api/projects/team", {{"excludes", {"*.sync-conflict-*"}}});
        check(act(core, "backup").at("file_count") == 1,
              "Private recovery exclusion cannot be disabled by custom project globs");
        auto exported = run / "exported";
        act(core, "resolution-export",
            {{"resolution", done.at("id")}, {"destination", path_string(exported)}});
        check(read_file(exported / "current/notes.txt") == "changed\n" &&
                  read_file(exported / "conflict" / copy_name("notes")) == "conflict\n",
              "Export restores both versions to a new separate directory");
        rejects(
            [&] {
                act(core, "resolution-export",
                    {{"resolution", done.at("id")}, {"destination", path_string(root / "bad")}});
            },
            "Export never enters a working project");
        rejects(
            [&] {
                act(core, "resolution-export",
                    {{"resolution", done.at("id")}, {"destination", path_string(exported)}});
            },
            "Export refuses existing destination");
        atomic_write(directory / "conflict.bin", "damaged");
        rejects(
            [&] {
                act(core, "resolution-export",
                    {{"resolution", done.at("id")},
                     {"destination", path_string(run / "corrupt-export")}});
            },
            "Corrupt private backup blocks export");
        check(!fs::exists(run / "corrupt-export"),
              "Integrity failure creates no export destination");
        atomic_write(directory / "conflict.bin", "conflict\n");
        rejects([&] { act(core, "resolve-conflict", input); },
                "Consumed plan cannot reapply a removed conflict");
        atomic_write(root / "keep.txt", "keep these bytes");
        atomic_write(root / copy_name("keep"), "alternative");
        apply(core, request("keep", "current"));
        check(read_file(root / "keep.txt") == "keep these bytes",
              "Current choice preserves original contents");
        atomic_write(root / copy_name("deleted"), "old deleted version");
        auto deletion = apply(core, request("deleted", "current"));
        check(!fs::exists(root / "deleted.txt") && !deletion.at("result").at("exists").get<bool>(),
              "Keeping deletion does not recreate the original");
        atomic_write(root / copy_name("restore"), "recover original");
        apply(core, request("restore", "conflict"));
        check(read_file(root / "restore.txt") == "recover original",
              "Conflict choice can recreate a missing original");
        atomic_write(root / "merged.txt", "left\r\n");
        atomic_write(root / copy_name("merged"), "right\r\n");
        auto merged = request("merged", "merged");
        merged["content"] = "left and right\r\nété\r\n";
        auto mergedDone = apply(core, merged);
        check(read_file(root / "merged.txt") == merged.at("content").get<std::string>(),
              "Manual merge preserves exact UTF-8 and line endings");
        auto mergedOutput = run / "merged-export";
        check(act(core, "resolution-export",
                  {{"resolution", mergedDone.at("id")}, {"destination", path_string(mergedOutput)}})
                          .at("files") == 3 &&
                  read_file(mergedOutput / "merged/merged.txt") ==
                      merged.at("content").get<std::string>(),
              "Manual result is independently recoverable with both sources");
        atomic_write(root / "binary.txt", std::string("a\0b", 3));
        atomic_write(root / copy_name("binary"), std::string("c\0d", 3));
        apply(core, request("binary", "conflict"));
        check(read_file(root / "binary.txt") == std::string("c\0d", 3),
              "Binary choice is byte exact");
        atomic_write(root / "invalid.txt", "before");
        atomic_write(root / copy_name("invalid"), "after");
        auto invalid = request("invalid", "merged");
        invalid["content"] = std::string("a\0b", 3);
        rejects([&] { act(core, "resolution-plan", invalid); }, "Manual merge rejects NUL bytes");
        invalid["content"] = std::string(2, static_cast<char>(0xff));
        rejects([&] { act(core, "resolution-plan", invalid); },
                "Manual merge rejects invalid UTF-8");
        invalid["content"] = std::string(512 * 1024 + 1, 'x');
        rejects([&] { act(core, "resolution-plan", invalid); },
                "Manual editor payload has a bounded size");
        invalid["choice"] = "automatic";
        rejects([&] { act(core, "resolution-plan", invalid); }, "Unknown resolution mode rejected");
        auto excluded = request("invalid", "current");
        excluded["path"] = "../" + copy_name("invalid");
        rejects([&] { act(core, "resolution-plan", excluded); }, "Resolution refuses traversal");
        atomic_write(root / copy_name("too-large"), "");
        fs::resize_file(root / copy_name("too-large"), 100 * 1024 * 1024 + 1);
        rejects([&] { act(core, "resolution-plan", request("too-large", "conflict")); },
                "Oversized resolution input rejected before hashing");
        fs::remove(root / copy_name("too-large"));
#ifdef _WIN32
        auto handle = CreateFileW((root / "invalid.txt").c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, 0, nullptr);
        check(handle != INVALID_HANDLE_VALUE, "Locked source fixture opened");
        rejects([&] { apply(core, request("invalid", "conflict")); },
                "Locked Windows source refuses mutation");
        CloseHandle(handle);
        check(read_file(root / "invalid.txt") == "before" &&
                  read_file(root / copy_name("invalid")) == "after",
              "Windows sharing failure leaves both sources intact");
#endif
        Core other(run / "another-profile");
        other.request("POST", "/api/projects",
                      {{"id", "team"},
                       {"name", "Other"},
                       {"mode", "backup"},
                       {"root", path_string(root)},
                       {"backup_interval_minutes", 0},
                       {"paused", true}});
        rejects([&] { apply(other, request("invalid", "conflict")); },
                "A second profile cannot claim another profile's recovery directory");
        check(read_file(root / "invalid.txt") == "before" &&
                  read_file(root / copy_name("invalid")) == "after",
              "Recovery ownership refusal preserves both live sources");
        atomic_write(root / "mode.txt", "read only original");
        atomic_write(root / copy_name("mode"), "replacement");
        fs::permissions(root / "mode.txt", fs::perms::owner_read);
        auto original_mode = fs::status(root / "mode.txt").permissions();
        apply(core, request("mode", "conflict"));
        check(fs::status(root / "mode.txt").permissions() == original_mode,
              "Resolution preserves the original target's simple permissions");
        // A non-cooperating app creates a new working file in the move/publication gap.
        atomic_write(root / "race.txt", "reviewed source");
        atomic_write(root / copy_name("race"), std::string(8 * 1024 * 1024, 'z'));
        atomic_write(run / "external.txt", "external editor data");
        auto race = request("race", "conflict");
        race["plan"] = act(core, "resolution-plan", race).at("plan");
        std::atomic<bool> stop = false, inserted = false;
        std::thread editor([&] {
            while (!stop) {
                if (!fs::exists(root / "race.txt")) {
                    std::error_code error;
                    if (fs::copy_file(run / "external.txt", root / "race.txt",
                                      fs::copy_options::none, error)) {
                        inserted = true;
                        return;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        bool refused = false;
        try {
            act(core, "resolve-conflict", race);
        } catch (const std::exception &) {
            refused = true;
        }
        stop = true;
        editor.join();
        check(inserted && refused && read_file(root / "race.txt") == "external editor data",
              "Exclusive publication never overwrites a newly created external file");
        auto history = act(core, "resolutions");
        Json interrupted;
        for (const auto &record : history.at("records"))
            if (record.at("original") == "race.txt")
                interrupted = record;
        check(!interrupted.is_null() && interrupted.at("needs_attention"),
              "Partial operation is discoverable in recovery history");
        check(core.request("GET", "/api/overview").at("projects").at(0).at("resolution_pending") ==
                  interrupted.at("id"),
              "Interrupted resolution is visible from the overview after restarting a controller");
        rejects([&] { apply(core, request("invalid", "conflict")); },
                "Pending recovery blocks another resolution until reviewed");
        auto raceExport = run / "race-export";
        act(core, "resolution-export",
            {{"resolution", interrupted.at("id")}, {"destination", path_string(raceExport)}});
        check(read_file(raceExport / "current/race.txt") == "reviewed source",
              "The reviewed source is recoverable after external interference");
        check(fs::is_regular_file(
                  root / fs::u8path(interrupted.at("recovery_directory").get<std::string>()) /
                  "current"),
              "The actual moved source is also retained after interference");
        auto brokenRecord = core.state() / "resolutions/team" /
                            interrupted.at("id").get<std::string>() / "resolution.json";
        auto record = Json::parse(read_file(brokenRecord));
        auto malicious = record;
        malicious["original"] = "../../outside";
        atomic_write(brokenRecord, malicious.dump());
        check(!act(core, "resolutions").at("errors").empty(),
              "Damaged history remains diagnosable");
        rejects(
            [&] {
                act(core, "resolution-export",
                    {{"resolution", record.at("id")},
                     {"destination", path_string(run / "malicious-export")}});
            },
            "Tampered recovery paths refuse export");
        atomic_write(brokenRecord, record.dump());
        rejects(
            [&] {
                act(core, "resolution-export",
                    {{"resolution", "../../state"}, {"destination", path_string(run / "bad-id")}});
            },
            "History traversal identifier rejected");
        act(core, "resolution-acknowledge", {{"resolution", interrupted.at("id")}});
        check(!core.request("GET", "/api/overview")
                   .at("projects")
                   .at(0)
                   .contains("resolution_pending"),
              "Explicit recovery review clears the pending diagnostic");
        check(fs::is_regular_file(core.state() / "resolutions/team" /
                                  interrupted.at("id").get<std::string>() / "current.bin") &&
                  read_file(root / "race.txt") == "external editor data",
              "Acknowledgement retains backups and never changes the working file");
        std::cout << "PASS " << checks << " resolution checks; fixtures: " << path_string(run)
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
