#include "git_review.hpp"
#include "git_lfs.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace cysynck {
namespace {
constexpr uintmax_t preview_limit = 512 * 1024;
bool oid(const std::string &value) {
    return (value.size() == 40 || value.size() == 64) &&
           value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
bool zero(const std::string &value) { return value.find_first_not_of('0') == std::string::npos; }
std::string trim(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
        value.pop_back();
    return value;
}
uintmax_t number(const std::string &value) {
    uintmax_t result = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || value.empty())
        throw std::runtime_error("Invalid Git review count");
    return result;
}
std::string resolve(const Json &project, const std::string &reference) {
    auto result = trim(offline_git(project, {"rev-parse", "--verify", reference + "^{commit}"}));
    if (!oid(result))
        throw std::runtime_error("Invalid Git review commit");
    return result;
}
Json merge_base(const Json &project, const std::string &base, const std::string &head) {
    auto result = process({"git", "--no-replace-objects", "-c", "protocol.allow=never", "-C",
                           project.at("root"), "merge-base", base, head},
                          {}, {}, 120);
    if (result.code == 1)
        return nullptr;
    auto value = trim(result.out);
    if (result.code || !oid(value))
        throw std::runtime_error("Cannot inspect Git branch ancestry");
    return value;
}
Json side(const std::string &mode, const std::string &hash) {
    if (mode.size() != 6 || mode.find_first_not_of("01234567") != std::string::npos || !oid(hash) ||
        ((mode == "000000") != zero(hash)))
        throw std::runtime_error("Invalid Git review file metadata");
    return {{"mode", mode},
            {"oid", zero(hash) ? Json(nullptr) : Json(hash)},
            {"exists", !zero(hash)},
            {"bytes", 0},
            {"sha256", nullptr}};
}
Json changed_files(const Json &project, const std::string &base, const std::string &head) {
    auto before =
        base.empty() ? trim(offline_git(project, {"hash-object", "-t", "tree", "--stdin"})) : base;
    if (!oid(before))
        throw std::runtime_error("Invalid Git comparison tree");
    std::vector<std::string> args{"diff"};
    args.insert(args.end(), {"--raw", "-z", "--no-abbrev", "--no-renames", "--no-ext-diff",
                             "--no-textconv", "--ignore-submodules=none"});
    args.insert(args.end(), {before, head, "--"});
    auto raw = offline_git(project, args);
    Json files = Json::array();
    std::set<std::string> objects;
    size_t offset = 0;
    while (offset < raw.size()) {
        auto end = raw.find('\0', offset);
        if (end == std::string::npos)
            throw std::runtime_error("Incomplete Git review listing");
        std::istringstream header(raw.substr(offset, end - offset));
        std::string beforeMode, afterMode, before, after, status, extra;
        if (!(header >> beforeMode >> afterMode >> before >> after >> status) ||
            beforeMode.empty() || beforeMode.front() != ':' || (header >> extra) ||
            status.size() != 1 || std::string("AMDT").find(status) == std::string::npos)
            throw std::runtime_error("Unsupported Git change record");
        beforeMode.erase(0, 1);
        offset = end + 1;
        end = raw.find('\0', offset);
        if (end == std::string::npos)
            throw std::runtime_error("Incomplete Git review filename");
        auto path = raw.substr(offset, end - offset);
        offset = end + 1;
        if (path.empty() || path.size() > 4096 || files.size() >= 10000)
            throw std::runtime_error(
                "Git review is limited to 10000 changed files and 4096-byte paths");
        (void)Json(path).dump();
        auto left = side(beforeMode, before), right = side(afterMode, after);
        for (const auto &item : {left, right})
            if (item.at("exists").get<bool>() && item.at("mode") != "160000")
                objects.insert(item.at("oid"));
        files.push_back(
            {{"path", path}, {"status", status}, {"before_info", left}, {"after_info", right}});
    }
    std::map<std::string, uintmax_t> sizes;
    if (!objects.empty()) {
        std::string input;
        for (const auto &object : objects)
            input += object + "\n";
        std::istringstream info(offline_git(project, {"cat-file", "--batch-check"}, input));
        for (const auto &expected : objects) {
            std::string hash, type, size, extra, line;
            if (!std::getline(info, line))
                throw std::runtime_error("Incomplete Git review object sizes");
            std::istringstream header(line);
            if (!(header >> hash >> type >> size) || (header >> extra) || hash != expected ||
                type != "blob")
                throw std::runtime_error("Git review object is missing or is not a blob");
            sizes[hash] = number(size);
        }
        std::string extra;
        if (std::getline(info, extra))
            throw std::runtime_error("Unexpected Git review sizes");
    }
    for (auto &file : files)
        for (auto key : {"before_info", "after_info"})
            if (file.at(key).at("exists").get<bool>() && file.at(key).at("mode") != "160000")
                file[key]["bytes"] = sizes.at(file.at(key).at("oid"));
    return files;
}
bool utf8(const std::string &bytes) {
    try {
        (void)Json(bytes).dump();
        return true;
    } catch (const std::exception &) {
        return false;
    }
}
Json lfs_pointer(const std::string &bytes) {
    static const std::regex pattern("version https://git-lfs\\.github\\.com/spec/v1\\noid "
                                    "sha256:([0-9a-f]{64})\\nsize ([0-9]+)\\n");
    std::smatch match;
    if (bytes.size() >= 1024 || !std::regex_match(bytes, match, pattern))
        return nullptr;
    try {
        return {{"sha256", match[1].str()}, {"bytes", number(match[2].str())}};
    } catch (const std::exception &) {
        return nullptr;
    }
}
Json preview(const Json &project, Json file, const std::string &base, const std::string &head) {
    file["binary"] = true;
    file["before"] = "";
    file["after"] = "";
    file["patch"] = "";
    file["added_lines"] = 0;
    file["removed_lines"] = 0;
    file["hunks"] = 0;
    file["approximate"] = false;
    file["identical"] = false;
    file["message"] = "Git object cannot be previewed as text";
    for (auto key : {"before_info", "after_info"}) {
        if (file.at(key).at("mode") == "160000") {
            file["reason"] = "submodule";
            file["message"] = "Submodule commit changed; inspect its repository separately";
            return file;
        }
        if (file.at(key).at("bytes").get<uintmax_t>() > preview_limit) {
            file["reason"] = "large";
            file["message"] = "Git text previews are limited to 512 KiB per side";
            return file;
        }
    }
    for (auto [key, target] :
         {std::pair{"before_info", "before"}, std::pair{"after_info", "after"}}) {
        if (!file.at(key).at("exists").get<bool>())
            continue;
        auto bytes = offline_git(project, {"cat-file", "blob", file.at(key).at("oid")});
        if (bytes.size() != file.at(key).at("bytes").get<uintmax_t>())
            throw std::runtime_error("Git object changed or its preview was truncated");
        if (bytes.find('\0') != std::string::npos || !utf8(bytes)) {
            file["reason"] = "binary";
            file["message"] = "Binary or non-UTF-8 Git object; text preview unavailable";
            return file;
        }
        file[target] = bytes;
        file[key]["sha256"] = sha256(bytes);
        file[std::string("lfs_") + target] = lfs_pointer(bytes);
    }
    auto before =
        base.empty() ? trim(offline_git(project, {"hash-object", "-t", "tree", "--stdin"})) : base;
    if (!oid(before))
        throw std::runtime_error("Invalid Git comparison tree");
    std::vector<std::string> args{"-c", "core.quotePath=true", "diff"};
    args.insert(args.end(), {"--text", "--no-ext-diff", "--no-textconv", "--no-color",
                             "--no-renames", "--src-prefix=a/", "--dst-prefix=b/", "-U3"});
    args.insert(args.end(),
                {before, head, "--", ":(literal)" + file.at("path").get<std::string>()});
    auto patch = offline_git(project, args);
    if (patch.size() > 4 * 1024 * 1024 || !utf8(patch))
        throw std::runtime_error("Git patch exceeds its preview bound");
    size_t added = 0, removed = 0, hunks = 0;
    std::istringstream lines(patch);
    std::string line;
    bool inHunk = false;
    while (std::getline(lines, line)) {
        if (line.starts_with("@@")) {
            inHunk = true;
            ++hunks;
            continue;
        }
        added += inHunk && line.starts_with('+');
        removed += inHunk && line.starts_with('-');
    }
    file["binary"] = false;
    file["reason"] = "text";
    file["patch"] = patch;
    file["added_lines"] = added;
    file["removed_lines"] = removed;
    file["hunks"] = hunks;
    file["message"] = "";
    file["identical"] = file.at("before") == file.at("after") &&
                        file.at("before_info").at("mode") == file.at("after_info").at("mode");
    if (!file.value("lfs_before", Json(nullptr)).is_null() ||
        !file.value("lfs_after", Json(nullptr)).is_null())
        file["reason"] = "lfs_pointer";
    return file;
}
Json local_head(const Json &project) {
    auto symbolic = offline_git_result(project, {"symbolic-ref", "--quiet", "HEAD"});
    Json reference = nullptr;
    if (symbolic.code == 0) {
        auto ref = trim(symbolic.out);
        if (!ref.starts_with("refs/") || ref.size() > 4096 ||
            ref.find_first_of("\r\n\0", 0, 3) != ref.npos)
            throw std::runtime_error("Invalid local Git HEAD reference");
        reference = ref;
    } else if (symbolic.code != 1)
        throw std::runtime_error("Cannot read local Git HEAD");
    auto resolved =
        offline_git_result(project, {"rev-parse", "--verify", "--quiet", "HEAD^{commit}"});
    auto value = trim(resolved.out);
    if (!resolved.code && oid(value))
        return {{"base", value}, {"local_ref", reference}};
    // A missing branch is a valid unborn HEAD. Existing broken refs are not empty history.
    if (resolved.code == 1 && reference.is_string() &&
        reference.get<std::string>().starts_with("refs/heads/") &&
        offline_git_result(project, {"show-ref", "--verify", "--quiet", reference}).code == 1)
        return {{"base", nullptr}, {"local_ref", reference}};
    throw std::runtime_error("Local Git HEAD is missing, corrupt or is not a commit");
}
Json context(const Json &project, const Json &data) {
    if (project.at("mode") != "git-bundles")
        throw std::runtime_error("Git review requires git-bundles mode");
    auto peer = data.at("peer").get<std::string>(), branch = data.at("branch").get<std::string>();
    if (!std::regex_match(peer, std::regex("[A-Za-z0-9][A-Za-z0-9_-]{0,79}")) || branch.empty() ||
        branch.size() > 1024 || branch.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid Git review branch identity");
    auto reference = "refs/remotes/cysynck/" + peer + "/" + branch;
    git(project, {"check-ref-format", reference});
    auto local = local_head(project);
    auto head = resolve(project, reference);
    if ((data.contains("base") && data.at("base") != local.at("base")) ||
        (data.contains("local_ref") && data.at("local_ref") != local.at("local_ref")) ||
        (data.contains("head") && data.at("head") != head))
        throw std::runtime_error("Git review changed; refresh the branch comparison");
    return {{"base", local.at("base")},
            {"local_ref", local.at("local_ref")},
            {"head", head},
            {"tracking_ref", reference},
            {"peer", peer},
            {"branch", branch}};
}
void unchanged(const Json &project, const Json &review) {
    auto local = local_head(project);
    if (local.at("base") != review.at("base") || local.at("local_ref") != review.at("local_ref") ||
        resolve(project, review.at("tracking_ref")) != review.at("head").get<std::string>())
        throw std::runtime_error("Git refs changed during comparison; refresh the review");
}
uintmax_t bounded(const Json &data, const char *key, uintmax_t fallback, uintmax_t low,
                  uintmax_t high) {
    auto value = data.value(key, Json(fallback));
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0))
        throw std::runtime_error(std::string("Expected an integer Git ") + key);
    auto n = value.get<uintmax_t>();
    if (n < low || n > high)
        throw std::runtime_error(std::string("Git ") + key + " exceeds the supported range");
    return n;
}
Json commit_log(const Json &project, const std::string &head, uintmax_t offset, uintmax_t limit) {
    auto raw = offline_git(
        project,
        {"log", "--no-patch", "--no-decorate", "--no-notes", "--no-show-signature",
         "--no-use-mailmap", "--no-color", "--encoding=UTF-8", "--topo-order", "-z",
         "--format=%H%x00%P%x00%an%x00%ae%x00%aI%x00%cn%x00%ce%x00%cI%x00%s",
         "--skip=" + std::to_string(offset), "--max-count=" + std::to_string(limit), head, "--"});
    if (raw.size() >= 8 * 1024 * 1024)
        throw std::runtime_error("Git history exceeds the process output bound");
    Json records = Json::array();
    size_t position = 0;
    while (position < raw.size()) {
        std::vector<std::string> fields;
        for (int i = 0; i < 9; ++i) {
            auto end = raw.find('\0', position);
            if (end == std::string::npos || end - position > 16384)
                throw std::runtime_error("Invalid or oversized Git history metadata");
            auto value = raw.substr(position, end - position);
            (void)Json(value).dump();
            fields.push_back(std::move(value));
            position = end + 1;
        }
        if (!oid(fields[0]) || records.size() >= limit)
            throw std::runtime_error("Invalid Git history commit identity");
        Json parents = Json::array();
        std::istringstream input(fields[1]);
        std::string parent;
        while (input >> parent) {
            if (!oid(parent) || parents.size() >= 256)
                throw std::runtime_error("Invalid or oversized Git commit parent list");
            parents.push_back(parent);
        }
        records.push_back(
            {{"oid", fields[0]},
             {"parents", parents},
             {"author", {{"name", fields[2]}, {"email", fields[3]}, {"date", fields[4]}}},
             {"committer", {{"name", fields[5]}, {"email", fields[6]}, {"date", fields[7]}}},
             {"subject", fields[8]}});
    }
    return records;
}
} // namespace
Json git_local_head(const Json &project) { return local_head(project); }
Json imported_git_branches(const Json &project) {
    if (project.at("mode") != "git-bundles")
        throw std::runtime_error("Git review requires git-bundles mode");
    auto raw = offline_git(project, {"for-each-ref", "--format=%(refname)%00%(objectname)%00",
                                     "refs/remotes/cysynck/"});
    std::istringstream lines(raw);
    std::string line;
    Json result = Json::array();
    const std::string prefix = "refs/remotes/cysynck/";
    while (std::getline(lines, line)) {
        auto split = line.find('\0'),
             end = line.find('\0', split == std::string::npos ? 0 : split + 1);
        if (split == std::string::npos || end != line.size() - 1 || !line.starts_with(prefix) ||
            result.size() >= 10000)
            throw std::runtime_error("Invalid or oversized imported Git branch list");
        auto ref = line.substr(0, split), hash = line.substr(split + 1, end - split - 1);
        auto slash = ref.find('/', prefix.size());
        if (slash == std::string::npos || !oid(hash))
            throw std::runtime_error("Invalid imported Git ref");
        auto peer = ref.substr(prefix.size(), slash - prefix.size());
        if (!std::regex_match(peer, std::regex("[A-Za-z0-9][A-Za-z0-9_-]{0,79}")))
            throw std::runtime_error("Invalid imported peer identity");
        result.push_back({{"peer", peer},
                          {"branch", ref.substr(slash + 1)},
                          {"head", hash},
                          {"tracking_ref", ref}});
    }
    return result;
}
Json review_git_branch(const Json &project, const Json &data, bool filePreview) {
    auto review = context(project, data);
    auto base = review.at("base").is_null() ? std::string{} : review.at("base").get<std::string>();
    auto head = review.at("head").get<std::string>();
    auto files = changed_files(project, base, head);
    Json result;
    if (filePreview) {
        auto path = data.at("path").get<std::string>();
        auto found = std::find_if(files.begin(), files.end(),
                                  [&](const Json &item) { return item.at("path") == path; });
        if (found == files.end())
            throw std::runtime_error("Choose a changed file from this Git review");
        result = preview(project, *found, base, head);
    } else {
        auto counts = base.empty()
                          ? "0 " + trim(offline_git(project, {"rev-list", "--count", head, "--"}))
                          : trim(offline_git(project, {"rev-list", "--left-right", "--count",
                                                       base + "..." + head}));
        std::istringstream input(counts);
        std::string left, right, extra;
        if (!(input >> left >> right) || (input >> extra))
            throw std::runtime_error("Invalid Git ancestry counts");
        auto local = number(left), remote = number(right);
        auto common = base.empty() ? Json(nullptr) : merge_base(project, base, head);
        auto relation = base.empty()                ? "unborn"
                        : local == 0 && remote == 0 ? "identical"
                        : local == 0                ? "fast_forward"
                        : remote == 0               ? "local_ahead"
                        : common.is_null()          ? "unrelated"
                                                    : "diverged";
        result = {{"files", files},
                  {"local_only_commits", local},
                  {"peer_only_commits", remote},
                  {"merge_base", common},
                  {"relation", relation}};
    }
    unchanged(project, review);
    result.update(review);
    return result;
}
Json git_branch_history(const Json &project, const Json &data) {
    auto review = context(project, data);
    if (trim(offline_git(project, {"rev-parse", "--is-shallow-repository"})) != "false")
        throw std::runtime_error("Git history review requires a complete, non-shallow repository");
    auto offset = bounded(data, "offset", 0, 0, 100000);
    auto limit = bounded(data, "limit", 50, 1, 100);
    if (offset && (!data.contains("base") || !data.contains("head")))
        throw std::runtime_error("Paginated Git history requires the original base and head");
    auto commits = commit_log(project, review.at("head"), offset, limit + 1);
    bool more = commits.size() > limit;
    if (more)
        commits.erase(commits.end() - 1);
    review.update(
        {{"commits", commits},
         {"offset", offset},
         {"limit", limit},
         {"next_offset", more && offset + limit <= 100000 ? Json(offset + limit) : Json(nullptr)},
         {"truncated", more && offset + limit > 100000}});
    unchanged(project, review);
    return review;
}
Json review_git_commit(const Json &project, const Json &data, bool filePreview) {
    auto review = context(project, data);
    if (trim(offline_git(project, {"rev-parse", "--is-shallow-repository"})) != "false")
        throw std::runtime_error("Git history review requires a complete, non-shallow repository");
    auto commit = data.at("commit").get<std::string>();
    if (!oid(commit) || resolve(project, commit) != commit)
        throw std::runtime_error("Choose a complete Git commit object ID");
    auto ancestor =
        process({"git", "--no-replace-objects", "-c", "protocol.allow=never", "-C",
                 project.at("root"), "merge-base", "--is-ancestor", commit, review.at("head")},
                {}, {}, 120);
    if (ancestor.code != 0)
        throw std::runtime_error("Git commit is not reachable from the imported branch");
    auto records = commit_log(project, commit, 0, 1);
    if (records.size() != 1 || records[0].at("oid") != commit)
        throw std::runtime_error("Git commit metadata is incomplete");
    auto metadata = records[0];
    const auto &parents = metadata.at("parents");
    auto parent =
        bounded(data, "parent", parents.empty() ? 0 : 1, parents.empty() ? 0 : 1, parents.size());
    auto base = parents.empty() ? std::string{}
                                : parents.at(static_cast<size_t>(parent - 1)).get<std::string>();
    auto files = changed_files(project, base, commit);
    Json result;
    if (filePreview) {
        auto path = data.at("path").get<std::string>();
        auto found = std::find_if(files.begin(), files.end(),
                                  [&](const Json &f) { return f.at("path") == path; });
        if (found == files.end())
            throw std::runtime_error("Choose a changed file from this Git commit");
        result = preview(project, *found, base, commit);
    } else
        result = {{"files", files}};
    unchanged(project, review);
    result.update(review);
    result["commit"] = metadata;
    result["parent"] = parent;
    result["comparison_base"] = base.empty() ? Json(nullptr) : Json(base);
    result["comparison_head"] = commit;
    return result;
}
} // namespace cysynck
