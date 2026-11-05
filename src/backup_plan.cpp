#include "backup_plan.hpp"
#include "backup.hpp"
#include "backup_data.hpp"
#include "file_time.hpp"
#include <algorithm>
#include <limits>

namespace cysynck {
namespace {
bool glob(const std::string &text, const std::string &pattern) {
    size_t i = 0, j = 0, star = std::string::npos, mark = 0;
    while (i < text.size()) {
        if (j < pattern.size() && (pattern[j] == '?' || pattern[j] == text[i])) {
            ++i;
            ++j;
        } else if (j < pattern.size() && pattern[j] == '*') {
            star = j++;
            mark = i;
        } else if (star != std::string::npos) {
            j = star + 1;
            i = ++mark;
        } else
            return false;
    }
    while (j < pattern.size() && pattern[j] == '*')
        ++j;
    return j == pattern.size();
}
std::string folded(std::string value) {
#ifdef _WIN32
    for (auto &ch : value)
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
#endif
    return value;
}
size_t page_bound(const Json &data, const char *key, size_t fallback, size_t maximum) {
    if (!data.contains(key))
        return fallback;
    const auto &value = data.at(key);
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0) ||
        value.get<uint64_t>() > maximum || (std::string(key) == "limit" && value == 0))
        throw std::runtime_error("Invalid backup preview page bound");
    return value.get<size_t>();
}
} // namespace
void validate_backup_excludes(const Json &rules) {
    if (!rules.is_array() || rules.size() > 256 || rules.dump().size() > 16384)
        throw std::runtime_error("Backup exclusions require at most 256 rules and 16 KiB of JSON");
    for (const auto &rule : rules) {
        if (!rule.is_string())
            throw std::runtime_error("Backup exclusion rules must be strings");
        auto value = rule.get<std::string>();
        if (value.empty() || value.size() > 1024 || value.find('\0') != value.npos ||
            value.find_first_of("\r\n") != value.npos)
            throw std::runtime_error(
                "Backup rules require 1 to 1024 UTF-8 bytes without NUL or newlines");
    }
}
Json backup_exclusion(const Json &project, const std::string &relative) {
    for (const auto &part : fs::u8path(relative))
        if (folded(path_string(part)) == ".cysynck-recovery")
            return {
                {"reason", "protected"}, {"rule", ".cysynck-recovery"}, {"rule_index", nullptr}};
    const auto &rules = project.at("excludes");
    for (size_t index = 0; index < rules.size(); ++index) {
        auto pattern = rules.at(index).get<std::string>();
        bool matches = glob(relative, pattern);
        for (const auto &part : fs::u8path(relative)) {
            auto component = path_string(part);
            if (pattern == ".git" || pattern == ".stfolder" || pattern == ".stversions" ||
                pattern == ".stignore")
                component = folded(component);
            matches = matches || glob(component, pattern);
        }
        if (matches)
            return {{"reason", "rule"}, {"rule", pattern}, {"rule_index", index}};
    }
    return nullptr;
}
Json preview_backup(const fs::path &state, const Json &project, const Json &data) {
    auto candidate = project;
    if (data.contains("excludes"))
        candidate["excludes"] = data.at("excludes");
    validate_backup_excludes(candidate.at("excludes"));
    auto offset = page_bound(data, "offset", 0, 100000),
         limit = page_bound(data, "limit", 100, 200);
    auto root = fs::u8path(project.at("root").get<std::string>());
    fs::path parent;
    for (const auto &part : fs::absolute(root)) {
        parent /= part;
        plain_directory(parent);
    }
    if (!fs::is_directory(root))
        throw std::runtime_error("Project root is missing or linked");
    Json entries = Json::array();
    size_t visited = 0, files = 0, skipped = 0, metadata_bytes = 0;
    uintmax_t bytes = 0;
    auto policy = project.value("backup_storage", "auto");
    if (policy != "auto" && policy != "whole" && policy != "cdc")
        throw std::runtime_error("Choose backup_storage auto, whole or cdc");
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator();
         ++it) {
        if (++visited > 100000)
            throw std::runtime_error("Backup preview exceeds 100000 visited entries");
        auto path = it->path();
        auto name = path_string(path.lexically_relative(root));
        Json row{{"path", name}, {"included", false}, {"bytes", nullptr}};
        auto excluded = backup_exclusion(candidate, name);
        if (path_is_link(path)) {
            it.disable_recursion_pending();
            row.update({{"kind", "link"}, {"reason", "link"}});
        } else if (!excluded.is_null()) {
            it.disable_recursion_pending();
            row.update(excluded);
            row["kind"] = it->is_directory() ? "directory" : "file";
        } else if (it->is_directory())
            continue;
        else if (!it->is_regular_file())
            row.update({{"kind", "special"}, {"reason", "special"}});
        else {
            (void)project_child(root, name);
            auto size = fs::file_size(path);
            auto modified = fs::last_write_time(path);
            auto permissions = static_cast<unsigned>(fs::status(path).permissions()) & 07777;
            if (size > std::numeric_limits<uintmax_t>::max() - bytes)
                throw std::runtime_error("Backup preview byte count overflow");
            auto block_limit =
                (policy == "cdc" ? backup_cdc_min : backup_block_size) * backup_max_blocks;
            auto storage =
                policy != "whole" && size >= backup_block_threshold && size <= block_limit
                    ? (policy == "cdc" ? "blocks-cdc-v1" : "blocks-v1")
                    : "whole";
            row.update({{"kind", "file"},
                        {"included", true},
                        {"bytes", size},
                        {"modified_ticks", std::to_string(file_time_ticks(modified))},
                        {"permissions", permissions},
                        {"storage", storage}});
            if (path_is_link(path) || size != fs::file_size(path) ||
                modified != fs::last_write_time(path) ||
                permissions != (static_cast<unsigned>(fs::status(path).permissions()) & 07777))
                throw std::runtime_error(
                    "Project file changed during backup preview; preview again");
            bytes += size;
            ++files;
        }
        if (!row.at("included").get<bool>())
            ++skipped;
        metadata_bytes += row.dump().size();
        if (metadata_bytes > 32 * 1024 * 1024)
            throw std::runtime_error("Backup preview metadata exceeds 32 MiB");
        entries.push_back(std::move(row));
    }
    std::sort(entries.begin(), entries.end(),
              [](const Json &a, const Json &b) { return a.at("path") < b.at("path"); });
    auto inventory_hash = sha256(entries.dump());
    if (data.contains("inventory_hash") && data.at("inventory_hash") != inventory_hash)
        throw std::runtime_error("Backup inventory changed; load the first preview page again");
    auto identity = Json{{"state", path_string(fs::absolute(state))},
                         {"project", project},
                         {"excludes", candidate.at("excludes")},
                         {"inventory_hash", inventory_hash}};
    Json result{{"project", project.at("id")},
                {"excludes", candidate.at("excludes")},
                {"stored_excludes", project.at("excludes")},
                {"files", files},
                {"bytes", bytes},
                {"skipped", skipped},
                {"visited", visited},
                {"content_verified", false},
                {"inventory_hash", inventory_hash},
                {"plan", sha256(identity.dump())},
                {"rules_changed", candidate.at("excludes") != project.at("excludes")},
                {"total", entries.size()},
                {"offset", offset},
                {"limit", limit},
                {"entries", Json::array()}};
    auto end = std::min(entries.size(), offset + limit);
    for (size_t index = offset; index < end; ++index)
        result["entries"].push_back(entries.at(index));
    result["has_more"] = end < entries.size();
    return result;
}
} // namespace cysynck
