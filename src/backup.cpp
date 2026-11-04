#include "backup.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
#include <regex>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace cysynck {
void plain_directory(const fs::path &path) {
    if (path_is_link(path) || (fs::exists(path) && !fs::is_directory(path)))
        throw std::runtime_error("Backup directory is linked or is not a directory: " +
                                 path_string(path));
}
static bool digest_name(const std::string &name) {
    return name.size() == 64 && name.find_first_not_of("0123456789abcdef") == std::string::npos;
}
static uintmax_t nonnegative(const Json &value) {
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0))
        throw std::runtime_error("Invalid snapshot numeric metadata");
    return value.get<uintmax_t>();
}
static void add_bytes(uintmax_t &total, uintmax_t value) {
    if (value > std::numeric_limits<uintmax_t>::max() - total)
        throw std::runtime_error("Backup size overflow");
    total += value;
}
struct PathLess {
    bool operator()(const std::string &a, const std::string &b) const {
#ifdef _WIN32
        auto x = fs::u8path(a).native(), y = fs::u8path(b).native();
        return CompareStringOrdinal(x.c_str(), static_cast<int>(x.size()), y.c_str(),
                                    static_cast<int>(y.size()), TRUE) == CSTR_LESS_THAN;
#else
        return a < b;
#endif
    }
};
Json read_snapshot(const fs::path &path, const std::string &project) {
    if (path_is_link(path) || !fs::is_regular_file(path))
        throw std::runtime_error("Snapshot manifest is missing or linked");
    auto m = Json::parse(read_file(path, 64 * 1024 * 1024));
    validate_snapshot(m, path, project);
    return m;
}
void validate_snapshot(const Json &m, const fs::path &path, const std::string &project) {
    auto id = m.at("id").get<std::string>();
    if (!std::regex_match(id, std::regex("[A-Za-z0-9][A-Za-z0-9_-]{0,79}")) ||
        id + ".json" != path_string(path.filename()) || m.at("project") != project ||
        !m.at("files").is_object() || !m.at("created").is_string() || !m.at("reason").is_string())
        throw std::runtime_error("Invalid snapshot identity or metadata");
    if (m.contains("created_ns") && nonnegative(m.at("created_ns")) >
                                        static_cast<uintmax_t>(std::numeric_limits<int64_t>::max()))
        throw std::runtime_error("Invalid snapshot creation order");
    std::set<std::string, PathLess> names, directories;
    uintmax_t bytes = 0;
    for (const auto &[name, entry] : m.at("files").items()) {
        auto relative = fs::u8path(name);
        if (name.empty() || name.find('\0') != std::string::npos || name.front() == '/' ||
            name.back() == '/' || name.find("//") != std::string::npos ||
            name.find_first_of("\\:") != std::string::npos || relative.is_absolute())
            throw std::runtime_error("Invalid snapshot relative path");
        for (const auto &part : relative) {
            auto segment = path_string(part);
            if (segment.empty() || segment == "." || segment == "..")
                throw std::runtime_error("Invalid snapshot path component");
#ifdef _WIN32
            if (segment.back() == '.' || segment.back() == ' ' ||
                segment.find_first_of("<>\"|?*") != std::string::npos ||
                std::any_of(segment.begin(), segment.end(),
                            [](unsigned char c) { return c < 32; }) ||
                std::regex_match(segment, std::regex("(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?",
                                                     std::regex::icase)))
                throw std::runtime_error("Snapshot contains a reserved Windows filename");
#endif
        }
        if (!names.insert(name).second)
            throw std::runtime_error("Snapshot file paths collide on this platform");
        for (auto parent = relative.parent_path(); !parent.empty(); parent = parent.parent_path())
            directories.insert(path_string(parent));
        auto digest = entry.at("sha256").get<std::string>();
        if (!digest_name(digest) || nonnegative(entry.at("permissions")) > 07777)
            throw std::runtime_error("Invalid snapshot object or permissions");
        add_bytes(bytes, nonnegative(entry.at("size")));
    }
    for (const auto &name : names)
        if (directories.contains(name))
            throw std::runtime_error("Snapshot contains a file/directory path collision");
    if (nonnegative(m.at("file_count")) != m.at("files").size() ||
        nonnegative(m.at("bytes")) != bytes)
        throw std::runtime_error("Snapshot file count or byte total is inconsistent");
}
static Json choose_snapshot(const Json &list, const Json &data) {
    auto selected = data.value("snapshot", std::string{});
    for (const auto &m : list)
        if (selected.empty() || m.at("id") == selected)
            return m;
    throw std::runtime_error("No matching snapshot");
}
static Json inspect_objects(const fs::path &state, const Json &manifest) {
    plain_directory(state / "objects");
    Json issues = Json::array();
    std::map<std::string, Json> checked;
    uintmax_t verified = 0;
    for (const auto &[name, entry] : manifest.at("files").items()) {
        auto digest = entry.at("sha256").get<std::string>();
        if (!checked.contains(digest)) {
            Json info = Json::object();
            auto object = state / "objects" / digest;
            try {
                if (path_is_link(object) || !fs::is_regular_file(object))
                    throw std::runtime_error("Object missing, linked or not a regular file");
                if (sha256_file(object) != digest)
                    throw std::runtime_error("SHA-256 mismatch");
                info["size"] = fs::file_size(object);
                ++verified;
            } catch (const std::exception &e) {
                info["error"] = e.what();
            }
            checked[digest] = info;
        }
        const auto &info = checked.at(digest);
        if (info.contains("error") || info.at("size") != entry.at("size"))
            issues.push_back({{"path", name},
                              {"sha256", digest},
                              {"error", info.value("error", "Object size differs from manifest")}});
    }
    return {{"snapshot", manifest.at("id")}, {"project", manifest.at("project")},
            {"ok", issues.empty()},          {"files", manifest.at("file_count")},
            {"bytes", manifest.at("bytes")}, {"objects_checked", checked.size()},
            {"objects_verified", verified},  {"issues", issues},
            {"checked_at", timestamp()}};
}
Json Core::verify(const Json &p, const Json &data) {
    return inspect_objects(state_, choose_snapshot(snapshots(p.at("id")), data));
}
static bool overlaps(const fs::path &a, const fs::path &b) {
    auto x = a.begin(), y = b.begin();
    for (; x != a.end() && y != b.end(); ++x, ++y) {
        PathLess less;
        if (less(path_string(*x), path_string(*y)) || less(path_string(*y), path_string(*x)))
            return false;
    }
    return true;
}
fs::path restore_target(const fs::path &state, const Json &projects, const Json &data) {
    auto name = data.at("destination").get<std::string>();
    if (name.empty() || name.find('\0') != std::string::npos)
        throw std::runtime_error("Choose a new restoration directory");
    auto requested = fs::absolute(fs::u8path(name)).lexically_normal();
    for (auto current = requested; !current.empty(); current = current.parent_path()) {
        if (path_is_link(current))
            throw std::runtime_error("Restore path contains a link or junction");
        if (fs::exists(current) && !fs::is_directory(current))
            throw std::runtime_error("Restore path contains an existing file");
        if (current == current.parent_path())
            break;
    }
    auto target = fs::weakly_canonical(requested);
    if (fs::exists(target) || overlaps(target, state))
        throw std::runtime_error("Restore into a separate new directory");
    for (const auto &p : projects)
        for (auto key : {"root", "exchange_path"}) {
            auto value = p.value(key, std::string{});
            if (!value.empty() && overlaps(target, fs::weakly_canonical(fs::u8path(value))))
                throw std::runtime_error("Restoration overlaps a project or exchange directory");
        }
    return target;
}
Json Core::restore_plan(const Json &p, const Json &data) {
    auto target = restore_target(state_, registry_.at("projects"), data);
    auto manifest = choose_snapshot(snapshots(p.at("id")), data);
    auto result = inspect_objects(state_, manifest);
    result["destination"] = path_string(target);
    result["entries"] = Json::array();
    for (const auto &[name, entry] : manifest.at("files").items())
        result["entries"].push_back({{"path", name}, {"size", entry.at("size")}});
    return result;
}
Json Core::restore(const Json &p, const Json &data) {
    auto plan = restore_plan(p, data);
    if (!plan.at("ok").get<bool>())
        throw std::runtime_error("Backup integrity check failed; destination was not created");
    auto manifest = choose_snapshot(snapshots(p.at("id")), {{"snapshot", plan.at("snapshot")}});
    auto target = fs::u8path(plan.at("destination").get<std::string>());
    // Claim the exact destination without replacing an existing directory. The empty parent
    // directories may remain if a subsequent disk/permission error interrupts restoration.
    fs::create_directories(target.parent_path());
    if (!fs::create_directory(target))
        throw std::runtime_error("Restoration destination already exists");
    uintmax_t written = 0;
    for (const auto &[name, entry] : manifest.at("files").items()) {
        auto destination = project_child(target, name);
        auto object = state_ / "objects" / entry.at("sha256").get<std::string>();
        fs::create_directories(destination.parent_path());
        fs::copy_file(object, destination);
        if (sha256_file(destination) != entry.at("sha256").get<std::string>())
            throw std::runtime_error("Restored file checksum mismatch; restoration incomplete");
        fs::permissions(destination,
                        static_cast<fs::perms>(entry.at("permissions").get<unsigned>()));
        ++written;
    }
    event(p.at("id"), "restore", "Snapshot restored to a new directory");
    return {
        {"destination", path_string(target)}, {"snapshot", manifest.at("id")}, {"files", written}};
}
Json Core::storage(const Json &data) {
    // Called under the same process/disk locks as backup publication and retention.
    Json errors = Json::array(), ignored = Json::array(), manifests = Json::array();
    std::set<std::string> references;
    std::map<std::string, fs::path> candidates;
    Json objects = Json::array();
    uintmax_t logical = 0, stored = 0, unused = 0, count = 0, retained = 0;
    auto directory = state_ / "snapshots";
    try {
        plain_directory(directory);
        if (fs::exists(directory))
            for (const auto &project : fs::directory_iterator(directory)) {
                try {
                    plain_directory(project.path());
                    if (!fs::is_directory(project.path()))
                        throw std::runtime_error("Unexpected file in snapshot catalog");
                    for (const auto &file : fs::directory_iterator(project.path())) {
                        try {
                            auto m =
                                read_snapshot(file.path(), path_string(project.path().filename()));
                            manifests.push_back(
                                {path_string(file.path().lexically_relative(state_)),
                                 sha256(m.dump())});
                            ++count;
                            add_bytes(logical, m.at("bytes").get<uintmax_t>());
                            for (const auto &[name, entry] : m.at("files").items())
                                references.insert(entry.at("sha256").get<std::string>());
                        } catch (const std::exception &e) {
                            errors.push_back(
                                {{"path", path_string(file.path())}, {"error", e.what()}});
                        }
                    }
                } catch (const std::exception &e) {
                    errors.push_back({{"path", path_string(project.path())}, {"error", e.what()}});
                }
            }
        plain_directory(state_ / "objects");
        for (const auto &file : fs::directory_iterator(state_ / "objects")) {
            auto name = path_string(file.path().filename());
            if (path_is_link(file.path())) {
                errors.push_back({{"path", name}, {"error", "Linked object entry"}});
                continue;
            }
            if (!digest_name(name) || !file.is_regular_file()) {
                ignored.push_back(name);
                continue;
            }
            auto bytes = file.file_size();
            objects.push_back({name, bytes, file.last_write_time().time_since_epoch().count()});
            add_bytes(stored, bytes);
            if (references.contains(name))
                ++retained;
            else {
                candidates[name] = file.path();
                add_bytes(unused, bytes);
            }
        }
    } catch (const std::exception &e) {
        errors.push_back({{"error", e.what()}});
    }
    std::sort(manifests.begin(), manifests.end());
    std::sort(objects.begin(), objects.end());
    auto token =
        sha256(Json{{"state", path_string(state_)}, {"manifests", manifests}, {"objects", objects}}
                   .dump());
    Json result = {{"snapshots", count},
                   {"logical_bytes", logical},
                   {"stored_bytes", stored},
                   {"objects", objects.size()},
                   {"referenced_objects", retained},
                   {"missing_objects", references.size() - retained},
                   {"unused_objects", candidates.size()},
                   {"reclaimable_bytes", unused},
                   {"ignored_entries", ignored},
                   {"errors", errors},
                   {"can_prune", errors.empty()},
                   {"plan", token},
                   {"applied", false}};
    if (data.value("apply", false)) {
        if (!errors.empty())
            throw std::runtime_error("Storage catalog is incomplete; cleanup refused");
        if (data.value("plan", std::string{}) != token)
            throw std::runtime_error("Storage plan changed or is missing; preview cleanup again");
        uintmax_t removed = 0, reclaimed = 0;
        Json failures = Json::array();
        for (const auto &[digest, path] : candidates) {
            try {
                // Never recurse, follow links or touch objects referenced by any manifest.
                if (path_is_link(path) || !fs::is_regular_file(path))
                    throw std::runtime_error("Candidate changed during cleanup");
                auto bytes = fs::file_size(path);
                if (!fs::remove(path))
                    throw std::runtime_error("Candidate disappeared during cleanup");
                ++removed;
                add_bytes(reclaimed, bytes);
            } catch (const std::exception &e) {
                failures.push_back({{"sha256", digest}, {"error", e.what()}});
            }
        }
        result["applied"] = true;
        result["removed_objects"] = removed;
        result["reclaimed_bytes"] = reclaimed;
        result["failures"] = failures;
        event("", "storage", "Removed " + std::to_string(removed) + " unreferenced backup objects");
    }
    return result;
}
} // namespace cysynck
