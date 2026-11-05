#include "backup.hpp"
#include "backup_data.hpp"
#include "file_time.hpp"
#include "snapshot.hpp"
#include <algorithm>
#include <cstdio>
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
    auto format = m.contains("format") ? nonnegative(m.at("format")) : 1;
    if (format != 1 && format != 2)
        throw std::runtime_error("Unsupported snapshot format");
    std::set<std::string, PathLess> names, directories;
    uintmax_t bytes = 0, block_files = 0, block_bytes = 0, block_count = 0, cdc_files = 0;
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
        auto references = backup_references(entry);
        if (block_entry(entry)) {
            if (format != 2)
                throw std::runtime_error("Block backup requires snapshot format 2");
            ++block_files;
            if (entry.at("storage") == "blocks-cdc-v1")
                ++cdc_files;
            add_bytes(block_bytes, nonnegative(entry.at("size")));
            add_bytes(block_count, references.size());
        }
        add_bytes(bytes, nonnegative(entry.at("size")));
    }
    for (const auto &name : names)
        if (directories.contains(name))
            throw std::runtime_error("Snapshot contains a file/directory path collision");
    if (nonnegative(m.at("file_count")) != m.at("files").size() ||
        nonnegative(m.at("bytes")) != bytes)
        throw std::runtime_error("Snapshot file count or byte total is inconsistent");
    if (m.contains("block_storage")) {
        const auto &stats = m.at("block_storage");
        uintmax_t stored = nonnegative(stats.at("new_block_bytes"));
        add_bytes(stored, nonnegative(stats.at("reused_block_bytes")));
        uintmax_t count = nonnegative(stats.at("new_blocks"));
        add_bytes(count, nonnegative(stats.at("reused_blocks")));
        if (nonnegative(stats.at("block_size")) != backup_block_size ||
            nonnegative(stats.at("chunked_files")) != block_files || stored != block_bytes ||
            count != block_count ||
            (stats.contains("content_defined_files") &&
             nonnegative(stats.at("content_defined_files")) != cdc_files))
            throw std::runtime_error("Snapshot block storage summary is inconsistent");
    }
}
static Json choose_snapshot(const Json &list, const Json &data) {
    auto selected = data.value("snapshot", std::string{});
    for (const auto &m : list)
        if (selected.empty() || m.at("id") == selected)
            return m;
    throw std::runtime_error("No matching snapshot");
}
static Json inspect_objects(const fs::path &state, const Json &manifest) {
    Json issues = Json::array();
    std::map<std::string, std::string> checked_files;
    std::set<std::string> checked, verified;
    for (const auto &[name, entry] : manifest.at("files").items()) {
        auto identity = sha256(
            Json{{"sha256", entry.at("sha256")}, {"references", backup_references(entry)}}.dump());
        if (!checked_files.contains(identity)) {
            std::string error;
            try {
                stream_backup_file(state, entry, {}, [&](const Json &ref, bool ok) {
                    auto id = ref.at("namespace").get<std::string>() + "/" +
                              ref.at("sha256").get<std::string>();
                    checked.insert(id);
                    if (ok)
                        verified.insert(id);
                });
            } catch (const std::exception &e) {
                error = e.what();
            }
            checked_files[identity] = error;
        }
        if (!checked_files.at(identity).empty())
            issues.push_back({{"path", name},
                              {"sha256", entry.at("sha256")},
                              {"error", checked_files.at(identity)}});
    }
    return {{"snapshot", manifest.at("id")},
            {"project", manifest.at("project")},
            {"ok", issues.empty()},
            {"files", manifest.at("file_count")},
            {"bytes", manifest.at("bytes")},
            {"objects_checked", checked.size()},
            {"objects_verified", verified.size()},
            {"issues", issues},
            {"checked_at", timestamp()}};
}
Json Core::verify(const Json &p, const Json &data) {
    auto manifest = select_snapshot_files(data.value("snapshot", "").empty()
                                              ? choose_snapshot(snapshots(p.at("id")), data)
                                              : snapshot_manifest(state_, p, data.at("snapshot")),
                                          data);
    auto result = inspect_objects(state_, manifest);
    result["manifest_hash"] = manifest.at("manifest_hash");
    result["selection"] = manifest.at("selection");
    return result;
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
    auto manifest = select_snapshot_files(data.value("snapshot", "").empty()
                                              ? choose_snapshot(snapshots(p.at("id")), data)
                                              : snapshot_manifest(state_, p, data.at("snapshot")),
                                          data);
    auto result = inspect_objects(state_, manifest);
    result["destination"] = path_string(target);
    result["manifest_hash"] = manifest.at("manifest_hash");
    result["selection"] = manifest.at("selection");
    result["plan"] = result.at("ok").get<bool>()
                         ? Json(sha256(Json{{"schema", 1},
                                            {"project", p.at("id")},
                                            {"root", p.at("root")},
                                            {"state", path_string(state_)},
                                            {"destination", path_string(target)},
                                            {"manifest_hash", manifest.at("manifest_hash")},
                                            {"selection", manifest.at("selection")}}
                                           .dump()))
                         : Json();
    result["entries"] = Json::array();
    for (const auto &[name, entry] : manifest.at("files").items())
        result["entries"].push_back({{"path", name}, {"size", entry.at("size")}});
    return result;
}
Json Core::restore(const Json &p, const Json &data) {
    auto plan = restore_plan(p, data);
    if (!plan.at("ok").get<bool>())
        throw std::runtime_error("Backup integrity check failed; destination was not created");
    if ((data.contains("paths") || data.contains("plan")) &&
        (!data.contains("plan") || data.at("plan") != plan.at("plan")))
        throw std::runtime_error("Restoration selection or preview changed; preview again");
    auto manifest = select_snapshot_files(snapshot_manifest(state_, p, plan.at("snapshot")), data);
    if (manifest.at("manifest_hash") != plan.at("manifest_hash"))
        throw std::runtime_error("Snapshot changed during restoration preview");
    auto target = fs::u8path(plan.at("destination").get<std::string>());
    // Claim the exact destination without replacing an existing directory. The empty parent
    // directories may remain if a subsequent disk/permission error interrupts restoration.
    fs::create_directories(target.parent_path());
    if (!fs::create_directory(target))
        throw std::runtime_error("Restoration destination already exists");
    uintmax_t written = 0;
    for (const auto &[name, entry] : manifest.at("files").items()) {
        auto destination = project_child(target, name);
        fs::create_directories(destination.parent_path());
        // C11 exclusive mode claims the file without replacing an existing file or link.
        FILE *file = nullptr;
#ifdef _WIN32
        _wfopen_s(&file, destination.c_str(), L"wbx");
#else
        file = std::fopen(destination.c_str(), "wbx");
#endif
        if (!file)
            throw std::runtime_error("Cannot create a new restored file exclusively");
        std::unique_ptr<FILE, decltype(&std::fclose)> output(file, std::fclose);
        secure_state_path(destination, false);
        stream_backup_file(state_, entry, [&](const char *bytes, size_t size) {
            if (std::fwrite(bytes, 1, size, output.get()) != size)
                throw std::runtime_error("Restored file write failed; restoration incomplete");
        });
        if (std::fflush(output.get()) != 0)
            throw std::runtime_error("Restored file write failed; restoration incomplete");
        output.reset();
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
    uintmax_t logical = 0, stored = 0, unused = 0, count = 0, retained = 0, referenced_bytes = 0;
    uintmax_t block_objects = 0, block_bytes = 0, chunked_files = 0, cdc_files = 0;
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
                            for (const auto &[name, entry] : m.at("files").items()) {
                                chunked_files += block_entry(entry);
                                cdc_files += entry.value("storage", "whole") == "blocks-cdc-v1";
                                for (const auto &ref : backup_references(entry))
                                    references.insert(ref.at("namespace").get<std::string>() + "/" +
                                                      ref.at("sha256").get<std::string>());
                            }
                        } catch (const std::exception &e) {
                            errors.push_back(
                                {{"path", path_string(file.path())}, {"error", e.what()}});
                        }
                    }
                } catch (const std::exception &e) {
                    errors.push_back({{"path", path_string(project.path())}, {"error", e.what()}});
                }
            }
        for (const auto &space : {"objects", "blocks"}) {
            auto store = state_ / space;
            plain_directory(store);
            if (!fs::exists(store))
                continue;
            for (const auto &file : fs::directory_iterator(store)) {
                auto name = path_string(file.path().filename());
                auto identity = std::string(space) + "/" + name;
                if (path_is_link(file.path())) {
                    errors.push_back({{"path", identity}, {"error", "Linked object entry"}});
                    continue;
                }
                if (!digest_name(name) || !file.is_regular_file()) {
                    ignored.push_back(identity);
                    continue;
                }
                auto bytes = file.file_size();
                objects.push_back({identity, bytes, file_time_ticks(file.last_write_time())});
                add_bytes(stored, bytes);
                if (std::string(space) == "blocks") {
                    ++block_objects;
                    add_bytes(block_bytes, bytes);
                }
                if (references.contains(identity)) {
                    ++retained;
                    add_bytes(referenced_bytes, bytes);
                } else {
                    candidates[identity] = file.path();
                    add_bytes(unused, bytes);
                }
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
                   {"referenced_bytes", referenced_bytes},
                   {"block_objects", block_objects},
                   {"block_stored_bytes", block_bytes},
                   {"chunked_files", chunked_files},
                   {"content_defined_files", cdc_files},
                   {"deduplication_complete", errors.empty() && references.size() == retained},
                   {"deduplicated_bytes",
                    errors.empty() && references.size() == retained && logical > referenced_bytes
                        ? logical - referenced_bytes
                        : 0},
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
