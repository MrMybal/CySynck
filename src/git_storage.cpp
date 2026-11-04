#include "git_storage.hpp"
#include "backup.hpp"
#include "file_time.hpp"
#include <algorithm>
#include <limits>
#include <regex>
#include <set>

namespace cysynck {
namespace {
bool identifier(const std::string &value) {
    return std::regex_match(value, std::regex("[A-Za-z0-9][A-Za-z0-9_-]{0,79}"));
}
bool hex(const std::string &value, size_t length) {
    return value.size() == length && value.find_first_not_of("0123456789abcdef") == value.npos;
}
void add(uintmax_t &total, uintmax_t bytes) {
    if (bytes > std::numeric_limits<uintmax_t>::max() - total)
        throw std::runtime_error("Git exchange storage size overflow");
    total += bytes;
}
Json identity(const fs::path &path) {
    if (path_is_link(path) || !fs::is_regular_file(path))
        throw std::runtime_error("Git exchange file is missing, linked or not regular");
    return {{"bytes", fs::file_size(path)},
            {"modified", std::to_string(file_time_ticks(fs::last_write_time(path)))}};
}
struct Catalog {
    Json exports = Json::array(), errors = Json::array(), ignored = Json::array();
    Json files = Json::object(), manifests = Json::object(), objects = Json::object();
    std::map<std::string, fs::path> unused;
    uintmax_t stored = 0, reclaimable = 0, retained = 0, missing = 0;
};
Catalog scan(const fs::path &directory, const std::string &project, const std::string &peer,
             const std::set<std::string> &selected) {
    Catalog c;
    std::map<std::string, uintmax_t> references;
    std::set<std::string> found;
    size_t entries = 0;
    auto budget = [&] {
        if (++entries > 100000)
            throw std::runtime_error("Git exchange storage catalog exceeds 100000 entries");
    };
    auto error = [&](const fs::path &path, const std::exception &e) {
        c.errors.push_back({{"path", path_string(path)}, {"error", e.what()}});
    };
    try {
        plain_directory(directory);
        if (fs::exists(directory)) {
            for (const auto &entry : fs::directory_iterator(directory)) {
                budget();
                auto path = entry.path();
                auto name = path_string(path.filename());
                if (name == ".cysynck-git.lock")
                    continue;
                try {
                    if (path_is_link(path))
                        throw std::runtime_error("Linked Git exchange entry prevents cleanup");
                    if (name == "lfs") {
                        plain_directory(path);
                        continue;
                    }
                    if (!entry.is_regular_file()) {
                        c.ignored.push_back(name);
                        continue;
                    }
                    c.files[name] = identity(path);
                    if (path.extension() != ".json") {
                        c.ignored.push_back(name);
                        continue;
                    }
                    if (c.manifests.size() >= 10000)
                        throw std::runtime_error("Git export catalog exceeds 10000 manifests");
                    auto record = git_exchange_manifest(path, project);
                    auto id = record.at("id").get<std::string>();
                    if (record.at("peer") != peer || !found.insert(id).second)
                        throw std::runtime_error("Conflicting Git export identity");
                    auto bundle = project_child(directory, id + ".bundle");
                    auto info = identity(bundle);
                    c.files[id + ".bundle"] = info;
                    c.manifests[name] = {{"record", record}, {"sha256", sha256_file(path)}};
                    bool remove = selected.contains(id);
                    if (remove) {
                        if (record.at("sha256") != sha256_file(bundle))
                            throw std::runtime_error("Selected Git bundle checksum mismatch");
                        add(c.reclaimable, info.at("bytes").get<uintmax_t>());
                        add(c.reclaimable, c.files.at(name).at("bytes").get<uintmax_t>());
                    }
                    uintmax_t lfsBytes = 0, lfsCount = 0;
                    if (record.at("version") == 2) {
                        lfsBytes = record.at("lfs").at("bytes");
                        lfsCount = record.at("lfs").at("objects").size();
                        if (!remove)
                            for (const auto &object : record.at("lfs").at("objects")) {
                                auto digest = object.at("sha256").get<std::string>();
                                auto bytes = object.at("size").get<uintmax_t>();
                                if (references.contains(digest) && references.at(digest) != bytes)
                                    throw std::runtime_error(
                                        "Conflicting retained LFS object sizes");
                                references[digest] = bytes;
                            }
                    }
                    c.exports.push_back({{"id", id},
                                         {"branch", record.at("branch")},
                                         {"head", record.at("head")},
                                         {"created", record.at("created")},
                                         {"version", record.at("version")},
                                         {"selected", remove},
                                         {"bundle_bytes", info.at("bytes")},
                                         {"lfs_objects", lfsCount},
                                         {"lfs_bytes", lfsBytes}});
                } catch (const std::exception &e) {
                    error(path, e);
                }
            }
            // Only fixed-depth, content-addressed files belong to the LFS object namespace.
            auto store = project_child(directory, "lfs/objects");
            plain_directory(store);
            if (fs::exists(store))
                for (const auto &first : fs::directory_iterator(store)) {
                    budget();
                    auto a = path_string(first.path().filename());
                    if (path_is_link(first.path()))
                        throw std::runtime_error("Linked LFS shard prevents cleanup");
                    if (!hex(a, 2) || !first.is_directory()) {
                        c.ignored.push_back("lfs/objects/" + a);
                        continue;
                    }
                    for (const auto &second : fs::directory_iterator(first.path())) {
                        budget();
                        auto b = path_string(second.path().filename());
                        if (path_is_link(second.path()))
                            throw std::runtime_error("Linked LFS shard prevents cleanup");
                        if (!hex(b, 2) || !second.is_directory()) {
                            c.ignored.push_back("lfs/objects/" + a + "/" + b);
                            continue;
                        }
                        for (const auto &entry : fs::directory_iterator(second.path())) {
                            budget();
                            auto digest = path_string(entry.path().filename());
                            if (path_is_link(entry.path()))
                                throw std::runtime_error("Linked LFS object prevents cleanup");
                            if (!hex(digest, 64) || digest.substr(0, 4) != a + b ||
                                !entry.is_regular_file()) {
                                c.ignored.push_back("lfs/objects/" + a + "/" + b + "/" + digest);
                                continue;
                            }
                            auto info = identity(entry.path());
                            c.objects[digest] = info;
                            auto bytes = info.at("bytes").get<uintmax_t>();
                            add(c.stored, bytes);
                            if (references.contains(digest)) {
                                ++c.retained;
                                if (references.at(digest) != bytes)
                                    throw std::runtime_error("Retained LFS object size mismatch");
                            } else {
                                c.unused[digest] = entry.path();
                                add(c.reclaimable, bytes);
                            }
                        }
                    }
                }
        }
        for (const auto &id : selected)
            if (!found.contains(id))
                throw std::runtime_error("Selected Git export is missing: " + id);
        for (const auto &[digest, bytes] : references)
            if (!c.objects.contains(digest))
                ++c.missing;
        if (c.missing)
            throw std::runtime_error("Retained Git exports have missing LFS objects");
    } catch (const std::exception &e) {
        error(directory, e);
    }
    // Indexed bundles are owned exports, rather than ignored files.
    c.ignored.erase(std::remove_if(c.ignored.begin(), c.ignored.end(),
                                   [&](const Json &name) {
                                       auto file = name.get<std::string>();
                                       return file.ends_with(".bundle") &&
                                              found.contains(file.substr(0, file.size() - 7));
                                   }),
                    c.ignored.end());
    std::sort(c.exports.begin(), c.exports.end(),
              [](const Json &a, const Json &b) { return a.at("id") < b.at("id"); });
    std::sort(c.ignored.begin(), c.ignored.end());
    return c;
}
} // namespace
Json git_exchange_storage(const Json &project, const std::string &peer, const Json &data) {
    if (project.at("mode") != "git-bundles" || !identifier(peer))
        throw std::runtime_error("Git storage requires git-bundles mode and a valid local peer");
    auto raw = data.value("exports", Json::array());
    if (!raw.is_array() || raw.size() > 10000)
        throw std::runtime_error("Expected up to 10000 selected Git export IDs");
    std::set<std::string> selected;
    for (const auto &value : raw) {
        auto id = value.get<std::string>();
        if (!identifier(id) || !selected.insert(id).second)
            throw std::runtime_error("Invalid or duplicate selected Git export ID");
    }
    auto directory =
        project_child(fs::u8path(project.at("exchange_path").get<std::string>()), "peers/" + peer);
    plain_directory(directory);
    project_child(directory, ".cysynck-git.lock");
    std::unique_ptr<DiskLock> lock;
    if (fs::exists(directory))
        lock = std::make_unique<DiskLock>(directory, ".cysynck-git.lock");
    auto c = scan(directory, project.at("id"), peer, selected);
    auto token = sha256(Json{
        {"directory", path_string(directory)},
        {"project", project.at("id")},
        {"peer", peer},
        {"selected", selected},
        {"files", c.files},
        {"manifests", c.manifests},
        {"objects", c.objects},
        {"ignored",
         c.ignored}}.dump());
    Json result = {{"peer", peer},
                   {"project", project.at("id")},
                   {"exports", c.exports},
                   {"selected_exports", selected},
                   {"objects", c.objects.size()},
                   {"stored_lfs_bytes", c.stored},
                   {"retained_objects", c.retained},
                   {"missing_objects", c.missing},
                   {"unused_objects", c.unused.size()},
                   {"reclaimable_bytes", c.reclaimable},
                   {"ignored_entries", c.ignored},
                   {"errors", c.errors},
                   {"can_prune", c.errors.empty()},
                   {"plan", token},
                   {"applied", false}};
    if (!data.value("apply", false))
        return result;
    if (!c.errors.empty())
        throw std::runtime_error("Git storage catalog is incomplete; cleanup refused");
    if (data.value("plan", std::string{}) != token)
        throw std::runtime_error("Git storage plan changed or is missing; preview cleanup again");
    uintmax_t removedExports = 0, removedObjects = 0, reclaimed = 0;
    Json failures = Json::array();
    for (const auto &[name, metadata] : c.manifests.items()) {
        const auto &record = metadata.at("record");
        auto id = record.at("id").get<std::string>();
        if (!selected.contains(id))
            continue;
        try {
            auto manifest = project_child(directory, name),
                 bundle = project_child(directory, id + ".bundle");
            if (identity(manifest) != c.files.at(name) ||
                sha256_file(manifest) != metadata.at("sha256").get<std::string>() ||
                identity(bundle) != c.files.at(id + ".bundle") ||
                sha256_file(bundle) != record.at("sha256").get<std::string>())
                throw std::runtime_error("Selected Git export changed during cleanup");
            // Unpublish first. A partial failure preserves objects and reports the orphan bundle.
            if (!fs::remove(manifest))
                throw std::runtime_error("Git manifest disappeared during cleanup");
            ++removedExports;
            add(reclaimed, c.files.at(name).at("bytes").get<uintmax_t>());
            if (!fs::remove(bundle))
                throw std::runtime_error("Git bundle disappeared during cleanup");
            add(reclaimed, c.files.at(id + ".bundle").at("bytes").get<uintmax_t>());
        } catch (const std::exception &e) {
            failures.push_back({{"export", id}, {"error", e.what()}});
            break;
        }
    }
    if (failures.empty()) {
        auto remaining = scan(directory, project.at("id"), peer, {});
        if (!remaining.errors.empty())
            failures = remaining.errors;
        else
            for (const auto &[digest, path] : c.unused) {
                try {
                    if (!remaining.unused.contains(digest) ||
                        remaining.objects.at(digest) != c.objects.at(digest) ||
                        identity(project_child(directory, path_string(path.lexically_relative(
                                                              directory)))) != c.objects.at(digest))
                        throw std::runtime_error("LFS cleanup candidate changed");
                    if (!fs::remove(path))
                        throw std::runtime_error("LFS cleanup candidate disappeared");
                    ++removedObjects;
                    add(reclaimed, c.objects.at(digest).at("bytes").get<uintmax_t>());
                } catch (const std::exception &e) {
                    failures.push_back({{"sha256", digest}, {"error", e.what()}});
                }
            }
    }
    result.update({{"applied", true},
                   {"removed_exports", removedExports},
                   {"removed_objects", removedObjects},
                   {"reclaimed_bytes", reclaimed},
                   {"failures", failures}});
    return result;
}
} // namespace cysynck
