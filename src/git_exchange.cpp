#include "git_lfs.hpp"
#include "backup.hpp"
#include "git_storage.hpp"
#include <regex>
#include <stdexcept>

namespace cysynck {
namespace {
std::string identifier(const Json &raw) {
    auto value = raw.get<std::string>();
    if (!std::regex_match(value, std::regex("[A-Za-z0-9][A-Za-z0-9_-]{0,79}")))
        throw std::runtime_error("Invalid Git exchange identifier");
    return value;
}
Json read_manifest(const fs::path &file, const std::string &project) {
    if (path_is_link(file) || !fs::is_regular_file(file))
        throw std::runtime_error("Git exchange manifest is missing or linked");
    auto raw = Json::parse(read_file(file, 1024 * 1024));
    auto peer = identifier(raw.at("peer")), id = identifier(raw.at("id"));
    auto version = raw.at("version");
    if (!version.is_number_integer() || (version != 1 && version != 2) ||
        raw.at("project") != project || path_string(file.parent_path().filename()) != peer ||
        path_string(file.filename()) != id + (version == 2 ? ".lfs.json" : ".json") ||
        !raw.at("created").is_string() || raw.at("created").get<std::string>().size() > 64 ||
        !raw.at("branch").is_string() || raw.at("branch").get<std::string>().size() > 1024 ||
        !std::regex_match(raw.at("head").get<std::string>(),
                          std::regex("([0-9a-f]{40}|[0-9a-f]{64})")) ||
        !std::regex_match(raw.at("sha256").get<std::string>(), std::regex("[0-9a-f]{64}")))
        throw std::runtime_error("Invalid Git exchange manifest");
    Json result;
    for (const auto &key :
         {"version", "peer", "id", "project", "branch", "head", "sha256", "created"})
        result[key] = raw.at(key);
    if (version == 2)
        result["lfs"] = validate_lfs_manifest(raw.at("lfs"));
    else if (raw.contains("lfs"))
        throw std::runtime_error("Legacy Git bundle cannot include an LFS inventory");
    return result;
}
fs::path peer_store(const fs::path &exchange, const std::string &peer) {
    auto store = project_child(exchange, "peers/" + peer + "/lfs/objects");
    plain_directory(store);
    return store;
}
} // namespace
Json git_exchange_manifest(const fs::path &file, const std::string &project) {
    return read_manifest(file, project);
}
Json Core::git_action(const Json &p, const std::string &action, const Json &data) {
    if (action == "git-status")
        return {
            {"branch", git(p, {"symbolic-ref", "--short", "HEAD"})},
            {"head", git(p, {"rev-parse", "HEAD"})},
            {"changes", git(p, {"status", "--short"})},
            {"tracking", git(p, {"for-each-ref", "--format=%(refname:short) %(objectname:short)",
                                 "refs/remotes/cysynck/"})}};
    if (p.at("mode") != "git-bundles")
        throw std::runtime_error("This operation requires git-bundles mode");
    auto exchange = fs::u8path(p.at("exchange_path").get<std::string>());
    plain_directory(exchange);
    if (action == "export") {
        auto branch = git(p, {"symbolic-ref", "--short", "HEAD"});
        git(p, {"check-ref-format", "refs/heads/" + branch});
        auto peer = identifier(registry_.at("peer_id"));
        auto directory = project_child(exchange, "peers/" + peer);
        plain_directory(directory);
        fs::create_directories(directory);
        project_child(directory, ".cysynck-git.lock");
        DiskLock exchangeLock(directory, ".cysynck-git.lock");
        auto bundle = random_id();
        auto temp = directory / (bundle + ".part"), destination = directory / (bundle + ".bundle");
        bool withLfs = data.value("lfs", p.value("git_lfs", true));
        Json manifest;
        try {
            git(p, {"--no-replace-objects", "bundle", "create", path_string(temp),
                    "refs/heads/" + branch});
            auto heads =
                git(p, {"bundle", "list-heads", path_string(temp), "refs/heads/" + branch});
            auto split = heads.find(' ');
            if (split == std::string::npos || heads.substr(split + 1) != "refs/heads/" + branch)
                throw std::runtime_error("Invalid exported Git bundle head");
            auto head = heads.substr(0, split);
            manifest = {{"version", withLfs ? 2 : 1},  {"peer", peer},          {"id", bundle},
                        {"project", p.at("id")},       {"branch", branch},      {"head", head},
                        {"sha256", sha256_file(temp)}, {"created", timestamp()}};
            if (withLfs) {
                auto objects = lfs_inventory(p, head);
                auto source = lfs_local_store(p);
                auto target = peer_store(exchange, peer);
                for (const auto &object : objects.at("objects"))
                    verify_lfs_object(lfs_object_path(source, object.at("sha256")), object);
                for (const auto &object : objects.at("objects"))
                    copy_lfs_object(lfs_object_path(source, object.at("sha256")), target, object);
                manifest["lfs"] = objects;
            }
            fs::rename(temp, destination);
            // LFS manifests use a suffix that old clients ignore rather than import partially.
            atomic_write(directory / (bundle + (withLfs ? ".lfs.json" : ".json")),
                         manifest.dump(2));
        } catch (...) {
            std::error_code ignored;
            fs::remove(temp, ignored);
            throw;
        }
        event(p.at("id"), "git",
              withLfs ? "Committed branch and Git LFS history exported"
                      : "Committed branch exported");
        return manifest;
    }
    Json incoming = Json::array();
    auto peers = project_child(exchange, "peers");
    plain_directory(peers);
    if (fs::exists(peers))
        for (const auto &dir : fs::directory_iterator(peers))
            if (dir.is_directory() && !path_is_link(dir.path()))
                for (const auto &entry : fs::directory_iterator(dir.path()))
                    if (entry.path().extension() == ".json" && !path_is_link(entry.path())) {
                        try {
                            auto manifest = read_manifest(entry.path(), p.at("id"));
                            auto source = project_child(dir.path(),
                                                        identifier(manifest.at("id")) + ".bundle");
                            if (!fs::is_regular_file(source))
                                continue;
                            if (manifest.at("version") == 2)
                                manifest["lfs_status"] = lfs_availability(
                                    peer_store(exchange, identifier(manifest.at("peer"))),
                                    manifest.at("lfs"));
                            incoming.push_back(manifest);
                        } catch (const std::exception
                                     &) { /* Incomplete or hostile transport metadata. */
                        }
                    }
    if (action == "inbox")
        return incoming;
    if (action != "import")
        throw std::runtime_error("Unknown Git exchange action");
    auto peer = identifier(data.at("peer")), bundle = identifier(data.at("bundle"));
    for (const auto &manifest : incoming)
        if (manifest.at("peer") == peer && manifest.at("id") == bundle) {
            auto branch = manifest.at("branch").get<std::string>();
            git(p, {"check-ref-format", "refs/heads/" + branch});
            auto source = project_child(peers, peer + "/" + bundle + ".bundle");
            auto temp = state_ / ("incoming-" + random_id() + ".bundle");
            try {
                fs::copy_file(source, temp, fs::copy_options::none);
                secure_state_path(temp, false);
                if (sha256_file(temp) != manifest.at("sha256").get<std::string>())
                    throw std::runtime_error("Bundle checksum mismatch");
                git(p, {"bundle", "verify", path_string(temp)});
                auto heads =
                    git(p, {"bundle", "list-heads", path_string(temp), "refs/heads/" + branch});
                if (heads != manifest.at("head").get<std::string>() + " refs/heads/" + branch)
                    throw std::runtime_error("Bundle head mismatch");
                if (manifest.at("version") == 2) {
                    auto incomingStore = peer_store(exchange, peer);
                    auto localStore = lfs_local_store(p);
                    auto objects = validate_lfs_manifest(manifest.at("lfs"));
                    for (const auto &object : objects.at("objects"))
                        verify_lfs_object(lfs_object_path(incomingStore, object.at("sha256")),
                                          object);
                    // Unbundle adds immutable Git objects without changing refs, HEAD or the index.
                    offline_git(p, {"bundle", "unbundle", path_string(temp)});
                    if (lfs_inventory(p, manifest.at("head")) != objects)
                        throw std::runtime_error(
                            "Git LFS inventory does not match the exported branch history");
                    for (const auto &object : objects.at("objects"))
                        copy_lfs_object(lfs_object_path(incomingStore, object.at("sha256")),
                                        localStore, object);
                }
                auto target = "refs/remotes/cysynck/" + peer + "/" + branch;
                git(p, {"-c", "protocol.file.allow=always", "fetch", "--no-tags",
                        "--no-recurse-submodules", "--no-write-fetch-head", path_string(temp),
                        "+refs/heads/" + branch + ":" + target});
                fs::remove(temp);
                event(p.at("id"), "git", "Peer branch imported for review");
                Json result = {{"tracking_ref", target},
                               {"head", manifest.at("head")},
                               {"working_tree_changed", false}};
                if (manifest.at("version") == 2)
                    result["lfs"] = {{"objects", manifest.at("lfs").at("objects").size()},
                                     {"bytes", manifest.at("lfs").at("bytes")},
                                     {"verified", true}};
                return result;
            } catch (...) {
                std::error_code ignored;
                fs::remove(temp, ignored);
                throw;
            }
        }
    throw std::runtime_error("No complete bundle found for this project");
}
} // namespace cysynck
