#include <cysynck/core.hpp>
#include <functional>
#include <iostream>
using namespace cysynck;
namespace {
int checks = 0;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
    ++checks;
}
void rejects(const std::function<void()> &call, const char *message) {
    try {
        call();
    } catch (const std::exception &) {
        ++checks;
        return;
    }
    throw std::runtime_error(message);
}
std::string pointer(const std::string &asset) {
    return "version https://git-lfs.github.com/spec/v1\noid sha256:" + sha256(asset) + "\nsize " +
           std::to_string(asset.size()) + "\n";
}
} // namespace
int main() {
    auto run = fs::current_path() / "git-asset-export-data" / random_id();
    try {
        for (auto format : {"sha1", "sha256"}) {
            auto work = run / format, root = work / fs::u8path("repo été");
            fs::create_directories(root);
            Core core(work / "profile");
            auto p = core.request("POST", "/api/projects",
                                  {{"id", "team"},
                                   {"name", "Preserved assets"},
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
                git(p, {"add", "--", ".gitattributes", "Assets", "ordinary.txt"});
                git(p,
                    {"-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "-c",
                     "commit.gpgsign=false", "-c", "core.hooksPath=", "commit", "-m", "Pointers"});
            };
            std::string asset(65539, '\0');
            for (size_t i = 0; i < asset.size(); ++i)
                asset[i] = static_cast<char>(i * 37);
            auto name = std::string("Assets/\xc3\xa9t\xc3\xa9 [1].uasset");
            auto target = root / fs::u8path(name), zero = root / "Assets/empty.uasset";
            atomic_write(root / ".gitattributes", "*.uasset filter=lfs diff=lfs merge=lfs -text\n");
            atomic_write(target, pointer(asset));
            atomic_write(zero, pointer(""));
            atomic_write(root / "ordinary.txt", "base\n");
            commit();
            atomic_write(target, asset);
            atomic_write(zero, "");
            auto ready = core.request("POST", "/api/projects/team/git-materialize",
                                      {{"operation", "dehydrate"}});
            auto done = core.request(
                "POST", "/api/projects/team/git-materialize",
                {{"operation", "dehydrate"}, {"apply", true}, {"plan", ready.at("plan")}});
            auto id = done.at("id").get<std::string>();
            auto receipt = fs::u8path(done.at("receipt_path").get<std::string>());
            auto receipt_bytes = read_file(receipt);
            auto source = root / fs::u8path(done.at("recovery_directory").get<std::string>()) /
                          "before" / fs::u8path(name);
            auto stamp = fs::last_write_time(source);
            auto perms = fs::status(source).permissions();
            atomic_write(target, pointer("different next version"));
            atomic_write(root / "ordinary.txt", "new commit\n");
            commit();
            atomic_write(root / "ordinary.txt", "local edits\n");
            auto head = git(p, {"rev-parse", "HEAD"});
            auto index = read_file(root / ".git/index"), config = read_file(root / ".git/config");
            auto dest = work / fs::u8path("export été");
            auto route = "/api/projects/team/git-materialization-export";
            Json query{{"receipt", id}, {"destination", path_string(dest)}};
            auto preview = [&] { return core.request("POST", route, query); };
            auto apply = [&](const Json &plan) {
                auto input = query;
                input.update({{"apply", true}, {"plan", plan.at("plan")}});
                return core.request("POST", route, input);
            };
            auto plan = preview();
            check(plan.at("can_export") && plan.at("files").size() == 2 &&
                      plan.at("bytes") == asset.size() && !fs::exists(dest),
                  "Export preview did not verify preserved assets after HEAD advanced");
            check(cli(core, {"git", "materialization-export", "team", id, "--destination",
                             path_string(dest)})
                          .at("plan") == plan.at("plan"),
                  "CLI and API export plans differ");
            auto exported = apply(plan);
            check(exported.at("exported") && exported.at("status") == "complete" &&
                      exported.at("files_written") == 2,
                  "Export did not complete its manifest");
            check(read_file(dest / "files" / fs::u8path(name), asset.size() + 1) == asset &&
                      read_file(dest / "files/Assets/empty.uasset").empty(),
                  "Exported binary or empty asset differs");
            check(Json::parse(read_file(dest / "manifest.json")) == exported,
                  "Export manifest differs from API result");
            check(fs::last_write_time(dest / "files" / fs::u8path(name)) == stamp &&
                      fs::status(dest / "files" / fs::u8path(name)).permissions() == perms,
                  "Export lost source timestamp or permissions");
            check(read_file(source, asset.size() + 1) == asset &&
                      fs::last_write_time(source) == stamp && read_file(receipt) == receipt_bytes,
                  "Export changed its source or receipt");
            check(read_file(root / ".git/index") == index &&
                      read_file(root / ".git/config") == config &&
                      git(p, {"rev-parse", "HEAD"}) == head &&
                      read_file(root / "ordinary.txt") == "local edits\n" &&
                      read_file(target) == pointer("different next version") &&
                      !fs::exists(root / ".git/lfs") &&
                      !fs::exists(root / ".git/cysynck-integration.lock"),
                  "Export changed Git context, working data, cache or lock");
            rejects([&] { apply(plan); }, "Existing destination or export replay accepted");
            query["destination"] = path_string(work / "selected");
            query["paths"] = Json::array({"Assets/empty.uasset"});
            auto selected = preview();
            check(selected.at("files").size() == 1 && selected.at("bytes") == 0 &&
                      selected.at("excluded") == 1,
                  "Selected export lost scope or zero-byte asset");
            auto paths_file = work / "paths.json";
            atomic_write(paths_file, query.at("paths").dump());
            auto single =
                cli(core, {"git", "materialization-export", "team", id, "--destination",
                           query.at("destination"), "--paths-file", path_string(paths_file),
                           "--apply", "true", "--plan", selected.at("plan")});
            check(single.at("exported") && !fs::exists(work / "selected/files" / fs::u8path(name)),
                  "CLI selected export copied an excluded asset");
            if (std::string(format) == "sha256")
                continue;
            query.erase("paths");
            query["destination"] = path_string(work / "blocked");
            auto fresh = preview();
            query["destination"] = path_string(work / "changed-destination");
            rejects([&] { apply(fresh); }, "Destination was not bound to export plan");
            check(!fs::exists(work / "changed-destination"), "Stale plan created destination");
            query["destination"] = path_string(work / "blocked");
            atomic_write(receipt, receipt_bytes + "\n");
            rejects([&] { apply(fresh); }, "Receipt bytes were not bound to export plan");
            atomic_write(receipt, receipt_bytes);
            atomic_write(source, std::string(asset.size(), 'x'));
            auto corrupt = preview();
            check(!corrupt.at("can_export").get<bool>() && !corrupt.at("errors").empty(),
                  "Corrupt preserved asset was accepted");
            rejects([&] { apply(fresh); }, "Corrupt source export applied");
            query["paths"] = Json::array({"Assets/empty.uasset"});
            check(preview().at("can_export"), "Excluded damaged source blocked selected export");
            query.erase("paths");
            atomic_write(source, asset);
            fs::permissions(source, perms);
            fs::last_write_time(source, stamp);
            auto forged = done;
            forged["files"][0]["oid"] = std::string(40, '0');
            atomic_write(receipt, forged.dump());
            rejects(preview, "Forged receipt bypassed committed pointer validation");
            atomic_write(receipt, receipt_bytes);
            auto unknown = source.parent_path().parent_path().parent_path() / "unknown";
            atomic_write(unknown, "keep");
            rejects(preview, "Unknown recovery entry ignored");
            check(read_file(unknown) == "keep", "Unknown recovery entry was removed");
            fs::remove(unknown);
            auto lock = root / ".git/cysynck-integration.lock";
            fs::create_directory(lock);
            rejects([&] { apply(fresh); }, "Pre-existing Git lock ignored");
            check(fs::is_directory(lock) && !fs::exists(work / "blocked"),
                  "Existing lock removed or blocked export created destination");
            fs::remove(lock);
            for (auto path : {root / "new-export", work / "profile/new-export",
                              work / "exchange/new-export", dest / "files", fs::path("relative")}) {
                query["destination"] = path_string(path);
                rejects(preview, "Unsafe or existing export destination accepted");
            }
            query["destination"] = path_string(work / "missing-parent/child");
            rejects(preview, "Missing export parent accepted");
            query["destination"] = path_string(work / "blocked");
            for (const auto &bad : {Json::array(), Json::array({"../outside"}),
                                    Json::array({".git/config"}), Json(true)}) {
                query["paths"] = bad;
                rejects(preview, "Unsafe export selection accepted");
            }
            query.erase("paths");
            core.request("PUT", "/api/projects/team", {{"name", "Renamed project"}});
            rejects([&] { apply(fresh); }, "Project changes were not bound to export plan");
            check(!fs::exists(work / "blocked") && read_file(receipt) == receipt_bytes &&
                      read_file(source, asset.size() + 1) == asset,
                  "Refused exports changed destination, source or receipt");
        }
        std::cout << "PASS " << checks << " preserved LFS asset export checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\nFixture: " << path_string(run) << '\n';
        return 1;
    }
}
