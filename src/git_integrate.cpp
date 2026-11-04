#include "git_integrate.hpp"
#include "backup.hpp"
#include "git_lfs.hpp"
#include "git_paths.hpp"
#include "git_review.hpp"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace cysynck {
namespace {
constexpr size_t output_limit = 8 * 1024 * 1024;
std::string trim(std::string text) {
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
        text.pop_back();
    return text;
}
std::vector<std::string> records(const std::string &text) {
    if (text.size() >= output_limit)
        throw std::runtime_error("Git integration output exceeds its bound");
    std::vector<std::string> result;
    size_t offset = 0;
    while (offset < text.size()) {
        auto end = text.find('\0', offset);
        if (end == text.npos)
            throw std::runtime_error("Incomplete Git integration listing");
        result.push_back(text.substr(offset, end - offset));
        offset = end + 1;
    }
    return result;
}
std::string null_path() {
#ifdef _WIN32
    return "NUL";
#else
    return "/dev/null";
#endif
}
struct Git {
    const Json &project;
    std::vector<std::string> settings;
    std::string config_hash;
    explicit Git(const Json &p) : project(p) {
        require_default_git_environment();
        for (auto key : {"GIT_CONFIG_PARAMETERS", "GIT_CONFIG_COUNT", "GIT_ATTR_SOURCE",
                         "GIT_CONFIG", "GIT_CONFIG_GLOBAL", "GIT_CONFIG_SYSTEM"}) {
            auto value = std::getenv(key);
            if (value && *value)
                throw std::runtime_error(
                    "Git integration requires GIT_* configuration overrides to be unset");
        }
        settings = {"--no-optional-locks",
                    "-c",
                    "core.fsmonitor=false",
                    "-c",
                    "core.untrackedCache=false",
                    "-c",
                    "core.autocrlf=false",
                    "-c",
                    "core.eol=lf",
                    "-c",
                    "core.protectNTFS=true",
                    "-c",
                    "core.protectHFS=true",
                    "-c",
                    "submodule.recurse=false",
                    "-c",
                    "gc.auto=0",
                    "-c",
                    "maintenance.auto=false",
                    "-c",
                    "merge.autoStash=false",
                    "-c",
                    "merge.verifySignatures=false",
                    "-c",
                    "core.attributesFile=" + null_path()};
        auto config = offline_git(p, {"config", "--null", "--list"});
        (void)records(config);
        config_hash = sha256(config);
        auto drivers = offline_git_result(p, {"config", "--null", "--name-only", "--get-regexp",
                                              "^filter\\..*\\.(clean|smudge|process|required)$"});
        if (drivers.code != 0 && drivers.code != 1)
            throw std::runtime_error("Cannot inspect Git filters");
        for (const auto &key : records(drivers.out)) {
            settings.push_back("-c");
            settings.push_back(key + (key.ends_with(".required") ? "=false" : "="));
        }
    }
    ProcessResult run(const std::vector<std::string> &args, const std::string &input = {}) const {
        auto all = settings;
        all.insert(all.end(), args.begin(), args.end());
        return offline_git_result(project, all, input);
    }
    std::string get(const std::vector<std::string> &args, const std::string &input = {}) const {
        auto result = run(args, input);
        if (result.code || result.out.size() >= output_limit)
            throw std::runtime_error(
                "Git integration inspection failed; check repository completeness and Git version");
        return result.out;
    }
};
void metadata(const Git &git, const fs::path &root) {
    fs::path current;
    for (const auto &part : root) {
        current /= part;
        plain_directory(current);
    }
    auto directory = project_child(root, ".git");
    plain_directory(directory);
    if (!fs::is_directory(directory) ||
        !fs::equivalent(fs::u8path(trim(git.get({"rev-parse", "--show-toplevel"}))), root) ||
        !fs::equivalent(fs::u8path(trim(git.get({"rev-parse", "--absolute-git-dir"}))),
                        directory) ||
        trim(git.get({"rev-parse", "--is-bare-repository"})) != "false")
        throw std::runtime_error(
            "Git integration requires the project root and its own ordinary .git directory");
    for (auto path :
         {"config", "HEAD", "index", "ORIG_HEAD", "logs/HEAD", "packed-refs", "objects", "refs"})
        (void)project_child(directory, path);
    for (auto path : {"commondir", "info/grafts", "objects/info/alternates", "MERGE_HEAD",
                      "CHERRY_PICK_HEAD", "REVERT_HEAD", "rebase-merge", "rebase-apply",
                      "sequencer", "BISECT_LOG", "index.lock", "HEAD.lock"})
        if (fs::exists(project_child(directory, path)))
            throw std::runtime_error("Finish the current Git operation or remove repository "
                                     "indirection before integrating");
    for (auto option : {"core.sparseCheckout", "core.splitIndex"}) {
        auto value = git.run({"config", "--bool", "--get", option});
        if ((value.code != 0 && value.code != 1) || (value.code == 0 && trim(value.out) != "false"))
            throw std::runtime_error(
                "Git integration requires a full working tree and ordinary index");
    }
    if (!trim(git.get({"rev-parse", "--shared-index-path"})).empty())
        throw std::runtime_error(
            "Git integration requires an ordinary index without shared storage");
    if (!fs::equivalent(
            fs::u8path(trim(git.get({"rev-parse", "--path-format=absolute", "--git-common-dir"}))),
            directory))
        throw std::runtime_error("Linked Git working trees require manual integration");
}
Json tree(const Git &git, const std::string &commit,
          std::map<std::string, std::string, GitPathLess> &paths) {
    Json entries = Json::array();
    if (commit.empty())
        return entries;
    std::set<std::string, GitPathLess> files;
    for (const auto &record :
         records(git.get({"ls-tree", "-r", "-z", "--long", "--full-tree", commit, "--"}))) {
        auto tab = record.find('\t');
        if (tab == record.npos || entries.size() >= 10000)
            throw std::runtime_error("Git integration is limited to 10000 regular files");
        auto name = record.substr(tab + 1);
        validate_git_path(name, paths);
        if (!files.insert(name).second)
            throw std::runtime_error("Duplicate Git integration path");
        std::istringstream input(record.substr(0, tab));
        std::string mode, type, oid, bytes, extra;
        if (!(input >> mode >> type >> oid >> bytes) || (input >> extra) ||
            (mode != "100644" && mode != "100755") || type != "blob" ||
            (oid.size() != 40 && oid.size() != 64) ||
            oid.find_first_not_of("0123456789abcdef") != oid.npos)
            throw std::runtime_error("Git integration excludes linked files and submodules");
        uintmax_t length = 0;
        auto parsed = std::from_chars(bytes.data(), bytes.data() + bytes.size(), length);
        if (parsed.ec != std::errc{} || parsed.ptr != bytes.data() + bytes.size() ||
            length > 1024ull * 1024 * 1024)
            throw std::runtime_error("Git integration supports ordinary blobs up to 1 GiB");
        if (length < 1024 && !git_lfs_pointer(git.get({"cat-file", "blob", oid})).is_null())
            throw std::runtime_error("LFS revisions require separate extraction; Git integration "
                                     "does not hydrate assets yet");
        entries.push_back({{"path", name}, {"mode", mode}, {"oid", oid}, {"bytes", length}});
    }
    uintmax_t total = 0;
    std::string names;
    for (const auto &entry : entries) {
        total += entry.at("bytes").get<uintmax_t>();
        if (total > 16ull * 1024 * 1024 * 1024)
            throw std::runtime_error("Git integration tree exceeds 16 GiB");
        auto name = entry.at("path").get<std::string>();
        names += name;
        names += '\0';
        for (auto parent = fs::u8path(name).parent_path(); !parent.empty();
             parent = parent.parent_path())
            if (files.contains(path_string(parent)))
                throw std::runtime_error("Git integration contains a file/directory collision");
    }
    if (!names.empty()) {
        auto attrs = records(git.get({"check-attr", "--source=" + commit, "-z", "--stdin", "filter",
                                      "text", "eol", "working-tree-encoding", "ident", "crlf"},
                                     names));
        if (attrs.size() != entries.size() * 18)
            throw std::runtime_error("Incomplete Git integration attributes");
        for (size_t i = 0; i < attrs.size(); i += 3)
            if (attrs[i + 2] != "unspecified" && attrs[i + 2] != "unset")
                throw std::runtime_error("Git integration excludes checkout filters, encodings and "
                                         "text conversions; use separate extraction");
    }
    return entries;
}
Json local_data(const fs::path &root, const Json &current, const Json &before, const Json &after) {
    std::set<std::string, GitPathLess> tracked, directories, incoming;
    std::map<std::string, std::string, GitPathLess> paths;
    for (const auto &file : current)
        tracked.insert(file.at("path").get<std::string>());
    for (const auto &files : {before, after})
        for (const auto &file : files) {
            auto name = file.at("path").get<std::string>();
            validate_git_path(name, paths);
            for (auto parent = fs::u8path(name).parent_path(); !parent.empty();
                 parent = parent.parent_path())
                directories.insert(path_string(parent));
        }
    for (const auto &file : after)
        incoming.insert(file.at("path").get<std::string>());
    std::map<std::string, Json, GitPathLess> inventory;
    size_t visited = 0, files = 0, folders = 0, metadata_bytes = 2;
    uintmax_t bytes = 0;
    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
        auto relative = it->path().lexically_relative(root);
        if (relative == ".git") {
            it.disable_recursion_pending();
            continue;
        }
        if (++visited > 100000)
            throw std::runtime_error("Git integration scans at most 100000 working-tree entries");
        auto name = path_string(relative);
        validate_git_path(name, paths);
        auto path = project_child(root, name); // Reject reparse points before descending.
        auto status = fs::symlink_status(path);
        bool directory = fs::is_directory(status);
        if (!directory && !fs::is_regular_file(status))
            throw std::runtime_error("Local data contains a linked or special entry: " + name);
        if (!directory && tracked.contains(name))
            continue;
        // Shared tracked parent directories may gain/lose tracked children during checkout.
        if (directory && directories.contains(name))
            continue;
        if (incoming.contains(name))
            throw std::runtime_error("Local data collides with an imported file: " + name);
        for (auto parent = relative.parent_path(); !parent.empty(); parent = parent.parent_path())
            if (incoming.contains(path_string(parent)))
                throw std::runtime_error("Local data lies below an imported file: " + name);
        // A local file must not replace a directory needed by the imported tree.
        if (!directory && directories.contains(name))
            throw std::runtime_error("Local data blocks a Git directory: " + name);
        auto leaf = path_string(relative.filename());
        std::transform(leaf.begin(), leaf.end(), leaf.begin(), [](unsigned char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : static_cast<char>(c);
        });
        if (leaf == ".gitattributes")
            throw std::runtime_error("Untracked attributes require manual handling: " + name);
        Json entry = {{"path", name},
                      {"type", directory ? "directory" : "file"},
                      {"permissions", static_cast<unsigned>(status.permissions())}};
        if (directory) {
            ++folders;
        } else {
            auto size = fs::file_size(path);
            if (size > std::numeric_limits<uintmax_t>::max() - bytes)
                throw std::runtime_error("Local data size exceeds the supported range");
            bytes += size;
            ++files;
            entry["bytes"] = size;
            auto modified = fs::last_write_time(path).time_since_epoch().count();
#ifdef _LIBCPP_VERSION
            // libc++ can use a 128-bit file clock; std::to_string has no such overload.
            bool negative = modified < 0;
            std::string stamp;
            do {
                auto digit = modified % 10;
                if (digit < 0)
                    digit = -digit;
                stamp += static_cast<char>('0' + digit);
                modified /= 10;
            } while (modified != 0);
            if (negative)
                stamp += '-';
            std::reverse(stamp.begin(), stamp.end());
            entry["modified"] = stamp;
#else
            entry["modified"] = std::to_string(modified);
#endif
        }
        metadata_bytes += entry.dump().size() + 1;
        if (metadata_bytes > 16 * 1024 * 1024)
            throw std::runtime_error("Local data metadata exceeds 16 MiB");
        inventory.emplace(name, std::move(entry));
    }
    Json entries = Json::array();
    for (const auto &[name, entry] : inventory)
        entries.push_back(entry);
    return {{"files", files},
            {"directories", folders},
            {"bytes", bytes},
            {"fingerprint", sha256(entries.dump())}};
}
Json inspect(const fs::path &state, const Json &project, const Json &data) {
    if (data.contains("commit"))
        throw std::runtime_error(
            "Git integration applies an imported branch head; selected commits use extraction");
    Git git(project);
    auto root = fs::absolute(fs::u8path(project.at("root").get<std::string>()));
    metadata(git, root);
    auto result = git_revision_context(project, data);
    result.update({{"project", project.at("id")},
                   {"root", path_string(root)},
                   {"applied", false},
                   {"can_integrate", false},
                   {"local_data", nullptr},
                   {"plan", nullptr},
                   {"errors", Json::array()}});
    auto block = [&](const std::string &reason) { result["errors"].push_back(reason); };
    if (result.at("local_ref").is_null() ||
        !result.at("local_ref").get<std::string>().starts_with("refs/heads/"))
        block("Select a local branch before integrating; detached HEAD is preserved");
    else {
        auto ref = result.at("local_ref").get<std::string>();
        (void)project_child(root / ".git", ref);
        (void)project_child(root / ".git", "logs/" + ref);
    }
    auto base = result.at("base").is_null() ? "" : result.at("base").get<std::string>();
    if (base.empty())
        result["relation"] = "unborn";
    else if (result.at("base") == result.at("head")) {
        result["relation"] = "identical";
        block("The imported branch is already applied");
    } else {
        auto ancestor = git.run({"merge-base", "--is-ancestor", base, result.at("head")});
        if (ancestor.code == 0)
            result["relation"] = "fast_forward";
        else if (ancestor.code == 1) {
            result["relation"] = "not_fast_forward";
            block("Integration requires a fast-forward; divergent or older branches need manual "
                  "Git merge");
        } else
            throw std::runtime_error("Cannot inspect Git integration ancestry");
    }
    std::map<std::string, std::string, GitPathLess> paths;
    Json before = Json::array(), after = Json::array();
    try {
        before = tree(git, base, paths);
        after = tree(git, result.at("head"), paths);
        auto index = git.get({"ls-files", "--stage", "-z"});
        if (!base.empty()) {
            std::string expected;
            for (const auto &file : before)
                expected += file.at("mode").get<std::string>() + " " +
                            file.at("oid").get<std::string>() + " 0\t" +
                            file.at("path").get<std::string>() + '\0';
            if (index != expected)
                block("Commit or preserve staged changes before integration");
        } else if (!index.empty())
            block("Preserve staged files before the first integration");
        for (const auto &entry : records(git.get({"ls-files", "-v", "-z"})))
            if (!entry.starts_with("H "))
                block("Clear assume-unchanged and skip-worktree index flags before integration");
        for (const auto &file : before) {
            auto path = project_child(root, file.at("path"));
            if (!fs::is_regular_file(path) ||
                fs::file_size(path) != file.at("bytes").get<uintmax_t>() ||
                file.at("oid") !=
                    trim(git.get({"hash-object", "--no-filters", "--", path_string(path)}))) {
                block("Preserve modified or missing working files before integration");
                break;
            }
#ifndef _WIN32
            auto executable =
                (fs::status(path).permissions() & (fs::perms::owner_exec | fs::perms::group_exec |
                                                   fs::perms::others_exec)) != fs::perms::none;
            if (executable != (file.at("mode") == "100755")) {
                block("Preserve changed executable permissions before integration");
                break;
            }
#endif
        }
        result["local_data"] = local_data(root, before, before, after);
        if (!git.get({"status", "--porcelain=v1", "-z", "--untracked-files=no",
                      "--ignore-submodules=none"})
                 .empty())
            block("Tracked files and index must be clean before integration");
        result["index_hash"] = sha256(index);
    } catch (const std::exception &error) {
        block(error.what());
    }
    result["files_before"] = before;
    result["files_after"] = after;
    result["file_count"] = after.size();
    result["config_hash"] = git.config_hash;
    result["state"] = path_string(fs::absolute(state));
    (void)git_revision_context(project, result);
    if (result.at("errors").empty()) {
        result["can_integrate"] = true;
        result["plan"] = sha256(result.dump());
    }
    return result;
}
bool hex(const std::string &value, size_t length) {
    return value.size() == length && value.find_first_not_of("0123456789abcdef") == value.npos;
}
fs::path receipt_path(const fs::path &state, const Json &project, const std::string &id) {
    if (!hex(id, 32))
        throw std::runtime_error("Invalid Git integration receipt identifier");
    return project_child(state, "git-integrations/" + project.at("id").get<std::string>() + "/" +
                                    id + ".json");
}
Json read_receipt(const fs::path &state, const Json &project, const std::string &id) {
    auto path = receipt_path(state, project, id);
    if (!fs::is_regular_file(path))
        throw std::runtime_error("Git integration receipt is not a regular file");
    auto record = Json::parse(read_file(path, 16 * 1024 * 1024));
    if (record.at("id") != id || record.at("receipt") != id ||
        record.at("project") != project.at("id") || record.at("root") != project.at("root") ||
        record.at("state") != path_string(fs::absolute(state)) ||
        (record.at("status") != "complete" && record.at("status") != "incomplete") ||
        !record.at("applied").is_boolean() || !record.at("created").is_string() ||
        record.at("created").get_ref<const std::string &>().size() > 64 ||
        !record.at("local_ref").is_string() ||
        !record.at("local_ref").get<std::string>().starts_with("refs/heads/") ||
        !record.at("files_before").is_array() || record.at("files_before").size() > 10000 ||
        !record.at("files_after").is_array() || record.at("files_after").size() > 10000)
        throw std::runtime_error("Git integration receipt identity or schema mismatch");
    for (auto key : {"peer", "branch", "local_ref"})
        if (!record.at(key).is_string() || record.at(key).get_ref<const std::string &>().empty() ||
            record.at(key).get_ref<const std::string &>().size() > 1024)
            throw std::runtime_error("Invalid Git integration receipt reference");
    for (auto key : {"base", "head"}) {
        if (std::string(key) == "base" && record.at(key).is_null())
            continue;
        auto oid = record.at(key).get<std::string>();
        if (!hex(oid, 40) && !hex(oid, 64))
            throw std::runtime_error("Invalid Git integration receipt commit");
    }
    if (record.contains("acknowledged_at") && !record.at("acknowledged_at").is_string())
        throw std::runtime_error("Invalid Git integration acknowledgement");
    if (record.contains("local_data") && !record.at("local_data").is_null()) {
        const auto &local = record.at("local_data");
        for (auto key : {"files", "directories", "bytes"})
            if (!local.at(key).is_number_unsigned())
                throw std::runtime_error("Invalid Git integration local inventory");
        if (!hex(local.at("fingerprint").get<std::string>(), 64))
            throw std::runtime_error("Invalid Git integration local fingerprint");
    }
    // Stored absolute paths are informational; always derive the actual destination.
    record["receipt_path"] = path_string(path);
    return record;
}
Json receipt_summary(const Json &record) {
    Json summary = Json::object();
    for (auto key : {"id", "created", "status", "applied", "peer", "branch", "base", "head",
                     "local_ref", "receipt_path", "acknowledged_at", "local_data", "error"})
        if (record.contains(key))
            summary[key] = record.at(key);
    summary["needs_attention"] =
        record.at("status") == "incomplete" && !record.contains("acknowledged_at");
    return summary;
}
} // namespace
Json git_integrations(const fs::path &state, const Json &project, const Json &data) {
    for (auto key : {"offset", "limit"})
        if (data.contains(key) &&
            (!data.at(key).is_number_integer() ||
             (!data.at(key).is_number_unsigned() && data.at(key).get<int64_t>() < 0)))
            throw std::runtime_error("Expected non-negative integer Git receipt pagination");
    auto offset = data.value("offset", size_t{0}), limit = data.value("limit", size_t{20});
    if (offset > 1000 || limit == 0 || limit > 50)
        throw std::runtime_error("Git receipt pagination requires offset <= 1000 and limit 1..50");
    Json records = Json::array(), errors = Json::array();
    auto directory =
        project_child(state, "git-integrations/" + project.at("id").get<std::string>());
    plain_directory(directory);
    size_t count = 0;
    uintmax_t bytes = 0;
    bool truncated = false;
    if (fs::exists(directory))
        for (const auto &entry : fs::directory_iterator(directory)) {
            if (++count > 1000) {
                truncated = true;
                break;
            }
            auto name = path_string(entry.path().filename());
            try {
                if (!name.ends_with(".json"))
                    throw std::runtime_error("Unexpected file in Git integration receipts");
                auto path = receipt_path(state, project, name.substr(0, name.size() - 5));
                if (!fs::is_regular_file(path))
                    throw std::runtime_error("Git integration receipt is not a regular file");
                auto length = fs::file_size(path);
                if (length > 64 * 1024 * 1024 - bytes) {
                    truncated = true;
                    break;
                }
                bytes += length;
                records.push_back(
                    receipt_summary(read_receipt(state, project, name.substr(0, name.size() - 5))));
            } catch (const std::exception &error) {
                errors.push_back({{"path", name}, {"error", error.what()}});
            }
        }
    std::sort(records.begin(), records.end(), [](const Json &left, const Json &right) {
        if (left.at("created") != right.at("created"))
            return left.at("created").get<std::string>() > right.at("created").get<std::string>();
        return left.at("id").get<std::string>() > right.at("id").get<std::string>();
    });
    auto total = records.size();
    Json page = Json::array();
    for (auto i = offset; i < total && i < offset + limit; ++i)
        page.push_back(records.at(i));
    return {{"records", page},       {"errors", errors}, {"offset", offset},
            {"limit", limit},        {"total", total},   {"has_more", offset + page.size() < total},
            {"truncated", truncated}};
}
Json check_git_integration(const fs::path &state, const Json &project, const Json &data) {
    auto record = read_receipt(state, project, data.at("receipt").get<std::string>());
    Json result{{"receipt", record.at("id")},
                {"record", receipt_summary(record)},
                {"position", "unknown"},
                {"current_base", nullptr},
                {"current_ref", nullptr},
                {"tracked_matches", false},
                {"local_data_matches", nullptr},
                {"lock_present", false},
                {"errors", Json::array()},
                {"can_acknowledge", false},
                {"plan", nullptr},
                {"receipt_hash", sha256(record.dump())}};
    try {
        Git git(project);
        auto root = fs::u8path(project.at("root").get<std::string>());
        metadata(git, root);
        auto lock = project_child(root, ".git/cysynck-integration.lock");
        result["lock_present"] = fs::exists(lock);
        if (result.at("lock_present").get<bool>())
            throw std::runtime_error("Inspect the repository and confirm no integration is running "
                                     "before manually removing its leftover integration lock");
        auto current = git_local_head(project);
        result["current_base"] = current.at("base");
        result["current_ref"] = current.at("local_ref");
        result["config_hash"] = git.config_hash;
        if (current.at("base") == record.at("head"))
            result["position"] = "after";
        else if (current.at("base") == record.at("base"))
            result["position"] = "before";
        else
            result["position"] = "other";
        if (current.at("local_ref") != record.at("local_ref") || result.at("position") == "other")
            throw std::runtime_error(
                "The current Git branch or commit differs from both receipt states");
        std::map<std::string, std::string, GitPathLess> paths;
        auto before = tree(
            git, record.at("base").is_null() ? "" : record.at("base").get<std::string>(), paths);
        auto after = tree(git, record.at("head"), paths);
        if (before != record.at("files_before") || after != record.at("files_after"))
            throw std::runtime_error("Receipt file listings do not match their Git commit objects");
        const auto &expected = result.at("position") == "after" ? after : before;
        auto index = git.get({"ls-files", "--stage", "-z"});
        result["index_hash"] = sha256(index);
        std::string expected_index;
        for (const auto &file : expected)
            expected_index += file.at("mode").get<std::string>() + " " +
                              file.at("oid").get<std::string>() + " 0\t" +
                              file.at("path").get<std::string>() + '\0';
        if (index != expected_index)
            throw std::runtime_error(
                "The Git index differs from the receipt commit; preserve staged work");
        for (const auto &flag : records(git.get({"ls-files", "-v", "-z"})))
            if (!flag.starts_with("H "))
                throw std::runtime_error(
                    "Clear assume-unchanged and skip-worktree flags before verification");
        for (const auto &file : expected) {
            auto path = project_child(root, file.at("path"));
            if (!fs::is_regular_file(path) ||
                fs::file_size(path) != file.at("bytes").get<uintmax_t>() ||
                file.at("oid") !=
                    trim(git.get({"hash-object", "--no-filters", "--", path_string(path)})))
                throw std::runtime_error("Tracked working files differ from the receipt commit");
#ifndef _WIN32
            bool executable =
                (fs::status(path).permissions() & (fs::perms::owner_exec | fs::perms::group_exec |
                                                   fs::perms::others_exec)) != fs::perms::none;
            if (executable != (file.at("mode") == "100755"))
                throw std::runtime_error(
                    "Tracked executable permissions differ from the receipt commit");
#endif
        }
        if (!git.get({"status", "--porcelain=v1", "-z", "--untracked-files=no"}).empty())
            throw std::runtime_error("Tracked working files or index changed during verification");
        result["tracked_matches"] = true;
        auto inventory = local_data(root, expected, before, after);
        result["local_data"] = inventory;
        if (record.contains("local_data") && !record.at("local_data").is_null()) {
            result["local_data_matches"] = inventory == record.at("local_data");
            if (!result.at("local_data_matches").get<bool>())
                throw std::runtime_error(
                    "Local data metadata differs from the integration receipt");
        }
        if (git_local_head(project) != current)
            throw std::runtime_error("Git HEAD changed during receipt verification; check again");
        result["can_acknowledge"] = result.at("record").at("needs_attention");
        if (result.at("can_acknowledge").get<bool>())
            result["plan"] = sha256(result.dump());
    } catch (const std::exception &error) {
        result["can_acknowledge"] = false;
        result["errors"].push_back(error.what());
    }
    return result;
}
Json acknowledge_git_integration(const fs::path &state, const Json &project, const Json &data) {
    auto checked = check_git_integration(state, project, data);
    if (!checked.at("can_acknowledge").get<bool>() || !data.contains("plan") ||
        checked.at("plan") != data.at("plan"))
        throw std::runtime_error(
            "Git receipt acknowledgement is blocked or its check changed; check again");
    auto id = data.at("receipt").get<std::string>();
    auto record = read_receipt(state, project, id);
    if (sha256(record.dump()) != checked.at("receipt_hash").get<std::string>())
        throw std::runtime_error("Git integration receipt changed; check again");
    record["acknowledged_at"] = timestamp();
    checked.erase("plan");
    checked.erase("record");
    record["acknowledgement"] = checked;
    atomic_write(receipt_path(state, project, id), record.dump(2), true);
    return receipt_summary(record);
}
Json integrate_git_branch(const fs::path &state, const Json &project, const Json &data) {
    auto result = inspect(state, project, data);
    if (!data.value("apply", false))
        return result;
    if (!result.at("can_integrate").get<bool>() || !data.contains("plan") ||
        result.at("plan") != data.at("plan"))
        throw std::runtime_error("Git integration is blocked or its plan changed; preview again");
    auto lock = project_child(fs::u8path(project.at("root").get<std::string>()),
                              ".git/cysynck-integration.lock");
    if (!fs::create_directory(lock))
        throw std::runtime_error("Another Git integration is active; inspect a leftover "
                                 ".git/cysynck-integration.lock before removing it");
    struct Unlock {
        fs::path path;
        ~Unlock() {
            std::error_code error;
            fs::remove(path, error);
        }
    } unlock{lock};
    auto checked = inspect(state, project, data);
    if (checked.at("plan") != result.at("plan"))
        throw std::runtime_error("Git integration changed while acquiring the lock; preview again");
    auto receipts = project_child(state, "git-integrations/" + project.at("id").get<std::string>());
    plain_directory(receipts);
    fs::create_directories(receipts);
    secure_state_path(receipts, true);
    auto id = random_id(), receipt = path_string(receipts / (id + ".json"));
    Json record = result;
    record.update({{"id", id},
                   {"created", timestamp()},
                   {"status", "incomplete"},
                   {"receipt", id},
                   {"receipt_path", receipt}});
    atomic_write(fs::u8path(receipt), record.dump(2), true);
    try {
        Git git(project);
        if (git.config_hash != result.at("config_hash").get<std::string>())
            throw std::runtime_error("Git configuration changed during integration");
        (void)git_revision_context(project, result);
        auto merged = git.run({"merge", "--ff-only", "--no-squash", "--no-edit", "--no-stat",
                               "--no-autostash", "--no-overwrite-ignore", "--no-verify",
                               "--no-verify-signatures", "--no-gpg-sign", result.at("head")});
        if (merged.code)
            throw std::runtime_error("Git fast-forward failed; inspect Git status and the "
                                     "preserved integration receipt");
        auto local = git_local_head(project);
        if (local.at("base") != result.at("head") ||
            local.at("local_ref") != result.at("local_ref"))
            throw std::runtime_error("Git HEAD changed unexpectedly during integration");
        auto root = fs::u8path(project.at("root").get<std::string>());
        for (const auto &file : result.at("files_after")) {
            auto path = project_child(root, file.at("path"));
            if (!fs::is_regular_file(path) ||
                fs::file_size(path) != file.at("bytes").get<uintmax_t>() ||
                file.at("oid") !=
                    trim(git.get({"hash-object", "--no-filters", "--", path_string(path)})))
                throw std::runtime_error("Integrated file does not match the reviewed Git object");
#ifndef _WIN32
            auto executable =
                (fs::status(path).permissions() & (fs::perms::owner_exec | fs::perms::group_exec |
                                                   fs::perms::others_exec)) != fs::perms::none;
            if (executable != (file.at("mode") == "100755"))
                throw std::runtime_error(
                    "Integrated executable permissions do not match the reviewed tree");
#endif
        }
        if (!git.get({"status", "--porcelain=v1", "-z", "--untracked-files=no"}).empty())
            throw std::runtime_error("Working tree changed during integration; inspect Git status");
        if (local_data(root, result.at("files_after"), result.at("files_before"),
                       result.at("files_after")) != result.at("local_data"))
            throw std::runtime_error(
                "Local data changed during integration; inspect the repository and receipt");
        record.update({{"applied", true},
                       {"status", "complete"},
                       {"plan", nullptr},
                       {"can_integrate", false}});
        atomic_write(fs::u8path(receipt), record.dump(2), true);
    } catch (const std::exception &error) {
        record.update({{"status", "incomplete"}, {"applied", false}, {"can_integrate", false}});
        record["error"] = error.what();
        bool saved = true;
        try {
            atomic_write(fs::u8path(receipt), record.dump(2), true);
        } catch (const std::exception &) {
            saved = false;
        }
        throw std::runtime_error(
            "Git integration incomplete; inspect the repository and receipt " + receipt + ": " +
            error.what() +
            (saved ? ""
                   : "; receipt update also failed; the initial incomplete receipt was preserved"));
    }
    return record;
}
} // namespace cysynck
