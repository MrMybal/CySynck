#include <cysynck/core.hpp>
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
static std::string pointer(const std::string &asset) {
    return "version https://git-lfs.github.com/spec/v1\noid sha256:" + sha256(asset) + "\nsize " +
           std::to_string(asset.size()) + "\n";
}
static Json add(Core &core, const fs::path &root, const fs::path &exchange,
                const std::string &format) {
    fs::create_directories(root);
    auto p = core.request("POST", "/api/projects",
                          {{"id", "team"},
                           {"name", "LFS integration"},
                           {"mode", "git-bundles"},
                           {"root", path_string(root)},
                           {"exchange_path", path_string(exchange)},
                           {"git_lfs", false},
                           {"backup_interval_minutes", 0}});
    git(p, {"init", "--initial-branch=main", "--object-format=" + format});
    git(p, {"config", "core.autocrlf", "false"});
    return p;
}
static std::string commit(const Json &p, const std::string &message) {
    git(p, {"-c", "filter.lfs.process=", "-c", "filter.lfs.clean=", "-c",
            "filter.lfs.required=false", "add", "--all", "--", ".", ":(exclude)keep.local"});
    git(p,
        {"-c", "filter.lfs.process=", "-c", "filter.lfs.clean=", "-c", "filter.lfs.required=false",
         "-c", "user.name=LFS Test", "-c", "user.email=lfs@example.invalid", "-c",
         "commit.gpgsign=false", "commit", "-m", message});
    return git(p, {"rev-parse", "HEAD"});
}
int main() {
    auto run = fs::current_path() / "git-lfs-integrate-test-data" / random_id();
    try {
        for (auto format : {"sha1", "sha256"}) {
            auto work = run / format, root = work / fs::u8path("projet été");
            Core core(work / "profile");
            auto p = add(core, root, work / "exchange", format);
            auto action = [&](const std::string &name, const Json &input) {
                return core.request("POST", "/api/projects/team/git-" + name, input);
            };
            auto query = Json{{"peer", "peer-a"}, {"branch", "main"}};
            auto input = query;
            input["lfs_policy"] = "pointers";
            atomic_write(root / "ordinary.txt", "base\n");
            auto base = commit(p, "Base");
            std::string asset("binary\0asset", 12);
            auto original = pointer(asset), empty = pointer("");
            atomic_write(root / ".gitattributes", "*.uasset filter=lfs diff=lfs merge=lfs -text\n");
            atomic_write(root / fs::u8path("été.uasset"), original);
            atomic_write(root / "copy.uasset", original);
            atomic_write(root / "empty.uasset", empty);
            auto head = commit(p, "LFS pointers");
            git(p, {"update-ref", "refs/remotes/cysynck/peer-a/main", head});
            auto bundle = work / "source.bundle";
            git(p, {"bundle", "create", path_string(bundle), "refs/remotes/cysynck/peer-a/main"});
            git(p, {"reset", "--hard", base});
            for (auto key : {"process", "clean", "smudge"})
                git(p,
                    {"config", std::string("filter.lfs.") + key, "cysynck-forbidden-lfs-helper"});
            git(p, {"config", "filter.lfs.required", "true"});
            auto config = sha256_file(root / ".git/config"),
                 index = sha256_file(root / ".git/index");
            atomic_write(root / "keep.local", "preserved local data");
            check(!action("integrate", query).at("can_integrate").get<bool>(),
                  "Default integration still refuses LFS");
            auto preview = action("integrate", input);
            check(preview.at("can_integrate") && preview.at("lfs_policy") == "pointers",
                  "Explicit policy enables pointer-only fast-forward");
            check(preview.at("lfs_before").at("files") == 0 &&
                      preview.at("lfs_after") ==
                          Json{{"files", 3}, {"objects", 2}, {"bytes", asset.size()}},
                  "Inventory counts paths and deduplicates asset bytes, including empty objects");
            check(sha256_file(root / ".git/index") == index && !fs::exists(root / "copy.uasset") &&
                      !fs::exists(core.state() / "git-integrations"),
                  "Preview preserves index and working files and creates no receipt");
            rejects(
                [&] {
                    auto bad = query;
                    bad.update({{"apply", true}, {"plan", preview.at("plan")}});
                    action("integrate", bad);
                },
                "Application must repeat its LFS policy");
            atomic_write(root / "ordinary.txt", "staged\n");
            git(p, {"add", "ordinary.txt"});
            check(!action("integrate", input).at("can_integrate").get<bool>(),
                  "Staged ordinary changes remain protected");
            git(p, {"reset", "--hard", base});
            preview = action("integrate", input);
            auto apply = input;
            apply.update({{"apply", true}, {"plan", preview.at("plan")}});
            auto result = action("integrate", apply);
            check(result.at("applied") && result.at("status") == "complete" &&
                      git(p, {"rev-parse", "HEAD"}) == head,
                  "Pointer-only application fast-forwards the selected branch");
            check(read_file(root / fs::u8path("été.uasset")) == original &&
                      read_file(root / "empty.uasset") == empty &&
                      read_file(root / "keep.local") == "preserved local data",
                  "Working checkout contains exact committed pointers and preserves local data");
            check(!fs::exists(root / ".git/lfs") && sha256_file(root / ".git/config") == config,
                  "Integration invokes no LFS helper, creates no asset cache and preserves "
                  "configuration");
            auto receipt_id = result.at("receipt").get<std::string>();
            auto checked = action("integration-check", {{"receipt", receipt_id}});
            check(checked.at("position") == "after" && checked.at("tracked_matches") &&
                      checked.at("errors").empty(),
                  "Receipt verification understands committed LFS pointer trees");
            check(action("integrations", Json::object()).at("records").at(0).at("lfs_policy") ==
                      "pointers",
                  "History discloses the applied pointer policy");
            auto record_path = core.state() / "git-integrations/team" / (receipt_id + ".json");
            auto good = read_file(record_path);
            auto interrupted = Json::parse(good);
            interrupted.update({{"status", "incomplete"}, {"applied", false}});
            atomic_write(record_path, interrupted.dump());
            checked = action("integration-check", {{"receipt", receipt_id}});
            check(checked.at("can_acknowledge"),
                  "An interrupted pointer checkout can be explicitly reviewed after verification");
            check(action("integration-acknowledge",
                         {{"receipt", receipt_id}, {"plan", checked.at("plan")}})
                          .at("status") == "incomplete",
                  "Review preserves the original incomplete status");
            auto tampered = Json::parse(good);
            tampered["lfs_after"]["files"] = 99;
            atomic_write(record_path, tampered.dump());
            check(!action("integration-check", {{"receipt", receipt_id}}).at("errors").empty(),
                  "Receipt LFS count tampering is detected against committed pointers");
            atomic_write(record_path, good);
            atomic_write(root / fs::u8path("été.uasset"), asset);
            check(!action("integration-check", {{"receipt", receipt_id}})
                       .at("tracked_matches")
                       .get<bool>(),
                  "Later materialization differs from the receipt's pointer checkout state");
            atomic_write(root / fs::u8path("été.uasset"), original);
            // A second branch update exercises an already pointer-based local HEAD.
            atomic_write(root / "copy.uasset", pointer("next asset"));
            auto next = commit(p, "Next pointer");
            git(p, {"update-ref", "refs/remotes/cysynck/peer-a/main", next});
            git(p, {"-c", "filter.lfs.process=", "-c", "filter.lfs.smudge=", "-c",
                    "filter.lfs.required=false", "reset", "--hard", head});
            auto cli_preview = cli(
                core, {"git", "integrate", "team", "peer-a", "main", "--lfs-policy", "pointers"});
            check(cli_preview.at("can_integrate") && cli_preview.at("lfs_before").at("files") == 3,
                  "CLI previews an LFS-to-LFS fast-forward");
            atomic_write(root / fs::u8path("été.uasset"), asset);
            check(!action("integrate", input).at("can_integrate").get<bool>(),
                  "Materialized working assets block further integration and remain preserved");
            rejects(
                [&] {
                    auto stale = input;
                    stale.update({{"apply", true}, {"plan", cli_preview.at("plan")}});
                    action("integrate", stale);
                },
                "Materialization invalidates an existing plan");
            check(read_file(root / fs::u8path("été.uasset")) == asset &&
                      git(p, {"rev-parse", "HEAD"}) == head,
                  "Blocked application preserves expanded bytes and HEAD");
            atomic_write(root / fs::u8path("été.uasset"), original);
            cli_preview = cli(
                core, {"git", "integrate", "team", "peer-a", "main", "--lfs-policy", "pointers"});
            check(cli(core, {"git", "integrate", "team", "peer-a", "main", "--lfs-policy",
                             "pointers", "--apply", "true", "--plan", cli_preview.at("plan")})
                      .at("applied"),
                  "CLI repeats its checked pointer policy on application");
            for (const auto &bad : {Json(true), Json("hydrate"), Json(nullptr)}) {
                auto invalid = query;
                invalid["lfs_policy"] = bad;
                rejects([&] { action("integrate", invalid); },
                        "Unknown or mistyped pointer policies are refused");
            }
            rejects([&] { action("merge", input); },
                    "Pointer policy cannot enable divergent LFS merging");
            rejects(
                [&] {
                    cli(core,
                        {"git", "merge", "team", "peer-a", "main", "--lfs-policy", "pointers"});
                },
                "CLI restricts pointer policy to fast-forward integration");
            // Attribute/filter mismatch and malformed pointers remain rejected.
            atomic_write(root / "bad.uasset", "ordinary bytes under an LFS filter");
            auto bad_head = commit(p, "Mismatched filter");
            git(p, {"update-ref", "refs/remotes/cysynck/peer-a/main", bad_head});
            git(p, {"-c", "filter.lfs.process=", "-c", "filter.lfs.smudge=", "-c",
                    "filter.lfs.required=false", "reset", "--hard", next});
            check(!action("integrate", input).at("can_integrate").get<bool>(),
                  "LFS filter on nonpointer content is refused");
            atomic_write(root / "bad.uasset",
                         "version https://git-lfs.github.com/spec/v1\r\noid sha256:" +
                             sha256(asset) + "\r\nsize 12\r\n");
            bad_head = commit(p, "Malformed pointer");
            git(p, {"update-ref", "refs/remotes/cysynck/peer-a/main", bad_head});
            git(p, {"-c", "filter.lfs.process=", "-c", "filter.lfs.smudge=", "-c",
                    "filter.lfs.required=false", "reset", "--hard", next});
            check(!action("integrate", input).at("can_integrate").get<bool>(),
                  "Noncanonical LFS pointer revisions remain refused");
            Core fresh(work / "fresh profile");
            auto fresh_root = work / fs::u8path("premier import été");
            auto fresh_p = add(fresh, fresh_root, work / "exchange", format);
            git(fresh_p, {"bundle", "unbundle", path_string(bundle)});
            git(fresh_p, {"update-ref", "refs/remotes/cysynck/peer-a/main", head});
            auto first = fresh.request("POST", "/api/projects/team/git-integrate", input);
            check(first.at("relation") == "unborn" && first.at("can_integrate"),
                  "An unborn repository can explicitly preview LFS pointers without assets");
            auto first_apply = input;
            first_apply.update({{"apply", true}, {"plan", first.at("plan")}});
            check(fresh.request("POST", "/api/projects/team/git-integrate", first_apply)
                          .at("applied") &&
                      read_file(fresh_root / fs::u8path("été.uasset")) == original,
                  "First import installs raw pointers without an LFS runtime or cache");
        }
        std::cout << "PASS " << checks
                  << " LFS pointer integration checks; fixtures: " << path_string(run) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << "; fixtures: " << path_string(run) << '\n';
        return 1;
    }
}
