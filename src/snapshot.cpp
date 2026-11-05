#include "snapshot.hpp"
#include "backup.hpp"
#include "diff.hpp"
#include <algorithm>
#include <regex>
#include <set>

namespace cysynck {
namespace {
void relative_path(const std::string &name) {
    if (name.empty() || name.size() > 4096 || name.front() == '/' ||
        name.find_first_of("\\:\n\r") != name.npos || name.find('\0') != name.npos ||
        name.find("//") != name.npos)
        throw std::runtime_error("Choose a relative snapshot path using forward slashes");
    for (const auto &part : fs::u8path(name))
        if (part == "." || part == "..")
            throw std::runtime_error("Snapshot paths cannot contain dot components");
    (void)Json(name).dump();
}
std::string folder_prefix(const Json &data) {
    auto prefix = data.value("prefix", std::string{});
    if (!prefix.empty()) {
        relative_path(prefix);
        if (prefix.back() != '/')
            prefix += '/';
    }
    return prefix;
}
size_t bound(const Json &data, const char *key, size_t fallback, size_t maximum) {
    if (!data.contains(key))
        return fallback;
    const auto &value = data.at(key);
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0) ||
        value.get<uintmax_t>() > maximum || (std::string(key) == "limit" && value == 0))
        throw std::runtime_error("Invalid snapshot page bound");
    return value.get<size_t>();
}
void pinned(const Json &data, const char *key, const std::string &hash) {
    if (data.contains(key) && data.at(key) != hash)
        throw std::runtime_error("Snapshot manifest changed; load the first page again");
}
Json metadata(const std::string &path, const Json &entry) {
    if (entry.is_null())
        return nullptr;
    return {{"path", path},
            {"size", entry.at("size")},
            {"sha256", entry.at("sha256")},
            {"permissions", entry.at("permissions")},
            {"storage", entry.value("storage", "whole")}};
}
} // namespace
Json snapshot_manifest(const fs::path &state, const Json &project, const std::string &id) {
    if (!std::regex_match(id, std::regex("[A-Za-z0-9][A-Za-z0-9_-]{0,79}")))
        throw std::runtime_error("Choose a saved snapshot identifier");
    auto path = project_child(state, "snapshots/" + project.at("id").get<std::string>() + "/" + id +
                                         ".json");
    return read_snapshot(path, project.at("id"));
}
Json select_snapshot_files(const Json &manifest, const Json &data) {
    auto result = manifest;
    result["manifest_hash"] = sha256(manifest.dump());
    pinned(data, "manifest_hash", result.at("manifest_hash"));
    result["selection"] = nullptr;
    if (!data.contains("paths"))
        return result;
    const auto &paths = data.at("paths");
    if (!paths.is_array() || paths.empty() || paths.size() > 256)
        throw std::runtime_error("Select between 1 and 256 snapshot files or folders");
    std::set<std::string> selectors;
    size_t length = 0;
    for (const auto &value : paths) {
        auto name = value.get<std::string>();
        relative_path(name);
        length += name.size();
        if (length > 16384)
            throw std::runtime_error("Snapshot selection exceeds 16 KiB");
        selectors.insert(name);
    }
    Json files = Json::object();
    uintmax_t bytes = 0;
    const auto &entries = manifest.at("files").get_ref<const Json::object_t &>();
    for (const auto &name : selectors) {
        bool found = false;
        for (auto it = entries.lower_bound(name); it != entries.end(); ++it) {
            const auto &[path, entry] = *it;
            if (!(name.back() == '/' ? path.starts_with(name) : path == name))
                break;
            found = true;
            if (!files.contains(path)) {
                bytes += entry.at("size").get<uintmax_t>();
                files[path] = entry;
            }
        }
        if (!found)
            throw std::runtime_error(
                "Selected snapshot path is missing; folders need a trailing slash");
    }
    result["selection"] = selectors;
    result["files"] = files;
    result["file_count"] = files.size();
    result["bytes"] = bytes;
    result.erase("block_storage");
    return result;
}
Json snapshot_query(const fs::path &state, const Json &project, const std::string &action,
                    const Json &data) {
    auto offset = bound(data, "offset", 0, 1000000), limit = bound(data, "limit", 100, 200);
    auto prefix = folder_prefix(data);
    if (action == "snapshot-files") {
        auto manifest = snapshot_manifest(state, project, data.at("snapshot"));
        auto hash = sha256(manifest.dump());
        pinned(data, "manifest_hash", hash);
        Json page = Json::array();
        size_t count = 0;
        uintmax_t bytes = 0;
        for (const auto &[path, entry] : manifest.at("files").items())
            if (path.starts_with(prefix)) {
                if (count++ >= offset && page.size() < limit)
                    page.push_back(metadata(path, entry));
                bytes += entry.at("size").get<uintmax_t>();
            }
        if (page.dump().size() > 2 * 1024 * 1024)
            throw std::runtime_error("Snapshot page exceeds 2 MiB; use a smaller limit");
        return {{"snapshot", manifest.at("id")},
                {"manifest_hash", hash},
                {"files", page},
                {"prefix", prefix},
                {"total", count},
                {"bytes", bytes},
                {"offset", offset},
                {"limit", limit},
                {"has_more", offset < count && page.size() < count - offset},
                {"content_verified", false}};
    }
    auto left = snapshot_manifest(state, project, data.at("left"));
    auto right = snapshot_manifest(state, project, data.at("right"));
    auto left_hash = sha256(left.dump()), right_hash = sha256(right.dump());
    pinned(data, "left_hash", left_hash);
    pinned(data, "right_hash", right_hash);
    Json result{{"left", left.at("id")},
                {"right", right.at("id")},
                {"left_hash", left_hash},
                {"right_hash", right_hash}};
    if (action == "snapshot-compare-file") {
        auto path = data.at("path").get<std::string>();
        if (!left.at("files").contains(path) && !right.at("files").contains(path))
            throw std::runtime_error("File is absent from both selected snapshots");
        auto compared = compare_backup_entries(state, left.at("files").value(path, Json()),
                                               right.at("files").value(path, Json()), path);
        compared.update(result);
        return compared;
    }
    if (action != "snapshot-compare")
        throw std::runtime_error("Unknown snapshot query");
    std::set<std::string> names;
    for (const auto *manifest : {&left, &right})
        for (const auto &[path, _] : manifest->at("files").items())
            if (path.starts_with(prefix))
                names.insert(path);
    Json page = Json::array();
    size_t count = 0;
    for (const auto &path : names) {
        auto a = left.at("files").value(path, Json()), b = right.at("files").value(path, Json());
        bool content = a.is_null() || b.is_null() || a.at("sha256") != b.at("sha256") ||
                       a.at("size") != b.at("size");
        bool permissions =
            !a.is_null() && !b.is_null() && a.at("permissions") != b.at("permissions");
        if (!content && !permissions)
            continue;
        if (count++ >= offset && page.size() < limit)
            page.push_back({{"path", path},
                            {"status", a.is_null()   ? "added"
                                       : b.is_null() ? "deleted"
                                       : content     ? "modified"
                                                     : "permissions"},
                            {"content_changed", content},
                            {"permissions_changed", permissions},
                            {"before", metadata(path, a)},
                            {"after", metadata(path, b)}});
    }
    if (page.dump().size() > 2 * 1024 * 1024)
        throw std::runtime_error("Snapshot comparison page exceeds 2 MiB");
    result.update({{"files", page},
                   {"total", count},
                   {"offset", offset},
                   {"limit", limit},
                   {"prefix", prefix},
                   {"has_more", offset < count && page.size() < count - offset},
                   {"content_verified", false}});
    return result;
}
} // namespace cysynck
