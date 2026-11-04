#include "git_integrate.hpp"
#include "backup.hpp"
#include "git_lfs.hpp"
#include "git_paths.hpp"
#include "git_review.hpp"
#include <charconv>
#include <cstdlib>
#include <fstream>
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
        // Include ignored payloads: native merge defaults may otherwise overwrite them.
        if (!git.get({"ls-files", "--others", "-z"}).empty())
            block("Move untracked and ignored files outside the project before integration");
        if (!git.get({"status", "--porcelain=v1", "-z", "--untracked-files=all",
                      "--ignore-submodules=none"})
                 .empty())
            block("The working tree must be clean before integration");
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
} // namespace
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
        if (!git.get({"status", "--porcelain=v1", "-z", "--untracked-files=all"}).empty())
            throw std::runtime_error("Working tree changed during integration; inspect Git status");
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
