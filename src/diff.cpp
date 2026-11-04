#include "diff.hpp"
#include "file_time.hpp"
#include <algorithm>
#include <chrono>
#include <regex>
#include <stdexcept>

namespace cysynck {
namespace {
constexpr uintmax_t preview_limit = 512 * 1024;
struct Side {
    Json info = {{"exists", false}, {"bytes", 0}, {"sha256", nullptr}};
    std::string bytes;
};
Side read_side(const fs::path &path, const std::string &expected = {}) {
    Side side;
    if (path.empty())
        return side;
    if (path_is_link(path))
        throw std::runtime_error("Comparison excludes links and junctions");
    if (!fs::exists(path)) {
        if (!expected.empty())
            throw std::runtime_error("Snapshot object is missing; verify this backup");
        return side;
    }
    if (!fs::is_regular_file(path))
        throw std::runtime_error("Comparison expects regular files");
    auto size = fs::file_size(path);
    auto time = fs::last_write_time(path);
    auto ticks = file_time_ticks(time);
    side.info = {{"exists", true},
                 {"bytes", size},
                 {"sha256", nullptr},
                 {"modified_ns", ticks},
                 {"modified_ns_exact", std::to_string(ticks)}};
    if (size <= preview_limit) {
        side.bytes = read_file(path, preview_limit);
        if (fs::file_size(path) != size || fs::last_write_time(path) != time)
            throw std::runtime_error("File changed during comparison; compare again");
        auto digest = sha256(side.bytes);
        if (!expected.empty() && digest != expected)
            throw std::runtime_error("Snapshot object checksum mismatch; verify this backup");
        side.info["sha256"] = digest;
    }
    return side;
}
std::vector<std::string> lines(const std::string &bytes) {
    std::vector<std::string> result;
    size_t start = 0;
    while (start < bytes.size()) {
        auto end = bytes.find('\n', start);
        if (end == std::string::npos)
            end = bytes.size() - 1;
        result.push_back(bytes.substr(start, end - start + 1));
        start = end + 1;
    }
    return result;
}
std::string quote_path(const std::string &path) {
    // Git's C-style quoted path syntax uses octal bytes, rather than JSON's \u escapes.
    std::string result = "\"";
    for (unsigned char ch : path) {
        if (ch == '\\' || ch == '"') {
            result += '\\';
            result += static_cast<char>(ch);
        } else if (ch < 32 || ch >= 127) {
            result += '\\';
            result += static_cast<char>('0' + (ch >> 6));
            result += static_cast<char>('0' + ((ch >> 3) & 7));
            result += static_cast<char>('0' + (ch & 7));
        } else
            result += static_cast<char>(ch);
    }
    return result + '"';
}
struct Edit {
    char kind;
    std::string line;
};
std::vector<Edit> edits(const std::vector<std::string> &a, const std::vector<std::string> &b,
                        bool &approximate) {
    std::vector<Edit> result;
    size_t prefix = 0, endA = a.size(), endB = b.size();
    while (prefix < endA && prefix < endB && a[prefix] == b[prefix]) {
        result.push_back({' ', a[prefix]});
        ++prefix;
    }
    while (endA > prefix && endB > prefix && a[endA - 1] == b[endB - 1]) {
        --endA;
        --endB;
    }
    size_t height = endA - prefix + 1, width = endB - prefix + 1;
    if (height * width <= 2000000) {
        std::vector<uint16_t> lcs(height * width);
        for (size_t x = height - 1; x-- > 0;)
            for (size_t y = width - 1; y-- > 0;)
                lcs[x * width + y] =
                    a[prefix + x] == b[prefix + y]
                        ? 1 + lcs[(x + 1) * width + y + 1]
                        : std::max(lcs[(x + 1) * width + y], lcs[x * width + y + 1]);
        size_t x = 0, y = 0;
        while (x + 1 < height || y + 1 < width) {
            if (x + 1 < height && y + 1 < width && a[prefix + x] == b[prefix + y]) {
                result.push_back({' ', a[prefix + x++]});
                ++y;
            } else if (y + 1 < width &&
                       (x + 1 == height || lcs[x * width + y + 1] >= lcs[(x + 1) * width + y]))
                result.push_back({'+', b[prefix + y++]});
            else
                result.push_back({'-', a[prefix + x++]});
        }
    } else {
        approximate = true;
        for (size_t i = prefix; i < endA; ++i)
            result.push_back({'-', a[i]});
        for (size_t i = prefix; i < endB; ++i)
            result.push_back({'+', b[i]});
    }
    for (size_t i = endA; i < a.size(); ++i)
        result.push_back({' ', a[i]});
    return result;
}
std::string unified(const std::vector<Edit> &rows, const std::string &name, bool before, bool after,
                    size_t &hunks) {
    auto left = quote_path("a/" + name), right = quote_path("b/" + name);
    std::string patch = "diff --git " + left + " " + right + "\n";
    if (!before)
        patch += "new file mode 100644\n";
    if (!after)
        patch += "deleted file mode 100644\n";
    std::vector<size_t> old(rows.size() + 1), now(rows.size() + 1);
    for (size_t i = 0; i < rows.size(); ++i) {
        old[i + 1] = old[i] + (rows[i].kind != '+');
        now[i + 1] = now[i] + (rows[i].kind != '-');
    }
    std::string body;
    for (size_t i = 0; i < rows.size();) {
        while (i < rows.size() && rows[i].kind == ' ')
            ++i;
        if (i == rows.size())
            break;
        auto start = i > 3 ? i - 3 : 0, end = std::min(i + 4, rows.size());
        for (size_t next = i + 1; next < rows.size(); ++next) {
            if (rows[next].kind == ' ')
                continue;
            if (next > end + 3)
                break;
            end = std::min(next + 4, rows.size());
        }
        auto countA = old[end] - old[start], countB = now[end] - now[start];
        body += "@@ -" + std::to_string(old[start] + (countA != 0)) + "," + std::to_string(countA) +
                " +" + std::to_string(now[start] + (countB != 0)) + "," + std::to_string(countB) +
                " @@\n";
        for (auto row = start; row < end; ++row) {
            body += rows[row].kind;
            body += rows[row].line;
            if (!rows[row].line.ends_with('\n'))
                body += "\n\\ No newline at end of file\n";
        }
        ++hunks;
        i = end;
    }
    if (!body.empty())
        patch += "--- " + (before ? left : "/dev/null") + "\n+++ " + (after ? right : "/dev/null") +
                 "\n" + body;
    return patch;
}
Json conflict_name(const std::string &relative) {
    auto path = fs::u8path(relative);
    auto base = path_string(path.filename());
    std::smatch match;
    static const std::regex pattern(
        "(.*)\\.sync-conflict-([0-9]{8})-([0-9]{6})-([A-Z2-7]{7})(\\.[^/]*)?");
    if (!std::regex_match(base, match, pattern))
        throw std::runtime_error("Not a recognized Syncthing conflict filename");
    auto date = match[2].str(), time = match[3].str();
    auto calendar = std::chrono::year(std::stoi(date.substr(0, 4))) /
                    std::chrono::month(std::stoi(date.substr(4, 2))) /
                    std::chrono::day(std::stoi(date.substr(6, 2)));
    if (!calendar.ok() || std::stoi(time.substr(0, 2)) > 23 || std::stoi(time.substr(2, 2)) > 59 ||
        std::stoi(time.substr(4, 2)) > 59)
        throw std::runtime_error("Invalid conflict timestamp");
    auto original = match[1].str() + match[5].str();
    if (original.empty() || original == "." || original == "..")
        throw std::runtime_error("Invalid conflict original filename");
    return {{"path", relative},
            {"original", path_string(path.parent_path() / fs::u8path(original))},
            {"modified_by", match[4].str()},
            {"timestamp", date + "-" + time}};
}
Json conflict_project(const Json &p) {
    auto result = p;
    Json patterns = Json::array();
    for (const auto &pattern : p.at("excludes"))
        if (pattern != "*.sync-conflict-*")
            patterns.push_back(pattern);
    for (auto pattern : {".git", ".stfolder", ".stversions", ".stignore"})
        patterns.push_back(pattern);
    result["excludes"] = patterns;
    return result;
}
} // namespace
Json conflict_identity(const std::string &relative) { return conflict_name(relative); }
Json inspect_conflict(const Json &p, const std::string &relative) {
    auto filtered = conflict_project(p);
    auto item = conflict_name(relative);
    auto original = item.at("original").get<std::string>();
    if (project_excludes(filtered, relative) || project_excludes(filtered, original))
        throw std::runtime_error("Conflict belongs to an excluded project path");
    auto root = fs::u8path(p.at("root").get<std::string>());
    (void)project_child(root, original);
    auto conflict = project_child(root, relative);
    if (!fs::is_regular_file(conflict))
        throw std::runtime_error("Conflict copy is missing or is not a regular file");
    return item;
}
Json compare_paths(const fs::path &left, const fs::path &right, const std::string &target,
                   const std::string &expected_left_hash) {
    auto a = read_side(left, expected_left_hash), b = read_side(right);
    Json result = {{"path", target},           {"before_info", a.info},
                   {"after_info", b.info},     {"binary", true},
                   {"patch_available", false}, {"identical", nullptr},
                   {"approximate", false},     {"patch", ""}};
    if (a.info.at("bytes") > preview_limit || b.info.at("bytes") > preview_limit) {
        result["message"] = "Text previews are limited to 512 KiB per side";
        return result;
    }
    result["identical"] = a.bytes == b.bytes && a.info.at("exists") == b.info.at("exists");
    try {
        if (a.bytes.find('\0') != std::string::npos || b.bytes.find('\0') != std::string::npos)
            throw std::runtime_error("Binary file");
        (void)Json(a.bytes).dump();
        (void)Json(b.bytes).dump();
    } catch (const std::exception &) {
        result["message"] = "Binary or non-UTF-8 file";
        return result;
    }
    auto old = lines(a.bytes), now = lines(b.bytes);
    if (std::max(old.size(), now.size()) > 10000) {
        result["message"] = "Text previews are limited to 10000 lines";
        return result;
    }
    result["binary"] = false;
    result["patch_available"] = true;
    result["before"] = a.bytes;
    result["after"] = b.bytes;
    bool approximate = false;
    auto rows = edits(old, now, approximate);
    size_t added = 0, removed = 0, hunks = 0;
    for (const auto &row : rows) {
        added += row.kind == '+';
        removed += row.kind == '-';
    }
    if (!result.at("identical").get<bool>())
        result["patch"] = unified(rows, target, a.info.at("exists"), b.info.at("exists"), hunks);
    result["added_lines"] = added;
    result["removed_lines"] = removed;
    result["hunks"] = hunks;
    result["approximate"] = approximate;
    return result;
}
Json Core::compare(const Json &p, const Json &data) {
    auto root = fs::u8path(p.at("root").get<std::string>());
    auto left = data.at("left").get<std::string>(), right = data.at("right").get<std::string>();
    auto a = project_child(root, left), b = project_child(root, right);
    if (!fs::is_regular_file(a) || !fs::is_regular_file(b))
        throw std::runtime_error("Select two existing regular files in this project");
    auto result = compare_paths(a, b, left);
    result["left"] = left;
    result["right"] = right;
    return result;
}
Json Core::conflicts(const Json &p, const Json &data) {
    auto filtered = conflict_project(p);
    auto root = fs::u8path(p.at("root").get<std::string>());
    if (data.contains("path")) {
        auto relative = data.at("path").get<std::string>();
        auto item = inspect_conflict(p, relative);
        auto original = item.at("original").get<std::string>();
        auto current = project_child(root, original), conflict = project_child(root, relative);
        auto result = compare_paths(current, conflict, original);
        result["conflict"] = item;
        result["left"] = original;
        result["right"] = relative;
        return result;
    }
    Json entries = Json::array(), errors = Json::array();
    size_t count = 0;
    for (const auto &[name, path] : project_files(filtered)) {
        if (path_string(path.filename()).find(".sync-conflict-") == std::string::npos)
            continue;
        try {
            auto item = conflict_name(name);
            auto original = item.at("original").get<std::string>();
            if (project_excludes(filtered, original))
                continue;
            ++count;
            if (entries.size() >= 10000)
                continue;
            auto current = project_child(root, original);
            item["current_exists"] = fs::is_regular_file(current);
            item["current_bytes"] = item.at("current_exists").get<bool>()
                                        ? Json(fs::file_size(current))
                                        : Json(nullptr);
            item["conflict_bytes"] = fs::file_size(project_child(root, name));
            entries.push_back(item);
        } catch (const std::exception &e) {
            errors.push_back({{"path", name}, {"error", e.what()}});
        }
    }
    return {{"files", entries},
            {"errors", errors},
            {"count", count},
            {"truncated", count > entries.size()}};
}
} // namespace cysynck
