#include <cysynck/core.hpp>
#include <functional>
#include <iostream>
#include <optional>

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
    return core.request("POST", "/api/projects/review/" + action, data);
}
int main() {
    auto run = fs::current_path() / "diff-test-data" / random_id();
    auto root = run / "project";
    fs::create_directories(root);
    try {
        Core core(run / "state");
        core.request("POST", "/api/projects",
                     {{"id", "review"},
                      {"name", "Review"},
                      {"root", path_string(root)},
                      {"mode", "backup"},
                      {"backup_interval_minutes", 0},
                      {"backup_keep", 100}});
        int number = 0;
        auto patch_case = [&](const std::string &name, std::optional<std::string> before,
                              std::optional<std::string> after) {
            auto path = root / fs::u8path(name);
            if (before)
                atomic_write(path, *before);
            else
                fs::remove(path);
            auto snapshot = act(core, "backup");
            if (after)
                atomic_write(path, *after);
            else
                fs::remove(path);
            auto result = act(core, "diff", {{"snapshot", snapshot.at("id")}, {"path", name}});
            check(!result.at("binary").get<bool>() && result.at("patch_available"),
                  "Text diff is exportable");
            auto patch = result.at("patch").get<std::string>();
            if (before == after) {
                check(result.at("identical") && patch.empty(), "Identical content has no patch");
                return result;
            }
            auto apply = run / ("apply-" + std::to_string(++number));
            fs::create_directories(apply);
            check(process({"git", "-C", path_string(apply), "init", "--quiet"}).code == 0,
                  "Initialize patch consumer");
            if (before)
                atomic_write(apply / fs::u8path(name), *before);
            auto patchFile = run / ("patch-" + std::to_string(number) + ".diff");
            atomic_write(patchFile, patch);
            auto applied = process({"git", "-C", path_string(apply), "-c", "core.autocrlf=false",
                                    "apply", "--whitespace=nowarn", "--", path_string(patchFile)});
            if (applied.code)
                throw std::runtime_error("git apply failed for " + name + ": " + applied.error);
            check(after ? fs::is_regular_file(apply / fs::u8path(name)) &&
                              read_file(apply / fs::u8path(name), 1024 * 1024) == *after
                        : !fs::exists(apply / fs::u8path(name)),
                  "Real git apply reproduces exact target bytes and existence");
            return result;
        };
        patch_case("simple.txt", "one\ntwo\n", "one\nthree\n");
        patch_case("created.txt", std::nullopt, "created\n");
        patch_case("deleted.txt", "deleted\n", std::nullopt);
        patch_case("empty-created.txt", std::nullopt, "");
        patch_case("empty-deleted.txt", "", std::nullopt);
        patch_case("end-of-file.txt", "one\ntwo", "one\nthree");
        patch_case("newline-added.txt", "one", "one\n");
        patch_case("newline-removed.txt", "one\n", "one");
        patch_case("windows.txt", "one\r\ntwo\r\n", "one\r\nthree\r\n");
        patch_case(path_string(fs::u8path("dossier/été espace.txt")), "été\n", "hiver\n");
        patch_case("identical.txt", "same\n", "same\n");
        std::string a, b;
        for (int i = 0; i < 40; ++i) {
            a += "line " + std::to_string(i) + "\n";
            b += (i == 2 || i == 35 ? "changed " : "line ") + std::to_string(i) + "\n";
        }
        auto separated = patch_case("hunks.txt", a, b);
        check(separated.at("hunks") == 2 && separated.at("added_lines") == 2 &&
                  separated.at("removed_lines") == 2,
              "Distant edits produce contextual hunks and accurate counts");
        a.clear();
        b.clear();
        for (int i = 0; i < 9000; ++i) {
            a += "line " + std::to_string(i) + "\n";
            b += (i == 4500 ? "changed " : "line ") + std::to_string(i) + "\n";
        }
        auto large = patch_case("large-similar.txt", a, b);
        check(!large.at("approximate").get<bool>() && large.at("added_lines") == 1,
              "Large similar files keep an exact small diff");
        a.clear();
        b.clear();
        for (int i = 0; i < 1500; ++i) {
            a += "left " + std::to_string(i) + "\n";
            b += "right " + std::to_string(i) + "\n";
        }
        check(patch_case("large-replaced.txt", a, b).at("approximate"),
              "Large unrelated diff reports bounded fallback");

        atomic_write(root / "left.txt", "before\n");
        atomic_write(root / "right.txt", "after\n");
        auto comparison = cli(core, {"compare", "review", "left.txt", "right.txt"});
        check(comparison.at("before") == "before\n" && comparison.at("after") == "after\n" &&
                  comparison.at("before_info").at("sha256") == sha256("before\n"),
              "Two-file CLI comparison preserves source identities and hashes");
        rejects([&] { act(core, "compare", {{"left", "../outside"}, {"right", "right.txt"}}); },
                "Comparison refuses traversal");
        rejects([&] { act(core, "compare", {{"left", "missing.txt"}, {"right", "right.txt"}}); },
                "Comparison requires existing inputs");
        rejects(
            [&] {
                act(core, "compare",
                    {{"left", std::string("left.txt\0hidden", 15)}, {"right", "right.txt"}});
            },
            "Comparison refuses embedded NUL paths");
        atomic_write(root / "binary.bin", std::string("a\0b", 3));
        comparison = act(core, "compare", {{"left", "binary.bin"}, {"right", "binary.bin"}});
        check(comparison.at("binary") && comparison.at("identical") &&
                  !comparison.at("patch_available").get<bool>(),
              "Equal binary data is identified without exporting a false text patch");
        atomic_write(root / "invalid-utf8.bin", std::string(2, static_cast<char>(0xff)));
        check(act(core, "compare", {{"left", "invalid-utf8.bin"}, {"right", "left.txt"}})
                  .at("binary"),
              "Invalid UTF-8 has no text preview");
        atomic_write(root / "oversized.txt", std::string(512 * 1024 + 1, 'x'));
        auto limited = act(core, "compare", {{"left", "oversized.txt"}, {"right", "left.txt"}});
        check(limited.at("binary") && limited.at("identical").is_null() &&
                  !limited.contains("before"),
              "Oversized preview is bounded and makes no content-equality claim");
        atomic_write(root / "many-lines.txt", std::string(10001, '\n'));
        check(
            act(core, "compare", {{"left", "many-lines.txt"}, {"right", "left.txt"}}).at("binary"),
            "Line-count limit protects the viewer");

        const auto conflict = "notes.sync-conflict-20261104-120000-ABCDEFG.txt";
        atomic_write(root / "notes.txt", "current\n");
        atomic_write(root / conflict, "other device\n");
        atomic_write(root / "orphan.sync-conflict-20261104-120001-ABCDEFG", "deleted original\n");
        atomic_write(root / ".sync-conflict-20261104-120002-ABCDEFG.env", "PRIVATE=value\n");
        atomic_write(root / "nested/long.name.sync-conflict-20261104-120003-ABCDEFG.tar.gz",
                     "archive\n");
        atomic_write(root / "notes.sync-conflict-20269999-120000-ABCDEFG.txt", "bad date\n");
        atomic_write(root / ".git/secret.sync-conflict-20261104-120000-ABCDEFG.txt", "ignored\n");
        atomic_write(root / ".stversions/old.sync-conflict-20261104-120000-ABCDEFG.txt",
                     "ignored\n");
        auto listing = cli(core, {"conflicts", "review"});
        check(listing.at("files").size() == 4 && listing.at("errors").size() == 1,
              "Conflict discovery handles extensionless, hidden and compound filenames and "
              "excludes internal directories");
        auto reviewed = cli(core, {"conflict-diff", "review", conflict});
        check(reviewed.at("before") == "current\n" && reviewed.at("after") == "other device\n" &&
                  reviewed.at("path") == "notes.txt",
              "Conflict diff targets the original file in current-to-conflict direction");
        auto orphan =
            act(core, "conflict-diff", {{"path", "orphan.sync-conflict-20261104-120001-ABCDEFG"}});
        check(!orphan.at("before_info").at("exists").get<bool>() && orphan.at("added_lines") == 1,
              "Conflict with a deleted original is reviewable as a creation");
        core.request("PUT", "/api/projects/review",
                     {{"excludes", {".git", ".stversions", "*.sync-conflict-*", "notes.txt"}}});
        check(act(core, "conflicts").at("files").size() == 3,
              "Original-file exclusion also hides its conflict copy");
        rejects([&] { act(core, "conflict-diff", {{"path", conflict}}); },
                "Direct conflict review respects original exclusions");
        rejects(
            [&] {
                act(core, "conflict-diff",
                    {{"path", ".git/secret.sync-conflict-20261104-120000-ABCDEFG.txt"}});
            },
            "Direct conflict review excludes Git internals");
#ifdef _WIN32
        rejects(
            [&] {
                act(core, "conflict-diff",
                    {{"path", ".GiT/secret.sync-conflict-20261104-120000-ABCDEFG.txt"}});
            },
            "Windows internal-directory exclusion ignores case aliases");
#endif
        rejects([&] { act(core, "conflict-diff"); }, "Conflict diff requires an explicit input");
        check(read_file(root / conflict) == "other device\n" &&
                  read_file(root / "notes.txt") == "current\n",
              "Review preserves both original sources");
        act(core, "backup");
        atomic_write(core.state() / "objects" / sha256("before\n"), "damaged");
        rejects([&] { act(core, "diff", {{"path", "left.txt"}}); },
                "Snapshot diff rejects a corrupt baseline instead of showing false changes");
        std::cout << "PASS " << checks << " diff checks; fixtures: " << path_string(run) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
