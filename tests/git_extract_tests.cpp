#include <cysynck/core.hpp>
#include "git_lfs.hpp"
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
static Json project(Core &core, const fs::path &root, const fs::path &exchange,
                    const std::string &format = "sha1") {
    fs::create_directories(root);
    auto p = core.request("POST", "/api/projects",
                          {{"id", "team"},
                           {"name", "Team"},
                           {"root", path_string(root)},
                           {"exchange_path", path_string(exchange)},
                           {"mode", "git-bundles"},
                           {"backup_interval_minutes", 0}});
    auto plan = cli(core, {"git", "setup", "team", "--object-format", format});
    cli(core, {"git", "setup", "team", "--object-format", format, "--apply", "true", "--plan",
               plan.at("plan")});
    return p;
}
static std::string object(const Json &p, const std::string &bytes) {
    auto id = offline_git(p, {"hash-object", "-w", "--stdin"}, bytes);
    id.erase(id.find_first_of("\r\n"));
    return id;
}
static void stage(const Json &p, const std::string &path, const std::string &bytes,
                  const std::string &mode = "100644") {
    git(p, {"update-index", "--add", "--cacheinfo", mode + "," + object(p, bytes) + "," + path});
}
static std::string commit(const Json &p, const std::string &message) {
    git(p, {"-c", "user.name=Extract", "-c", "user.email=test@example.invalid", "-c",
            "commit.gpgsign=false", "commit", "-m", message});
    return git(p, {"rev-parse", "HEAD"});
}
static std::string tree(const Json &p, const std::string &name, const std::string &hash,
                        const std::string &mode = "100644") {
    std::string data = mode + " " + name + '\0';
    for (size_t i = 0; i < hash.size(); i += 2)
        data += static_cast<char>(std::stoul(hash.substr(i, 2), nullptr, 16));
    auto id = offline_git(p, {"hash-object", "--literally", "-t", "tree", "-w", "--stdin"}, data);
    id.erase(id.find_first_of("\r\n"));
    return id;
}
int main() {
    auto run = fs::current_path() / "git-extract-test-data" / random_id();
    fs::create_directories(run);
    try {
        Core source(run / "source"), target(run / "target");
        auto a = project(source, run / fs::u8path("repo été"), run / "exchange");
        auto b = project(target, run / fs::u8path("receiver été"), run / "exchange");
        auto root = fs::u8path(a.at("root").get<std::string>()),
             receiver = fs::u8path(b.at("root").get<std::string>());
        const std::string binary("\0data\xff\r\n", 8);
        stage(a, "first.txt", "first version\n");
        stage(a, "binary.bin", binary);
        stage(a, "empty", "");
        stage(a, "dir/été [x].txt", "Unicode path\n");
        stage(a, "script.sh", "#!/bin/sh\nexit 0\n", "100755");
        stage(a, ".gitattributes", "*.txt filter=spy\n*.uasset filter=lfs\n");
        auto first = commit(a, "First");
        std::string asset(9 * 1024 * 1024, 'x');
        asset.back() = 'z';
        auto digest = sha256(asset);
        auto relative = fs::path(digest.substr(0, 2)) / digest.substr(2, 2) / digest;
        auto sourceAsset = root / ".git/lfs/objects" / relative;
        fs::create_directories(sourceAsset.parent_path());
        atomic_write(sourceAsset, asset);
        auto pointer = "version https://git-lfs.github.com/spec/v1\noid sha256:" + digest +
                       "\nsize " + std::to_string(asset.size()) + "\n";
        stage(a, "Asset.uasset", pointer);
        stage(a, "second.txt", "new file\n");
        stage(a, "first.txt", "second version\n");
        auto head = commit(a, "Second");
        auto exported = cli(source, {"git", "export", "team"});
        auto peer = exported.at("peer").get<std::string>();
        cli(target, {"git", "import", "team", peer, exported.at("id")});
        atomic_write(receiver / "local.txt", "local staged file\n");
        git(b, {"add", "local.txt"});
        git(b, {"config", "filter.spy.smudge", "command-that-must-not-run"});
        git(b, {"config", "filter.lfs.process", "command-that-must-not-run"});
        auto originalHead = read_file(receiver / ".git/HEAD"),
             index = sha256_file(receiver / ".git/index");
        auto request = [&](Json data) {
            return target.request("POST", "/api/projects/team/git-extract", data);
        };
        Json query = {{"peer", peer},
                      {"branch", "main"},
                      {"destination", path_string(run / fs::u8path("extracted été"))}};
        auto preview = request(query);
        check(preview.at("can_extract") && preview.at("file_count") == 8 &&
                  !fs::exists(run / fs::u8path("extracted été")),
              "Preview inspects the full revision without creating the destination");
        check(preview.at("base").is_null() && preview.at("commit") == head &&
                  preview.at("lfs_files") == 1 && preview.at("lfs_bytes") == asset.size(),
              "Unborn extraction previews expanded LFS size and the pinned revision");
        rejects(
            [&] {
                auto q = query;
                q["apply"] = true;
                request(q);
            },
            "Application requires the reviewed plan");
        rejects(
            [&] {
                auto q = query;
                q.update({{"apply", true}, {"plan", "bad"}});
                request(q);
            },
            "Invalid plan is refused");
        rejects(
            [&] {
                auto q = query;
                q.update({{"apply", true}, {"plan", preview.at("plan")}, {"lfs", false}});
                request(q);
            },
            "Changing LFS policy invalidates the preview");
        rejects(
            [&] {
                auto q = query;
                q.update({{"apply", true},
                          {"plan", preview.at("plan")},
                          {"destination", path_string(run / "different")}});
                request(q);
            },
            "Changing the destination invalidates the preview");
        rejects(
            [&] {
                auto q = query;
                q.update({{"apply", true}, {"plan", preview.at("plan")}, {"commit", first}});
                request(q);
            },
            "Changing the selected commit invalidates the preview");
        auto apply = query;
        apply.update({{"apply", true}, {"plan", preview.at("plan")}});
        auto done = request(apply);
        auto out = run / fs::u8path("extracted été");
        check(done.at("applied") && done.at("files_written") == 8 &&
                  done.at("status") == "complete",
              "Reviewed extraction writes the complete revision");
        check(read_file(out / "first.txt") == "second version\n" &&
                  read_file(out / "second.txt") == "new file\n",
              "Branch extraction uses the selected head's complete tree");
        check(read_file(out / "binary.bin") == binary && fs::file_size(out / "empty") == 0 &&
                  read_file(out / fs::u8path("dir/été [x].txt")) == "Unicode path\n",
              "Binary, empty and literal Unicode paths retain exact bytes");
        check(
            sha256_file(out / "Asset.uasset") == digest &&
                fs::file_size(out / "Asset.uasset") == asset.size(),
            "LFS assets larger than the ordinary blob limit stream from the verified local cache");
        check(!fs::exists(out / ".git") &&
                  read_file(out / ".gitattributes").find("filter=spy") != std::string::npos,
              "Extraction copies committed attributes without creating a Git repository or "
              "executing filters");
#ifndef _WIN32
        check((fs::status(out / "script.sh").permissions() & fs::perms::owner_exec) !=
                  fs::perms::none,
              "Executable Git mode is preserved on POSIX");
#endif
        auto receipt = Json::parse(read_file(run / "target/git-extractions/team" /
                                             (done.at("receipt").get<std::string>() + ".json")));
        check(receipt.at("status") == "complete" && receipt.at("files")[0].contains("sha256"),
              "The durable receipt records completion and verified file hashes");
        rejects([&] { request(apply); }, "An existing output directory is never overwritten");
        fs::create_directory(run / "empty-existing");
        rejects(
            [&] {
                auto q = query;
                q["destination"] = path_string(run / "empty-existing");
                request(q);
            },
            "Even an existing empty output directory is preserved");
        query["destination"] = path_string(run / "pending");
        for (auto path : {receiver / "nested", run / "target/nested", run / "exchange/nested"})
            rejects(
                [&] {
                    auto q = query;
                    q["destination"] = path_string(path);
                    request(q);
                },
                "Output may not overlap projects, exchange or profile state");
        auto old = query;
        old.update({{"destination", path_string(run / "old")}, {"commit", first}});
        auto oldPlan = request(old);
        old.update({{"apply", true}, {"plan", oldPlan.at("plan")}});
        request(old);
        check(read_file(run / "old/first.txt") == "first version\n" &&
                  !fs::exists(run / "old/Asset.uasset"),
              "A selected historical commit extracts its own tree");
        rejects(
            [&] {
                auto q = query;
                q["commit"] = head.substr(0, 12);
                request(q);
            },
            "Abbreviated commit IDs are refused");
        rejects(
            [&] {
                auto q = query;
                q["commit"] = "HEAD~1";
                request(q);
            },
            "Revision expressions are refused");
        auto raw = query;
        raw.update({{"destination", path_string(run / "raw")}, {"lfs", false}});
        auto rawPlan = request(raw);
        raw.update({{"apply", true}, {"plan", rawPlan.at("plan")}});
        request(raw);
        check(read_file(run / "raw/Asset.uasset") == pointer,
              "Explicit raw extraction preserves LFS pointer bytes");
        auto cached = receiver / ".git/lfs/objects" / relative;
        fs::rename(cached, run / "asset-held");
        auto missing = request(query);
        check(!missing.at("can_extract").get<bool>() && missing.at("plan").is_null() &&
                  !missing.at("errors").empty(),
              "Missing LFS data blocks hydrated extraction before creating output");
        rejects(
            [&] {
                auto q = query;
                q.update({{"apply", true}, {"plan", preview.at("plan")}});
                request(q);
            },
            "A missing LFS object prevents application");
        fs::rename(run / "asset-held", cached);
        asset[0] = 'y';
        atomic_write(cached, asset);
        check(!request(query).at("can_extract").get<bool>(),
              "A same-size corrupted LFS asset is refused");
        asset[0] = 'x';
        atomic_write(cached, asset);
        auto customQuery = query;
        customQuery["destination"] = path_string(run / "custom-output");
        auto customPlan = request(customQuery);
        auto custom = run / "approved-lfs";
        fs::create_directories(custom / "objects" / relative.parent_path());
        atomic_write(custom / "objects" / relative, asset);
        git(b, {"config", "lfs.storage", path_string(custom)});
        target.request("PUT", "/api/projects/team", {{"git_lfs_storage", path_string(custom)}});
        rejects(
            [&] {
                auto q = customQuery;
                q.update({{"apply", true}, {"plan", customPlan.at("plan")}});
                request(q);
            },
            "Changing approved LFS storage invalidates a previous extraction plan");
        auto approved = request(customQuery);
        customQuery.update({{"apply", true}, {"plan", approved.at("plan")}});
        request(customQuery);
        check(sha256_file(run / "custom-output/Asset.uasset") == digest,
              "Extraction reads the explicitly approved matching custom LFS cache");
        auto stale = query;
        stale["destination"] = path_string(run / "stale");
        auto stalePlan = request(stale);
        auto ref = "refs/remotes/cysynck/" + peer + "/main";
        git(b, {"update-ref", ref, first});
        rejects(
            [&] {
                stale.update({{"apply", true}, {"plan", stalePlan.at("plan")}});
                request(stale);
            },
            "A changed peer ref invalidates the reviewed token");
        git(b, {"update-ref", ref, head});
        atomic_write(receiver / ".git/shallow", head + "\n");
        rejects([&] { request(query); }, "Shallow repositories are refused explicitly");
        fs::remove(receiver / ".git/shallow");
        git(b, {"config", "remote.incomplete.promisor", "true"});
        git(b, {"config", "remote.incomplete.url", "ext::command-that-must-not-run"});
        rejects([&] { request(query); },
                "Promisor configuration is refused before lazy object fetching can run");
        git(b, {"config", "--unset", "remote.incomplete.promisor"});
        git(b, {"config", "--unset", "remote.incomplete.url"});
        auto makeRevision = [&](const std::string &rootTree) {
            auto oid =
                offline_git(b,
                            {"-c", "user.name=Extract", "-c", "user.email=test@example.invalid",
                             "-c", "commit.gpgsign=false", "commit-tree", rootTree},
                            "Fixture\n");
            oid.erase(oid.find_first_of("\r\n"));
            git(b, {"update-ref", "refs/remotes/cysynck/" + peer + "/fixture", oid});
            return oid;
        };
        auto special = query;
        special.update(
            {{"branch", "fixture"}, {"destination", path_string(run / "special")}, {"lfs", false}});
        auto small = object(b, "target\n");
        auto unreachable = makeRevision(tree(b, "link", small, "120000"));
        rejects(
            [&] {
                auto q = query;
                q["commit"] = unreachable;
                request(q);
            },
            "Full commit IDs outside the selected imported branch are refused");
        check(!request(special).at("can_extract").get<bool>(),
              "Symlinks block extraction instead of following or materializing them");
        makeRevision(tree(b, "module", head, "160000"));
        check(!request(special).at("can_extract").get<bool>(),
              "Submodules require explicit separate handling");
        makeRevision(tree(b, ".git", small));
        rejects([&] { request(special); }, "Git metadata paths cannot be extracted");
        makeRevision(tree(b, "bad\\path", small));
        rejects([&] { request(special); }, "Backslash path aliases are refused on every platform");
        makeRevision(tree(b, "bad:path", small));
        rejects([&] { request(special); }, "Drive and alternate stream path syntax is refused");
        auto collisionTree =
            offline_git(b, {"mktree", "-z"},
                        "100644 blob " + small + "\tCase.txt" + std::string(1, '\0') +
                            "100644 blob " + small + "\tcase.txt" + std::string(1, '\0'));
        collisionTree.erase(collisionTree.find_first_of("\r\n"));
        makeRevision(collisionTree);
        rejects([&] { request(special); },
                "Case-only paths are refused before output creation on every platform");
        auto direct = tree(b, "a", small), nested = tree(b, "inner", small);
        auto directory = tree(b, "a", nested, "40000");
        auto combined = offline_git(b, {"cat-file", "tree", direct}) +
                        offline_git(b, {"cat-file", "tree", directory});
        auto invalidTree =
            offline_git(b, {"hash-object", "--literally", "-t", "tree", "-w", "--stdin"}, combined);
        invalidTree.erase(invalidTree.find_first_of("\r\n"));
        makeRevision(invalidTree);
        rejects([&] { request(special); },
                "File/directory aliases are refused before output creation");
#ifdef _WIN32
        makeRevision(tree(b, "CON", small));
        rejects([&] { request(special); }, "Reserved Windows device names are refused");
#endif
        auto big = object(b, std::string(4 * 1024 * 1024 + 1, 'b'));
        makeRevision(tree(b, "big.bin", big));
        check(!request(special).at("can_extract").get<bool>(),
              "Oversized ordinary Git blobs are explicitly blocked");
        auto boundaryBytes = std::string(4 * 1024 * 1024, 'a');
        auto boundary = object(b, boundaryBytes);
        makeRevision(tree(b, "boundary.bin", boundary));
        auto boundaryPlan = request(special);
        special.update({{"apply", true}, {"plan", boundaryPlan.at("plan")}});
        request(special);
        check(sha256_file(run / "special/boundary.bin") == sha256(boundaryBytes),
              "The exact ordinary blob limit extracts without process truncation");
        auto emptyTree = offline_git(b, {"hash-object", "-w", "-t", "tree", "--stdin"});
        emptyTree.erase(emptyTree.find_first_of("\r\n"));
        makeRevision(emptyTree);
        auto emptyQuery = query;
        emptyQuery.update(
            {{"branch", "fixture"}, {"destination", path_string(run / "empty-revision")}});
        auto emptyPlan = request(emptyQuery);
        emptyQuery.update({{"apply", true}, {"plan", emptyPlan.at("plan")}});
        auto emptyDone = request(emptyQuery);
        check(emptyDone.at("file_count") == 0 && fs::is_empty(run / "empty-revision"),
              "An empty Git tree extracts as a completed empty directory");
        Core shaCore(run / "sha-state");
        auto shaProject = project(shaCore, run / "sha-repo", run / "sha-exchange", "sha256");
        stage(shaProject, "sha.txt", "SHA-256 content\n");
        auto shaHead = commit(shaProject, "SHA-256");
        git(shaProject, {"update-ref", "refs/remotes/cysynck/sha-peer/main", shaHead});
        auto shaPlan = cli(shaCore, {"git", "extract", "team", "sha-peer", "main",
                                     path_string(run / "sha-output")});
        auto shaDone = cli(shaCore, {"git", "extract", "team", "sha-peer", "main",
                                     path_string(run / "sha-output"), "--apply", "true", "--plan",
                                     shaPlan.at("plan")});
        check(shaDone.at("commit").get<std::string>().size() == 64 &&
                  read_file(run / "sha-output/sha.txt") == "SHA-256 content\n",
              "The CLI extracts SHA-256 revisions with exact object validation");
        auto corruptBytes = std::string(2048, 'a'), badBytes = std::string(2048, 'b');
        auto good = object(b, corruptBytes), bad = object(b, badBytes);
        makeRevision(tree(b, "corrupt.bin", good));
        auto corrupt = query;
        corrupt.update(
            {{"branch", "fixture"}, {"destination", path_string(run / "partial")}, {"lfs", false}});
        auto corruptPlan = request(corrupt);
        auto goodPath = receiver / ".git/objects" / good.substr(0, 2) / good.substr(2),
             badPath = receiver / ".git/objects" / bad.substr(0, 2) / bad.substr(2);
        fs::permissions(goodPath, fs::perms::owner_write, fs::perm_options::add);
        fs::copy_file(badPath, goodPath, fs::copy_options::overwrite_existing);
        rejects(
            [&] {
                corrupt.update({{"apply", true}, {"plan", corruptPlan.at("plan")}});
                request(corrupt);
            },
            "A corrupt Git object cannot silently extract under its expected OID");
        bool incomplete = false;
        for (const auto &entry : fs::directory_iterator(run / "target/git-extractions/team")) {
            auto r = Json::parse(read_file(entry.path()));
            if (r.at("destination") == path_string(run / "partial"))
                incomplete = r.at("status") == "incomplete" && r.contains("error");
        }
        check(incomplete && fs::exists(run / "partial/corrupt.bin"),
              "Failure preserves partial output and a durable incomplete receipt");
        check(read_file(receiver / ".git/HEAD") == originalHead &&
                  sha256_file(receiver / ".git/index") == index &&
                  read_file(receiver / "local.txt") == "local staged file\n" &&
                  !fs::exists(receiver / "first.txt"),
              "All extraction paths preserve local HEAD, index and existing working files");
        std::cout << "PASS " << checks << " Git extraction checks; fixtures: " << path_string(run)
                  << '\n';
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what()
                  << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
