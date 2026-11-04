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
          std::map<std::string, std::string, GitPathLess> &paths, bool merging = false) {
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
        std::vector<std::string> args{
            "check-attr", "--source=" + commit,    "-z",    "--stdin", "filter", "text",
            "eol",        "working-tree-encoding", "ident", "crlf"};
        if (merging)
            args.push_back("merge");
        auto attrs = records(git.get(args, names));
        if (attrs.size() != entries.size() * (merging ? 21 : 18))
            throw std::runtime_error("Incomplete Git integration attributes");
        for (size_t i = 0; i < attrs.size(); i += 3)
            if (attrs[i + 2] != "unspecified" &&
                !(attrs[i + 1] == "merge" ? attrs[i + 2] == "set" : attrs[i + 2] == "unset"))
                throw std::runtime_error(
                    "Git integration excludes checkout filters, encodings and "
                    "text conversions or custom merge rules; use manual Git handling");
    }
    return entries;
}
bool object_id(const std::string &value) {
    return (value.size() == 40 || value.size() == 64) &&
           value.find_first_not_of("0123456789abcdef") == value.npos;
}
void merge_environment() {
    for (auto key : {"GIT_AUTHOR_NAME", "GIT_AUTHOR_EMAIL", "GIT_AUTHOR_DATE", "GIT_COMMITTER_NAME",
                     "GIT_COMMITTER_EMAIL", "GIT_COMMITTER_DATE"})
        if (!env(key).empty())
            throw std::runtime_error(
                "Reviewed merges require GIT_AUTHOR_* and GIT_COMMITTER_* overrides to be unset");
}
Json merge_identity(const Git &git) {
    Json identity;
    for (auto key : {"name", "email"}) {
        auto value = git.run({"config", "--get", std::string("user.") + key});
        auto text = trim(value.out);
        if (value.code || text.empty() || text.size() > 1024 ||
            text.find_first_of("<>\r\n\0", 0, 5) != text.npos ||
            (std::string(key) == "email" && text.find_first_of(" \t") != text.npos))
            throw std::runtime_error(
                "Configure a valid Git user.name and user.email before a reviewed merge");
        identity[key] = text;
    }
    (void)identity.dump();
    return identity;
}
std::string common_ancestor(const Git &git, const Json &base, const Json &head) {
    auto found = git.run({"merge-base", "--all", base, head});
    if (found.code != 0 && found.code != 1)
        throw std::runtime_error("Cannot inspect reviewed merge ancestry");
    auto value = trim(found.out);
    if (!object_id(value))
        throw std::runtime_error("Reviewed merge requires exactly one common ancestor");
    return value;
}
void merge_rules(const Git &git) {
    auto driver = git.run({"config", "--get", "merge.default"});
    if ((driver.code != 0 && driver.code != 1) || !trim(driver.out).empty())
        throw std::runtime_error("Custom default merge drivers require manual Git handling");
    auto attributes = project_child(fs::u8path(git.project.at("root").get<std::string>()),
                                    ".git/info/attributes");
    if (fs::exists(attributes) && (!fs::is_regular_file(attributes) || fs::file_size(attributes)))
        throw std::runtime_error("Local .git/info/attributes require manual Git merging");
}
void overlapping_text(const Git &git, const Json &ancestor, const Json &local, const Json &peer) {
    std::map<std::string, Json> base_files, local_files;
    for (const auto &file : ancestor)
        base_files[file.at("path")] = file;
    for (const auto &file : local)
        local_files[file.at("path")] = file;
    uintmax_t total = 0;
    for (const auto &right : peer) {
        auto name = right.at("path").get<std::string>();
        auto left = local_files.find(name), original = base_files.find(name);
        if (left == local_files.end() || left->second.at("oid") == right.at("oid"))
            continue;
        if (original != base_files.end() && (original->second.at("oid") == left->second.at("oid") ||
                                             original->second.at("oid") == right.at("oid")))
            continue;
        for (const auto &file : {original == base_files.end() ? Json(nullptr) : original->second,
                                 left->second, right}) {
            if (file.is_null())
                continue;
            auto size = file.at("bytes").get<uintmax_t>();
            if (size > 512 * 1024 || total + size > 16 * 1024 * 1024)
                throw std::runtime_error(
                    "Overlapping Git merge text exceeds 512 KiB per side or 16 MiB total: " + name);
            auto content = git.get({"cat-file", "blob", file.at("oid")});
            if (content.size() != size || content.find('\0') != content.npos)
                throw std::runtime_error("Overlapping binary Git changes require manual merging: " +
                                         name);
            try {
                (void)Json(content).dump();
            } catch (const std::exception &) {
                throw std::runtime_error(
                    "Overlapping non-UTF-8 Git changes require manual merging: " + name);
            }
            total += size;
        }
    }
}
void verify_merge_commit(const Git &git, const std::string &commit, const Json &review) {
    auto bytes = git.get({"cat-file", "commit", commit});
    auto split = bytes.find("\n\n");
    if (split == bytes.npos ||
        bytes.substr(split + 2) != review.at("merge_message").get<std::string>() + "\n")
        throw std::runtime_error("Receipt merge commit message differs from its reviewed message");
    std::istringstream headers(bytes.substr(0, split));
    std::string line, tree_id;
    std::vector<std::string> parents;
    bool author = false, committer = false;
    auto identity = review.at("merge_identity").at("name").get<std::string>() + " <" +
                    review.at("merge_identity").at("email").get<std::string>() + "> ";
    while (std::getline(headers, line)) {
        if (line.starts_with("tree ") && tree_id.empty())
            tree_id = line.substr(5);
        else if (line.starts_with("parent "))
            parents.push_back(line.substr(7));
        else if (line.starts_with("author " + identity) && !author)
            author = true;
        else if (line.starts_with("committer " + identity) && !committer)
            committer = true;
        else
            throw std::runtime_error(
                "Receipt merge commit identity or headers differ from the reviewed commit");
    }
    if (!author || !committer || Json(tree_id) != review.at("result_tree") ||
        parents != std::vector<std::string>{review.at("base"), review.at("head")})
        throw std::runtime_error("Receipt merge commit has unexpected parents or tree");
}
struct MergeWorkspace {
    fs::path state;
    std::string relative;
    Json project;
    std::unique_ptr<Git> reader;
    explicit MergeWorkspace(const fs::path &profile, const Git &source) : state(profile) {
        auto parent = project_child(state, "git-merge-work");
        plain_directory(parent);
        fs::create_directories(parent);
        secure_state_path(parent, true);
        relative = "git-merge-work/" + random_id();
        auto path = project_child(state, relative);
        if (!fs::create_directory(path))
            throw std::runtime_error("Cannot reserve private Git merge workspace");
        secure_state_path(path, true);
        project = source.project;
        project["root"] = path_string(path);
        try {
            auto format = trim(source.get({"rev-parse", "--show-object-format"}));
            if (format != "sha1" && format != "sha256")
                throw std::runtime_error("Unsupported Git merge object format");
            offline_git(project, {"init", "--bare", "--template=", "--object-format=" + format});
            auto objects = path_string(project_child(
                fs::u8path(source.project.at("root").get<std::string>()), ".git/objects"));
            if (objects.find_first_of("\r\n\0", 0, 3) != objects.npos)
                throw std::runtime_error(
                    "Git merge object storage path cannot contain line breaks");
            atomic_write(path / "objects/info/alternates", objects + "\n", true);
            reader = std::make_unique<Git>(project);
            auto &settings = reader->settings;
            for (auto option :
                 {"merge.renames=false", "merge.directoryRenames=false", "merge.renormalize=false",
                  "merge.default=text", "diff.algorithm=myers", "rerere.enabled=false",
                  "rerere.autoupdate=false"}) {
                settings.push_back("-c");
                settings.push_back(option);
            }
            // Never execute configured external drivers, including conditional global config.
            auto drivers = offline_git_result(project, {"config", "--null", "--name-only",
                                                        "--get-regexp", "^merge\\..*\\.driver$"});
            if (drivers.code != 0 && drivers.code != 1)
                throw std::runtime_error("Cannot inspect private merge drivers");
            for (const auto &key : records(drivers.out)) {
                settings.push_back("-c");
                settings.push_back(key + "=");
            }
        } catch (...) {
            cleanup();
            throw;
        }
    }
    MergeWorkspace(const MergeWorkspace &) = delete;
    MergeWorkspace &operator=(const MergeWorkspace &) = delete;
    ~MergeWorkspace() { cleanup(); }
    void cleanup() noexcept {
        try {
            auto root = project_child(state, relative);
            if (!fs::exists(root))
                return;
            std::vector<std::string> paths;
            for (const auto &entry : fs::recursive_directory_iterator(root)) {
                auto name = relative + "/" + path_string(entry.path().lexically_relative(root));
                auto checked = project_child(state, name);
                if (paths.size() >= 50000 ||
                    (!fs::is_directory(checked) && !fs::is_regular_file(checked)))
                    throw std::runtime_error("Unexpected private Git workspace entry");
                paths.push_back(name);
            }
            std::sort(paths.rbegin(), paths.rend());
            for (const auto &name : paths) {
                auto path = project_child(state, name);
                if (fs::is_regular_file(path))
                    fs::permissions(path, fs::perms::owner_write, fs::perm_options::add);
                fs::remove(path);
            }
            fs::remove(root);
        } catch (...) { /* Preserve unexpected entries; cleanup never follows links or recurses for
                           deletion. */
        }
    }
    Json calculate(const Json &base, const Json &head, const std::string &ancestor,
                   std::map<std::string, std::string, GitPathLess> &paths) {
        auto merged = reader->run({"merge-tree", "--write-tree", "--name-only", "--no-messages",
                                   "-z", "--merge-base=" + ancestor, base, head});
        if (merged.code != 0 && merged.code != 1)
            throw std::runtime_error(
                "Git repository merge preview failed; check Git version and local storage");
        auto fields = records(merged.out);
        if (fields.empty() || !object_id(fields.front()))
            throw std::runtime_error("Invalid Git merge tree identity");
        Json conflicts = Json::array();
        for (size_t i = 1; i < fields.size(); ++i) {
            if (fields[i].empty())
                continue;
            if (conflicts.size() >= 10000 || fields[i].size() > 4096)
                throw std::runtime_error("Git merge conflict listing exceeds its bound");
            (void)Json(fields[i]).dump();
            conflicts.push_back(fields[i]);
        }
        if (merged.code == 0 && !conflicts.empty())
            throw std::runtime_error("Inconsistent clean Git merge output");
        return {{"result_tree", fields.front()},
                {"merge_clean", merged.code == 0},
                {"conflicts", conflicts},
                {"files_after",
                 merged.code == 0 ? tree(*reader, fields.front(), paths, true) : Json::array()}};
    }
    std::string import_commit(const Git &target, const Json &review) {
        auto identity = review.at("merge_identity");
        auto commit = trim(reader->get(
            {"-c", "user.name=" + identity.at("name").get<std::string>(), "-c",
             "user.email=" + identity.at("email").get<std::string>(), "-c", "commit.gpgsign=false",
             "-c", "i18n.commitEncoding=UTF-8", "commit-tree", "--no-gpg-sign",
             review.at("result_tree"), "-p", review.at("base"), "-p", review.at("head")},
            review.at("merge_message").get<std::string>() + "\n"));
        if (!object_id(commit))
            throw std::runtime_error("Invalid reviewed Git merge commit");
        reader->get({"update-ref", "refs/heads/result", commit});
        auto bundle = project_child(state, relative + "/result.bundle");
        reader->get({"bundle", "create", path_string(bundle), "refs/heads/result",
                     "^" + review.at("base").get<std::string>(),
                     "^" + review.at("head").get<std::string>()});
        if (fs::file_size(bundle) > 64 * 1024 * 1024)
            throw std::runtime_error("Private Git merge bundle exceeds 64 MiB");
        target.get({"bundle", "verify", path_string(bundle)});
        auto imported = trim(target.get({"bundle", "unbundle", path_string(bundle)}));
        if (imported != commit + " refs/heads/result")
            throw std::runtime_error("Reviewed Git merge object import mismatch");
        verify_merge_commit(target, commit, review);
        return commit;
    }
};
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
Json inspect(const fs::path &state, const Json &project, const Json &data, bool merging = false) {
    if (data.contains("commit"))
        throw std::runtime_error(
            "Git integration applies an imported branch head; selected commits use extraction");
    Git git(project);
    if (merging)
        merge_environment();
    auto root = fs::absolute(fs::u8path(project.at("root").get<std::string>()));
    metadata(git, root);
    auto result = git_revision_context(project, data);
    result.update({{"project", project.at("id")},
                   {"root", path_string(root)},
                   {"applied", false},
                   {"can_integrate", false},
                   {"integration_kind", merging ? "merge" : "fast_forward"},
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
    if (merging) {
        result.update({{"result_commit", nullptr},
                       {"result_tree", nullptr},
                       {"merge_base", nullptr},
                       {"merge_clean", false},
                       {"conflicts", Json::array()},
                       {"merge_identity", nullptr}});
        result["relation"] = "not_diverged";
        if (base.empty())
            block("Use fast-forward integration for a branch without a local commit");
        else {
            auto forward = git.run({"merge-base", "--is-ancestor", base, result.at("head")});
            auto backward = git.run({"merge-base", "--is-ancestor", result.at("head"), base});
            if (forward.code > 1 || backward.code > 1)
                throw std::runtime_error("Cannot inspect reviewed merge ancestry");
            if (forward.code == 0 || backward.code == 0)
                block("Reviewed merge requires divergent branches; use fast-forward integration or "
                      "keep the local branch");
            else {
                result["relation"] = "diverged";
                try {
                    result["merge_base"] =
                        common_ancestor(git, result.at("base"), result.at("head"));
                } catch (const std::exception &error) {
                    block(error.what());
                }
            }
        }
    } else if (base.empty())
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
        if (merging)
            merge_rules(git);
        before = tree(git, base, paths, merging);
        auto incoming = tree(git, result.at("head"), paths, merging);
        if (merging) {
            result["merge_identity"] = merge_identity(git);
            result["merge_message"] = "Merge imported branch " +
                                      result.at("tracking_ref").get<std::string>() +
                                      "\n\nCySynck reviewed merge.";
            if (!result.at("merge_base").is_null()) {
                auto ancestor = tree(git, result.at("merge_base"), paths, true);
                overlapping_text(git, ancestor, before, incoming);
                MergeWorkspace workspace(state, git);
                result.update(workspace.calculate(result.at("base"), result.at("head"),
                                                  result.at("merge_base"), paths));
                after = result.at("files_after");
                if (!result.at("merge_clean").get<bool>())
                    block("The repository merge has conflicts; resolve them with manual Git "
                          "handling");
            }
        } else
            after = incoming;
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
    auto kind = record.value("integration_kind", std::string("fast_forward"));
    if (kind != "fast_forward" && kind != "merge")
        throw std::runtime_error("Unknown Git integration receipt kind");
    if (kind == "merge") {
        for (auto key : {"merge_base", "result_tree"})
            if (!record.at(key).is_string() || !object_id(record.at(key)))
                throw std::runtime_error("Invalid reviewed merge receipt tree or ancestor");
        if (record.at("base").is_null() ||
            (!record.at("result_commit").is_null() &&
             (!record.at("result_commit").is_string() || !object_id(record.at("result_commit")))) ||
            (record.at("status") == "complete" && record.at("result_commit").is_null()))
            throw std::runtime_error("Invalid reviewed merge receipt commit");
        for (auto key : {"name", "email"})
            if (!record.at("merge_identity").at(key).is_string() ||
                record.at("merge_identity").at(key).get_ref<const std::string &>().empty() ||
                record.at("merge_identity").at(key).get_ref<const std::string &>().size() > 1024)
                throw std::runtime_error("Invalid reviewed merge receipt identity");
        if (!record.at("merge_message").is_string() ||
            record.at("merge_message").get_ref<const std::string &>().size() > 4096)
            throw std::runtime_error("Invalid reviewed merge receipt message");
    }
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
    for (auto key :
         {"id", "created", "status", "applied", "peer", "branch", "base", "head", "local_ref",
          "receipt_path", "acknowledged_at", "local_data", "error", "integration_kind",
          "merge_base", "result_commit", "result_tree", "merge_identity"})
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
        bool merging = record.value("integration_kind", std::string("fast_forward")) == "merge";
        Json after_commit = record.at("head");
        if (merging) {
            after_commit = record.at("result_commit");
            if (after_commit.is_null() && !current.at("base").is_null() &&
                current.at("base") != record.at("base")) {
                auto parents =
                    trim(git.get({"rev-list", "--parents", "-n", "1", current.at("base"), "--"}));
                auto expected = current.at("base").get<std::string>() + " " +
                                record.at("base").get<std::string>() + " " +
                                record.at("head").get<std::string>();
                if (parents == expected &&
                    trim(git.get(
                        {"rev-parse", current.at("base").get<std::string>() + "^{tree}"})) ==
                        record.at("result_tree").get<std::string>())
                    after_commit = current.at("base");
            }
            result["resolved_result_commit"] = after_commit;
        }
        if (!after_commit.is_null() && current.at("base") == after_commit)
            result["position"] = "after";
        else if (current.at("base") == record.at("base"))
            result["position"] = "before";
        else
            result["position"] = "other";
        if (current.at("local_ref") != record.at("local_ref") || result.at("position") == "other")
            throw std::runtime_error(
                "The current Git branch or commit differs from both receipt states");
        std::map<std::string, std::string, GitPathLess> paths;
        if (merging)
            merge_rules(git);
        auto before =
            tree(git, record.at("base").is_null() ? "" : record.at("base").get<std::string>(),
                 paths, merging);
        Json after;
        if (merging) {
            auto incoming = tree(git, record.at("head"), paths, true);
            auto ancestor = common_ancestor(git, record.at("base"), record.at("head"));
            if (Json(ancestor) != record.at("merge_base"))
                throw std::runtime_error("Receipt merge ancestor differs from its commit history");
            auto original = tree(git, ancestor, paths, true);
            overlapping_text(git, original, before, incoming);
            MergeWorkspace workspace(state, git);
            auto calculated =
                workspace.calculate(record.at("base"), record.at("head"), ancestor, paths);
            if (!calculated.at("merge_clean").get<bool>() ||
                calculated.at("result_tree") != record.at("result_tree"))
                throw std::runtime_error(
                    "Receipt result differs from the recomputed repository merge");
            after = calculated.at("files_after");
            if (!after_commit.is_null() &&
                (result.at("position") == "after" || record.at("status") == "complete")) {
                verify_merge_commit(git, after_commit, record);
            }
        } else
            after = tree(git, record.at("head"), paths);
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
Json integrate_git_branch(const fs::path &state, const Json &project, const Json &data,
                          bool merging) {
    auto result = inspect(state, project, data, merging);
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
    auto checked = inspect(state, project, data, merging);
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
        Json destination = result.at("head");
        if (merging) {
            merge_rules(git);
            std::map<std::string, std::string, GitPathLess> paths;
            (void)tree(git, result.at("base"), paths, true);
            (void)tree(git, result.at("head"), paths, true);
            (void)tree(git, result.at("merge_base"), paths, true);
            MergeWorkspace workspace(state, git);
            auto merged = workspace.calculate(result.at("base"), result.at("head"),
                                              result.at("merge_base"), paths);
            if (!merged.at("merge_clean").get<bool>() ||
                merged.at("result_tree") != result.at("result_tree") ||
                merged.at("files_after") != result.at("files_after"))
                throw std::runtime_error("Git repository merge result changed; preview again");
            destination = workspace.import_commit(git, result);
            record["result_commit"] = destination;
            atomic_write(fs::u8path(receipt), record.dump(2), true);
            (void)git_revision_context(project, result);
        }
        auto merged = git.run({"merge", "--ff-only", "--no-squash", "--no-edit", "--no-stat",
                               "--no-autostash", "--no-overwrite-ignore", "--no-verify",
                               "--no-verify-signatures", "--no-gpg-sign", destination});
        if (merged.code)
            throw std::runtime_error("Git fast-forward failed; inspect Git status and the "
                                     "preserved integration receipt");
        auto local = git_local_head(project);
        if (local.at("base") != destination || local.at("local_ref") != result.at("local_ref"))
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
        if (git_local_head(project) != local)
            throw std::runtime_error("Git HEAD changed during post-integration verification");
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
