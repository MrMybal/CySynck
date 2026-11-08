#include <cysynck/core.hpp>
#include <algorithm>
#include "cyrevision_codec.hpp"
#include "git_lfs.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace cysynck;
namespace cr = cysynck::cyrevision;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void refuses(const std::function<void()> &action, const char *message) {
    bool refused = false;
    try {
        action();
    } catch (const std::exception &) {
        refused = true;
    }
    check(refused, message);
}
std::string compact(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), '-'), value.end());
    return value;
}
void run(const std::vector<std::string> &args, const fs::path &cwd) {
    auto result = process(args, cwd);
    if (result.code)
        throw std::runtime_error("Fixture Git operation failed: " + result.error);
}
void repository(const fs::path &path) {
    fs::create_directories(path);
    run({"git", "init", "--initial-branch=main"}, path);
    run({"git", "config", "user.name", "CySynck isolated test"}, path);
    run({"git", "config", "user.email", "test@example.invalid"}, path);
    run({"git", "config", "core.autocrlf", "false"}, path);
}
int main() {
    auto root = fs::temp_directory_path() / ("cysynck-cyrevision-exchange-" + random_id());
    try {
        check(fs::create_directory(root), "Cannot reserve isolated test directory");
        auto left = root / "left", right = root / "right", exchange = root / "exchange";
        repository(left);
        repository(right);
        fs::create_directory(exchange);
        Json source, target, identity;
        std::string source_id, target_id, device, transaction;
        auto project_guid = "12345678-1234-4234-8234-123456789abc";
        {
            Core publisher(root / "publisher"), receiver(root / "receiver");
            source = publisher.request("POST", "/api/projects",
                                       {{"name", "Source"},
                                        {"root", path_string(left)},
                                        {"mode", "git-bundles"},
                                        {"exchange_path", path_string(exchange)}});
            target = receiver.request("POST", "/api/projects",
                                      {{"name", "Receiver"},
                                       {"root", path_string(right)},
                                       {"mode", "git-bundles"},
                                       {"exchange_path", path_string(exchange)}});
            source_id = source.at("id");
            target_id = target.at("id");
            auto source_route = "/api/projects/" + source_id + "/cyrevision-";
            auto target_route = "/api/projects/" + target_id + "/cyrevision-";
            identity =
                publisher.request("POST", "/api/cyrevision/identity", {{"name", "Author é + &"}});
            device = identity.at("identity").at("deviceId");
            auto key_hash = sha256_file(root / "publisher/cyrevision/signing.pem");
            check(publisher.request("POST", "/api/cyrevision/identity", {{"name", "Other"}}) ==
                      identity,
                  "Existing identity was replaced");
            check(sha256_file(root / "publisher/cyrevision/signing.pem") == key_hash,
                  "Existing private key changed");
            publisher.request(
                "POST", source_route + "configure",
                {{"project_id", project_guid}, {"signature_format", "windows-legacy"}});
            receiver.request("POST", target_route + "configure", {{"project_id", project_guid}});
            refuses(
                [&] {
                    receiver.request("POST", target_route + "configure",
                                     {{"project_id", "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"}});
                },
                "Project association was silently replaced");
            std::string asset(2 * 1024 * 1024, 'x');
            asset[1234] = 'y';
            auto digest = sha256(asset);
            atomic_write(left / ".gitattributes", "*.bin filter=lfs diff=lfs merge=lfs -text\n");
            auto pointer = "version https://git-lfs.github.com/spec/v1\noid sha256:" + digest +
                           "\nsize " + std::to_string(asset.size()) + "\n";
            // Commit a canonical pointer without running Git LFS or any filter in the test.
            atomic_write(left / "asset.bin", pointer);
            atomic_write(left / "note.txt", "Published text\n");
            run({"git", "-c", "filter.lfs.clean=cat", "-c", "filter.lfs.required=false", "add",
                 "."},
                left);
            run({"git", "-c", "core.hooksPath=", "commit", "-m", "Fixture publication"}, left);
            auto object = lfs_object_path(lfs_local_store(source), digest);
            fs::create_directories(object.parent_path());
            atomic_write(object, asset);
            run({"git", "branch", "feature/é"}, left);
            auto exported = publisher.request("POST", source_route + "export");
            transaction = exported.at("manifest").at("transactionId");
            auto signed_file =
                exchange / "transactions" / compact(transaction) / "transaction.json";
            auto signed_doc = cr::parse(read_file(signed_file));
            check(cr::verify_bundle(
                      signed_doc, identity.at("identity").at("signingPublicKey").get<std::string>(),
                      root),
                  "Native publication did not verify directly");
            check(exported.at("lfs_objects") == 1, "Current LFS object was not exported");
            auto inbox = receiver.request("GET", target_route + "inbox");
            check(inbox.at("publications").empty() && inbox.at("rejected").size() == 1,
                  "Untrusted publisher was accepted");
            auto wrong_pin = identity;
            wrong_pin["fingerprint"] = std::string(64, '0');
            refuses([&] { receiver.request("POST", target_route + "trust", wrong_pin); },
                    "Wrong independently supplied fingerprint was accepted");
            receiver.request("POST", target_route + "trust", identity);
            check(cr::verify_bundle(
                      signed_doc, identity.at("identity").at("signingPublicKey").get<std::string>(),
                      root / "receiver/cyrevision/projects" / target_id),
                  "Publication fails verification in the project scratch directory");
            inbox = receiver.request("GET", target_route + "inbox");
            if (inbox.at("publications").size() != 1)
                std::cerr << inbox.dump(2) << '\n';
            check(inbox.at("publications").size() == 1,
                  "Trusted signed publication was not listed");
            auto imported =
                receiver.request("POST", target_route + "import", {{"transaction", transaction}});
            check(imported.at("imported") == true && imported.at("lfs_copied") == 1 &&
                      imported.at("lfs_deferred") == 0,
                  "Signed bundle or LFS import failed");
            check(!fs::exists(right / "asset.bin") && !fs::exists(right / "note.txt"),
                  "Import changed the working tree");
            check(read_file(lfs_object_path(lfs_local_store(target), digest), asset.size() + 1) ==
                      asset,
                  "Imported LFS bytes differ");
            auto branches =
                receiver.request("POST", "/api/projects/" + target_id + "/git-branches");
            check(branches.size() == 2, "CyRevision branches are not available to native review");
            auto replay =
                receiver.request("POST", target_route + "import", {{"transaction", transaction}});
            check(replay.at("already_imported") == true, "Identical import was not idempotent");
            // A delayed LFS object can arrive after Git metadata and be recovered by retrying.
            fs::remove(lfs_object_path(lfs_local_store(target), digest));
            auto delayed =
                exchange / "lfs/objects" / digest.substr(0, 2) / digest.substr(2, 2) / digest;
            auto saved = read_file(delayed, asset.size() + 1);
            fs::remove(delayed);
            replay =
                receiver.request("POST", target_route + "import", {{"transaction", transaction}});
            check(replay.at("lfs_deferred") == 1, "Absent LFS bytes were reported as verified");
            atomic_write(delayed, saved);
            replay =
                receiver.request("POST", target_route + "import", {{"transaction", transaction}});
            check(replay.at("lfs_copied") == 1,
                  "Delayed LFS data was not recovered without changing refs");
            // Model interruption after the atomic ref transaction but before its receipt.
            auto project_home = root / "receiver/cyrevision/projects" / target_id;
            auto receipt = project_home / ("import-" + compact(device) + ".json");
            auto latest = Json::parse(read_file(receipt));
            auto pending = project_home / ("pending-" + compact(device) + ".json");
            fs::remove(receipt);
            atomic_write(
                pending,
                Json{{"manifest", latest.at("manifest")}, {"previous", Json::object()}}.dump());
            refuses(
                [&] {
                    receiver.request("POST", target_route + "import",
                                     {{"transaction", transaction}});
                },
                "Unreviewed interrupted import was silently reused");
            auto recovery =
                receiver.request("POST", target_route + "recover", {{"device", device}});
            check(recovery.at("outcome") == "committed" && recovery.at("applied") == false,
                  "Interrupted import recovery preview is wrong");
            refuses(
                [&] {
                    receiver.request("POST", target_route + "recover",
                                     {{"device", device}, {"apply", true}, {"plan", "stale"}});
                },
                "Stale recovery preview accepted");
            receiver.request("POST", target_route + "recover",
                             {{"device", device}, {"apply", true}, {"plan", recovery.at("plan")}});
            check(fs::exists(receipt) && !fs::exists(pending),
                  "Reviewed committed import was not recovered");
            // A later publication removes only the absent branch inside this device's namespace.
            atomic_write(left / "note.txt", "Second publication\n");
            run({"git", "add", "note.txt"}, left);
            run({"git", "-c", "core.hooksPath=", "commit", "-m", "Next"}, left);
            run({"git", "branch", "-D", "feature/é"}, left);
            auto next = publisher.request("POST", source_route + "export");
            auto review_ref = "refs/remotes/cysynck/cr-" + compact(device) + "/main";
            auto protected_head =
                latest.at("manifest").at("branches").at("main").get<std::string>();
            run({"git", "update-ref", "refs/heads/protected", protected_head}, right);
            run({"git", "symbolic-ref", review_ref, "refs/heads/protected"}, right);
            refuses(
                [&] {
                    receiver.request("POST", target_route + "import",
                                     {{"transaction", next.at("manifest").at("transactionId")}});
                },
                "A symbolic review ref was accepted");
            check(git(target, {"rev-parse", "refs/heads/protected"}) == protected_head,
                  "Refused import changed a local branch through a symbolic ref");
            run({"git", "update-ref", "--no-deref", review_ref, protected_head}, right);
            auto next_bundle = exchange / "transactions" /
                               compact(next.at("manifest").at("transactionId").get<std::string>()) /
                               "repository.bundle";
            auto saved_bundle = read_file(next_bundle, 32 * 1024 * 1024);
            atomic_write(next_bundle, saved_bundle + "altered");
            refuses(
                [&] {
                    receiver.request("POST", target_route + "import",
                                     {{"transaction", next.at("manifest").at("transactionId")}});
                },
                "A bundle with an invalid checksum was imported");
            check(receiver.request("POST", "/api/projects/" + target_id + "/git-branches").size() ==
                      2,
                  "A rejected bundle changed peer references");
            atomic_write(next_bundle, saved_bundle);
            receiver.request("POST", target_route + "import",
                             {{"transaction", next.at("manifest").at("transactionId")}});
            check(receiver.request("POST", "/api/projects/" + target_id + "/git-branches").size() ==
                      1,
                  "Deleted peer branch was not pruned");
            refuses(
                [&] {
                    receiver.request("POST", target_route + "import",
                                     {{"transaction", transaction}});
                },
                "Older signed publication rolled peer refs back");
            auto tx_dir = exchange / "transactions" /
                          compact(next.at("manifest").at("transactionId").get<std::string>());
            auto manifest_path = tx_dir / "transaction.json";
            auto original = read_file(manifest_path);
            auto changed = Json::parse(original);
            changed["manifest"]["branches"]["main"] = std::string(40, 'a');
            atomic_write(manifest_path, changed.dump());
            refuses(
                [&] {
                    receiver.request("POST", target_route + "import",
                                     {{"transaction", next.at("manifest").at("transactionId")}});
                },
                "Tampered signed metadata was accepted");
            atomic_write(manifest_path, original);
            receiver.request("POST", target_route + "revoke", {{"device", device}});
            check(receiver.request("GET", target_route + "inbox").at("publications").empty(),
                  "Revoked publisher retained import authority");
            check(receiver.request("POST", "/api/projects/" + target_id + "/git-branches").size() ==
                      1,
                  "Revocation destroyed previously imported refs");
        }
        fs::remove_all(root);
        std::cout << "PASS signed Git/LFS transport, pinned identity, replay prevention, "
                     "interrupted import recovery and worktree preservation\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\nIsolated fixture preserved: " << path_string(root) << '\n';
        return 1;
    }
}
