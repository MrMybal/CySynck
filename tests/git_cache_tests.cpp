#include <cysynck/core.hpp>
#include <cysynck/c_api.h>
#include "git_lfs.hpp"
#include <functional>
#include <iostream>
#include <set>
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
static Json working(const fs::path &root) {
    Json result = Json::object();
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator();
         ++it) {
        if (it->path().lexically_relative(root).begin()->string() == ".git") {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file())
            result[path_string(it->path().lexically_relative(root))] = sha256_file(it->path());
    }
    return result;
}
int main() {
    auto run = fs::current_path() / "git-cache-test-data" / random_id();
    try {
        for (auto format : {"sha1", "sha256"}) {
            auto work = run / format, root = work / fs::u8path("Projet été");
            fs::create_directories(root);
            Core core(work / "profile");
            auto p = core.request("POST", "/api/projects",
                                  {{"id", "team"},
                                   {"name", "Cache"},
                                   {"mode", "git-bundles"},
                                   {"root", path_string(root)},
                                   {"exchange_path", path_string(work / "exchange")},
                                   {"backup_interval_minutes", 0}});
            auto action = [&](const std::string &name, const Json &data) {
                return core.request("POST", "/api/projects/team/git-" + name, data);
            };
            git(p, {"init", "--initial-branch=main", std::string("--object-format=") + format});
            rejects([&] { action("lfs-cache", Json::object()); },
                    "Unborn cache inspection requires an imported branch or commit");
            git(p, {"config", "core.autocrlf", "false"});
            auto commit = [&] {
                git(p, {"add", "--all"});
                git(p, {"-c", "user.name=Cache Test", "-c", "user.email=cache@example.invalid",
                        "commit", "-m", "Cache fixture"});
            };
            std::string a("first\0asset", 11), b(2 * 1024 * 1024 + 17, 'b'), c = "third asset";
            atomic_write(root / "Asset.uasset", pointer(a));
            atomic_write(root / "other.uasset", pointer(b));
            commit();
            atomic_write(root / "Asset.uasset", pointer(c));
            commit();
            atomic_write(root / "Asset.uasset", "dirty working asset");
            atomic_write(root / "staged.txt", "staged");
            git(p, {"add", "staged.txt"});
            auto local = lfs_local_store(p), source = work / "exchange/peers/peer-a/lfs/objects";
            atomic_write(lfs_object_path(source, sha256(a)), a);
            atomic_write(lfs_object_path(source, sha256(b)), b);
            atomic_write(lfs_object_path(source, sha256(c)), c);
            auto before = working(root);
            auto index = sha256_file(root / ".git/index"), head = git(p, {"rev-parse", "HEAD"}),
                 config = sha256_file(root / ".git/config");
            auto original_config = read_file(root / ".git/config");
            auto initial = action("lfs-cache", {{"limit", 2}});
            check(initial.at("total") == 3 && initial.at("missing") == 3 &&
                      initial.at("objects").size() == 2,
                  "History audit includes deleted versions and bounded pages");
            check(initial.at("content_verified") == false &&
                      initial.at("source_peers") == Json{"peer-a"},
                  "Metadata audit discloses verification and source peers");
            check(!fs::exists(local) && !fs::exists(core.state() / "git-lfs-recoveries"),
                  "Inspection creates neither cache nor receipts");
            auto next = action(
                "lfs-cache",
                {{"offset", 2}, {"limit", 2}, {"inventory_hash", initial.at("inventory_hash")}});
            check(next.at("objects").size() == 1 && next.at("has_more") == false,
                  "Continuation uses a complete inventory fingerprint");
            auto preview = action("lfs-recover", {{"source_peer", "peer-a"}});
            check(preview.at("can_recover") && preview.at("objects").size() == 3 &&
                      preview.at("bytes") == a.size() + b.size() + c.size(),
                  "Verified peer objects become missing-cache candidates");
            check(!fs::exists(local), "Recovery preview does not create the local cache");
            Core other(work / "other profile");
            other.request("POST", "/api/projects", p);
            auto foreign = other.request("POST", "/api/projects/team/git-lfs-recover",
                                         {{"source_peer", "peer-a"}});
            rejects(
                [&] {
                    action(
                        "lfs-recover",
                        {{"source_peer", "peer-a"}, {"apply", true}, {"plan", foreign.at("plan")}});
                },
                "Same project and cache in another profile cannot authorize this recovery");
            rejects(
                [&] {
                    action("lfs-recover",
                           {{"source_peer", "peer-a"}, {"apply", true}, {"plan", "stale"}});
                },
                "Apply requires a fresh recovery plan");
            auto selection = Json{sha256(a)};
            auto selected =
                action("lfs-recover", {{"source_peer", "peer-a"}, {"objects", selection}});
            check(selected.at("objects").size() == 1,
                  "Explicit recovery selection narrows the operation");
            rejects(
                [&] {
                    action("lfs-recover", {{"source_peer", "peer-a"},
                                           {"objects", selection},
                                           {"apply", true},
                                           {"plan", preview.at("plan")}});
                },
                "Plan binds exact recovery selection");
            auto restored = action("lfs-recover", {{"source_peer", "peer-a"},
                                                   {"objects", selection},
                                                   {"apply", true},
                                                   {"plan", selected.at("plan")}});
            check(restored.at("applied") && restored.at("installed").size() == 1 &&
                      read_file(lfs_object_path(local, sha256(a))) == a,
                  "Recovery installs only the selected verified object");
            auto records = action("lfs-recoveries", Json::object());
            check(records.at("total") == 1 &&
                      records.at("records").at(0).at("status") == "complete",
                  "Durable recovery history summarizes results");
            auto receipt = action("lfs-recovery", {{"receipt", restored.at("id")}});
            check(receipt.at("installed") == Json{sha256(a)} && receipt.at("objects").size() == 1,
                  "Detailed receipt retains exact installed identities");
            rejects(
                [&] {
                    action("lfs-recover", {{"source_peer", "peer-a"},
                                           {"objects", selection},
                                           {"apply", true},
                                           {"plan", selected.at("plan")}});
                },
                "Changed cache invalidates an old plan");
            rejects(
                [&] { action("lfs-cache", {{"inventory_hash", initial.at("inventory_hash")}}); },
                "Cache writes invalidate continuation fingerprints");
            auto verified = action("lfs-cache", {{"verify", true}});
            check(verified.at("verified") == 1 && verified.at("missing") == 2 &&
                      verified.at("verification_requested") &&
                      !verified.at("content_verified").get<bool>(),
                  "Explicit verification hashes present objects and discloses missing content");
            atomic_write(lfs_object_path(local, sha256(b)), std::string(b.size(), 'x'));
            atomic_write(lfs_object_path(local, sha256(c)), "bad size");
            auto damaged = action("lfs-cache", {{"verify", true}});
            check(damaged.at("damaged") == 2 && damaged.at("verified") == 1,
                  "Audit detects same-size corruption and wrong sizes");
            auto refused_overwrite = action("lfs-recover", {{"source_peer", "peer-a"}});
            check(!refused_overwrite.at("can_recover").get<bool>() &&
                      refused_overwrite.at("preserved") == 3,
                  "Existing damaged cache entries are preserved");
            check(read_file(lfs_object_path(local, sha256(c))) == "bad size",
                  "Recovery preview preserves damaged bytes");
            fs::remove(lfs_object_path(local, sha256(b)));
            fs::remove(lfs_object_path(local, sha256(c)));
            atomic_write(lfs_object_path(source, sha256(b)), std::string(b.size(), 'y'));
            fs::remove(lfs_object_path(source, sha256(c)));
            auto unavailable = action("lfs-recover", {{"source_peer", "peer-a"}});
            check(!unavailable.at("can_recover").get<bool>() &&
                      unavailable.at("unavailable").size() == 2,
                  "Missing and corrupt sources cannot supply candidates");
            atomic_write(lfs_object_path(source, sha256(c)), c);
            auto partial = action("lfs-recover", {{"source_peer", "peer-a"}});
            check(partial.at("objects").size() == 1 && partial.at("unavailable").size() == 1,
                  "Preview discloses available subset without promising all missing objects");
            auto source_path = lfs_object_path(source, sha256(c));
            auto old_time = fs::last_write_time(source_path);
            fs::last_write_time(source_path, old_time + std::chrono::seconds(2));
            rejects(
                [&] {
                    action(
                        "lfs-recover",
                        {{"source_peer", "peer-a"}, {"apply", true}, {"plan", partial.at("plan")}});
                },
                "Source metadata changes invalidate a preview");
            auto plan = action("lfs-recover", {{"source_peer", "peer-a"}});
            git(p, {"config", "test.cache", "changed"});
            rejects(
                [&] {
                    action("lfs-recover",
                           {{"source_peer", "peer-a"}, {"apply", true}, {"plan", plan.at("plan")}});
                },
                "Configuration changes invalidate recovery");
            atomic_write(root / ".git/config", original_config);
            plan = action("lfs-recover", {{"source_peer", "peer-a"}});
            {
                DiskLock held(local.parent_path(), "cysynck-cache.lock", false);
                rejects(
                    [&] {
                        action("lfs-recover", {{"source_peer", "peer-a"},
                                               {"apply", true},
                                               {"plan", plan.at("plan")}});
                    },
                    "Shared cache lock prevents concurrent writers");
            }
            auto ids = work / "objects.json";
            atomic_write(ids, Json{sha256(c)}.dump());
            auto cli_plan =
                cli(core, {"git", "lfs-recover", "team", "peer-a", "--objects", path_string(ids)});
            check(cli(core, {"git", "lfs-recover", "team", "peer-a", "--objects", path_string(ids),
                             "--apply", "true", "--plan", cli_plan.at("plan")})
                      .at("applied"),
                  "CLI applies a selected reviewed refill");
            check(cli(core, {"git", "lfs-cache", "team", "--verify", "true", "--limit", "1"})
                          .at("objects")
                          .size() == 1,
                  "CLI shares verification and pagination");
            git(p, {"update-ref", "refs/remotes/cysynck/peer-a/main", head});
            auto branch = action("lfs-cache", {{"peer", "peer-a"}, {"branch", "main"}});
            check(branch.at("revision") == initial.at("revision") &&
                      branch.at("context").at("tracking_ref") == "refs/remotes/cysynck/peer-a/main",
                  "Imported branch selection pins its exact context");
            check(working(root) == before && sha256_file(root / ".git/index") == index &&
                      git(p, {"rev-parse", "HEAD"}) == head &&
                      sha256_file(root / ".git/config") == config,
                  "Audit and recovery preserve working, staged, branch and configuration state");
            for (const auto &bad : {Json{{"limit", 0}}, Json{{"limit", 201}}, Json{{"offset", -1}},
                                    Json{{"verify", 1}}, Json{{"offset", 0.5}},
                                    Json{{"peer", "../"}, {"branch", "main"}}})
                rejects([&] { action("lfs-cache", bad); }, "Invalid audit controls rejected");
            for (const auto &bad :
                 {Json{{"source_peer", "../"}},
                  Json{{"source_peer", "peer-a"}, {"objects", Json::array()}},
                  Json{{"source_peer", "peer-a"}, {"objects", {sha256(c), sha256(c)}}},
                  Json{{"source_peer", "peer-a"}, {"objects", {sha256("unknown")}}},
                  Json{{"source_peer", "peer-a"}, {"apply", 1}}})
                rejects([&] { action("lfs-recover", bad); },
                        "Invalid source, object selection or apply type rejected");
            rejects([&] { core.request("GET", "/api/projects/team/git-lfs-cache"); },
                    "LFS audit requires POST");
            rejects([&] { action("lfs-recovery", {{"receipt", "../bad"}}); },
                    "Receipt path traversal rejected");
            rejects([&] { cli(core, {"git", "lfs-cache", "team", "--unknown", "x"}); },
                    "CLI refuses unknown cache flags");
            auto record_path = core.state() / "git-lfs-recoveries/team" /
                               (restored.at("id").get<std::string>() + ".json");
            auto good = read_file(record_path);
            auto tampered = Json::parse(good);
            tampered["installed"] = {sha256("not planned")};
            atomic_write(record_path, tampered.dump());
            rejects([&] { action("lfs-recovery", {{"receipt", restored.at("id")}}); },
                    "Receipt membership tampering rejected");
            check(action("lfs-recoveries", Json::object()).at("errors").size() == 1,
                  "Malformed receipts are diagnosed alongside valid history");
            atomic_write(record_path, good);
            char *error = nullptr, *out = nullptr;
            auto handle = cysynck_open(path_string(core.state()).c_str(), &error);
            check(handle && !error &&
                      cysynck_request(handle, "POST", "/api/projects/team/git-lfs-cache", "{}",
                                      &out) == 0,
                  "Existing C ABI exposes cache inspection");
            check(Json::parse(out).at("total") == 3, "C ABI returns the same committed inventory");
            cysynck_free(out);
            cysynck_close(handle);
            auto custom = work / fs::u8path("cache approuvé");
            fs::create_directories(custom);
            git(p, {"config", "lfs.storage", path_string(custom)});
            core.request("PUT", "/api/projects/team", {{"git_lfs_storage", path_string(custom)}});
            auto custom_plan = action("lfs-recover", {{"source_peer", "peer-a"}});
            check(custom_plan.at("objects").size() == 2,
                  "Approved custom cache previews its available missing objects");
            check(action("lfs-recover", {{"source_peer", "peer-a"},
                                         {"apply", true},
                                         {"plan", custom_plan.at("plan")}})
                      .at("applied"),
                  "Approved custom storage accepts reviewed refill");
            check(read_file(lfs_object_path(custom / "objects", sha256(a))) == a &&
                      action("lfs-cache", {{"verify", true}}).at("verified") == 2,
                  "Custom cache bytes are verified independently of the original cache");
        }
        std::cout << "PASS " << checks
                  << " LFS cache workflow checks; fixtures: " << path_string(run) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << "; fixtures: " << path_string(run) << '\n';
        return 1;
    }
}
