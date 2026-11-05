#include "git_materialize.hpp"
#include "backup.hpp"
#include "file_time.hpp"
#include "git_blobs.hpp"
#include "git_lfs.hpp"
#include "git_paths.hpp"
#include "git_review.hpp"
#include <charconv>
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <picosha2.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace cysynck {
namespace {
constexpr uintmax_t asset_limit = 4ull * 1024 * 1024 * 1024;
constexpr uintmax_t total_limit = 16ull * 1024 * 1024 * 1024;
const std::string recovery_name = ".cysynck-lfs";
std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}
bool hex(const std::string &s, size_t n) {
    return s.size() == n && s.find_first_not_of("0123456789abcdef") == s.npos;
}
std::vector<std::string> records(const std::string &raw) {
    if (raw.size() >= 8 * 1024 * 1024)
        throw std::runtime_error("LFS working-tree listing exceeds its bound");
    std::vector<std::string> result;
    for (size_t start = 0; start < raw.size();) {
        auto end = raw.find('\0', start);
        if (end == raw.npos)
            throw std::runtime_error("Incomplete LFS working-tree listing");
        result.push_back(raw.substr(start, end - start));
        start = end + 1;
    }
    return result;
}
std::string read_git(const Json &p, const std::vector<std::string> &args,
                     const std::string &input = {}) {
    std::vector<std::string> command{"--no-optional-locks", "-c", "core.fsmonitor=false", "-c",
                                     "core.attributesFile="
#ifdef _WIN32
                                     "NUL"
#else
                                     "/dev/null"
#endif
    };
    command.insert(command.end(), args.begin(), args.end());
    auto result = offline_git(p, command, input);
    if (result.size() >= 8 * 1024 * 1024)
        throw std::runtime_error("LFS Git output exceeds its bound");
    return result;
}
void layout(const Json &p) {
    if (p.at("mode") != "git-bundles")
        throw std::runtime_error("LFS materialization requires git-bundles mode");
    require_default_git_environment();
    for (auto key : {"GIT_CONFIG_PARAMETERS", "GIT_CONFIG_COUNT", "GIT_ATTR_SOURCE", "GIT_CONFIG",
                     "GIT_CONFIG_GLOBAL", "GIT_CONFIG_SYSTEM"})
        if (!env(key).empty())
            throw std::runtime_error(
                "LFS materialization requires GIT_* configuration overrides to be unset");
    auto root = fs::u8path(p.at("root").get<std::string>());
    fs::path current;
    for (const auto &part : fs::absolute(root)) {
        current /= part;
        plain_directory(current);
    }
    auto directory = project_child(root, ".git");
    plain_directory(directory);
    if (!fs::is_directory(directory) ||
        trim(read_git(p, {"rev-parse", "--is-bare-repository"})) != "false" ||
        !fs::equivalent(fs::u8path(trim(read_git(p, {"rev-parse", "--show-toplevel"}))), root) ||
        !fs::equivalent(fs::u8path(trim(read_git(p, {"rev-parse", "--absolute-git-dir"}))),
                        directory) ||
        !fs::equivalent(fs::u8path(trim(read_git(
                            p, {"rev-parse", "--path-format=absolute", "--git-common-dir"}))),
                        directory))
        throw std::runtime_error(
            "LFS materialization requires the project root and its own ordinary .git directory");
    for (auto name : {"commondir", "info/grafts", "objects/info/alternates", "MERGE_HEAD",
                      "CHERRY_PICK_HEAD", "REVERT_HEAD", "rebase-merge", "rebase-apply",
                      "sequencer", "BISECT_LOG", "index.lock", "HEAD.lock"})
        if (fs::exists(project_child(directory, name)))
            throw std::runtime_error(
                "Finish the current Git operation before materializing LFS assets");
    auto attributes = project_child(directory, "info/attributes");
    if (fs::exists(attributes) && (!fs::is_regular_file(attributes) || fs::file_size(attributes)))
        throw std::runtime_error("Local .git/info/attributes require manual LFS handling");
    for (auto key : {"core.sparseCheckout", "core.splitIndex"}) {
        auto value = offline_git_result(p, {"config", "--bool", "--get", key});
        if ((value.code != 0 && value.code != 1) || (value.code == 0 && trim(value.out) != "false"))
            throw std::runtime_error(
                "LFS materialization requires a full working tree and ordinary index");
    }
    auto promisor = offline_git_result(
        p, {"config", "--get-regexp", "^(extensions\\.partialclone|remote\\..*\\.promisor)$"});
    if (promisor.code != 1 ||
        trim(read_git(p, {"rev-parse", "--is-shallow-repository"})) != "false" ||
        !trim(read_git(p, {"rev-parse", "--shared-index-path"})).empty())
        throw std::runtime_error(
            "LFS materialization requires a complete repository without shared index storage");
    size_t count = 0;
    for (const auto &entry : fs::directory_iterator(root)) {
        if (++count > 100000)
            throw std::runtime_error("LFS materialization root listing exceeds its bound");
        auto name = path_string(entry.path().filename());
        if (!GitPathLess{}(name, recovery_name) && !GitPathLess{}(recovery_name, name) &&
            name != recovery_name)
            throw std::runtime_error("LFS recovery namespace has a case alias");
    }
    (void)project_child(root, recovery_name);
}
Json committed_files(const Json &p, const std::string &commit) {
    Json files = Json::array();
    size_t visited = 0;
    std::map<std::string, std::string, GitPathLess> paths;
    for (const auto &record :
         records(read_git(p, {"ls-tree", "-r", "-z", "--long", "--full-tree", commit, "--"}))) {
        auto tab = record.find('\t');
        if (tab == record.npos || ++visited > 10000)
            throw std::runtime_error("LFS materialization inspects at most 10000 tracked files");
        auto name = record.substr(tab + 1);
        validate_git_path(name, paths);
        auto first = path_string(*fs::u8path(name).begin());
        if (!GitPathLess{}(first, recovery_name) && !GitPathLess{}(recovery_name, first))
            throw std::runtime_error("Tracked files occupy the LFS recovery namespace");
        std::string mode, type, oid, bytes, extra;
        std::istringstream line(record.substr(0, tab));
        if (!(line >> mode >> type >> oid >> bytes) || (line >> extra) ||
            (!hex(oid, 40) && !hex(oid, 64)))
            throw std::runtime_error("Invalid committed LFS file metadata");
        if ((mode != "100644" && mode != "100755") || type != "blob")
            continue;
        uintmax_t size = 0;
        auto parsed = std::from_chars(bytes.data(), bytes.data() + bytes.size(), size);
        if (parsed.ec != std::errc{} || parsed.ptr != bytes.data() + bytes.size())
            throw std::runtime_error("Invalid committed LFS blob size");
        files.push_back({{"path", name}, {"mode", mode}, {"oid", oid}, {"bytes", size}});
    }
    auto blobs = git_small_blobs(p, files);
    Json pointers = Json::array();
    uintmax_t total = 0;
    for (auto file : files) {
        auto found = blobs.find(file.at("oid"));
        if (found == blobs.end())
            continue;
        auto pointer = git_lfs_pointer(found->second);
        if (pointer.is_null())
            continue;
        auto size = pointer.at("size").get<uintmax_t>();
        if (size > asset_limit || size > total_limit - total || pointers.size() >= 4096)
            throw std::runtime_error(
                "LFS materialization is limited to 4096 paths, 4 GiB each and 16 GiB total");
        if (file.at("oid") !=
            trim(read_git(p, {"hash-object", "--no-filters", "--stdin"}, found->second)))
            throw std::runtime_error("Committed LFS pointer identity is corrupted");
        total += size;
        file["asset"] = pointer;
        file["pointer_text"] = found->second;
        pointers.push_back(file);
    }
    return pointers;
}
Json selection_paths(const Json &data) {
    if (!data.contains("paths"))
        return nullptr;
    const auto &values = data.at("paths");
    if (!values.is_array() || values.empty() || values.size() > 256)
        throw std::runtime_error("Select between 1 and 256 LFS files or folders");
    std::set<std::string> selected;
    std::map<std::string, std::string, GitPathLess> paths;
    size_t bytes = 0;
    for (const auto &value : values) {
        auto name = value.get<std::string>();
        if (name.size() > 16384 - bytes)
            throw std::runtime_error("LFS path selection exceeds 16 KiB");
        bytes += name.size();
        validate_git_path(
            !name.empty() && name.back() == '/' ? name.substr(0, name.size() - 1) : name, paths);
        selected.insert(name);
    }
    return Json(selected);
}
Json selected_files(const Json &files, const Json &selection) {
    if (selection.is_null())
        return files;
    Json result = Json::array();
    std::set<std::string> matched;
    for (const auto &file : files) {
        auto path = file.at("path").get<std::string>();
        bool included = false;
        for (const auto &value : selection) {
            auto name = value.get<std::string>();
            if (name.back() == '/' ? path.starts_with(name) : path == name) {
                included = true;
                matched.insert(name);
            }
        }
        if (included)
            result.push_back(file);
    }
    if (matched.size() != selection.size())
        throw std::runtime_error(
            "Selected LFS path has no committed pointer; folders need a trailing slash");
    return result;
}
Json fingerprint(const fs::path &path, bool digest = true) {
    if (path_is_link(path))
        throw std::runtime_error("LFS working files cannot be links or junctions");
    if (!fs::exists(path))
        return {{"exists", false},
                {"bytes", 0},
                {"sha256", nullptr},
                {"permissions", 0},
                {"modified", 0}};
    if (!fs::is_regular_file(path))
        throw std::runtime_error("LFS working paths must be regular files");
    auto size = fs::file_size(path);
    auto modified = file_time_ticks(fs::last_write_time(path));
    auto permissions = static_cast<unsigned>(fs::status(path).permissions());
    Json hash = digest ? Json(sha256_file(path)) : Json(nullptr);
    if (path_is_link(path) || size != fs::file_size(path) ||
        modified != file_time_ticks(fs::last_write_time(path)) ||
        permissions != static_cast<unsigned>(fs::status(path).permissions()))
        throw std::runtime_error("LFS working file changed during inspection");
    return {{"exists", true},
            {"bytes", size},
            {"sha256", hash},
            {"permissions", permissions},
            {"modified", modified}};
}
bool matches(const Json &info, const Json &asset) {
    return info.at("exists").get<bool>() && info.at("bytes") == asset.at("size") &&
           info.at("sha256") == asset.at("sha256");
}
std::string index_digest(const Json &p) {
    return sha256_file(project_child(fs::u8path(p.at("root").get<std::string>()), ".git/index"));
}
Json inspect(const Json &p, const Json &data) {
    layout(p);
    auto head = git_local_head(p);
    for (auto key : {"base", "local_ref"})
        if (data.contains(key) && data.at(key) != head.at(key))
            throw std::runtime_error("Git HEAD changed; preview LFS materialization again");
    if (head.at("base").is_null())
        throw std::runtime_error("Check out a local commit before materializing LFS assets");
    auto index = read_git(p, {"ls-files", "--stage", "-z"});
    auto index_file_hash = index_digest(p);
    auto config = sha256(read_git(p, {"config", "--null", "--list"}));
    std::map<std::string, std::string> indexed, flags;
    for (const auto &line : records(index)) {
        auto tab = line.find('\t');
        if (tab == line.npos)
            throw std::runtime_error("Invalid Git index record");
        auto name = line.substr(tab + 1);
        if (!indexed.emplace(name, line.substr(0, tab)).second)
            indexed[name] = "unmerged";
    }
    auto index_flags = read_git(p, {"ls-files", "-v", "-z"});
    for (const auto &line : records(index_flags)) {
        if (line.size() < 3 || line[1] != ' ')
            throw std::runtime_error("Invalid Git index flags");
        flags[line.substr(2)] = line.substr(0, 1);
    }
    auto selection = selection_paths(data);
    auto inventory = committed_files(p, head.at("base"));
    auto files = selected_files(inventory, selection);
    Json errors = Json::array();
    std::map<std::string, std::string> cache;
    fs::path store;
    size_t pending = 0, materialized = 0, skipped = 0;
    uintmax_t bytes = 0;
    for (auto &file : files) {
        auto name = file.at("path").get<std::string>();
        file["state"] = "blocked";
        file["cache"] = "not_checked";
        file["before"] = nullptr;
        try {
            auto expected_index =
                file.at("mode").get<std::string>() + " " + file.at("oid").get<std::string>() + " 0";
            if (indexed[name] != expected_index) {
                file["state"] = "staged";
                ++skipped;
                continue;
            }
            if (flags[name] != "H") {
                file["state"] = "index_flags";
                ++skipped;
                continue;
            }
            auto attrs = records(read_git(
                p,
                {"check-attr", "--source=" + head.at("base").get<std::string>(), "-z", "--stdin",
                 "filter", "text", "eol", "working-tree-encoding", "ident", "crlf"},
                name + '\0'));
            bool allowed = attrs.size() == 18 && attrs.at(2) == "lfs";
            for (size_t i = 3; allowed && i < attrs.size(); i += 3)
                allowed = attrs[i + 2] == "unspecified" || attrs[i + 2] == "unset";
            if (!allowed) {
                file["state"] = "attributes";
                ++skipped;
                continue;
            }
            auto path = project_child(fs::u8path(p.at("root").get<std::string>()), name);
            bool hash_file = fs::is_regular_file(path) &&
                             (Json(fs::file_size(path)) == file.at("bytes") ||
                              Json(fs::file_size(path)) == file.at("asset").at("size"));
            auto info = fingerprint(path, hash_file);
            file["before"] = info;
            if (!info.at("exists").get<bool>()) {
                file["state"] = "missing";
                ++skipped;
                continue;
            }
            if (info.at("bytes") == file.at("bytes") &&
                info.at("sha256") == sha256(file.at("pointer_text"))) {
                if ((info.at("permissions").get<unsigned>() &
                     static_cast<unsigned>(fs::perms::owner_write)) == 0) {
                    file["state"] = "read_only";
                    ++skipped;
                    continue;
                }
                file["state"] = "pointer";
                ++pending;
                bytes += file.at("asset").at("size").get<uintmax_t>();
                if (store.empty())
                    store = lfs_local_store(p);
                auto digest = file.at("asset").at("sha256").get<std::string>();
                if (!cache.contains(digest)) {
                    try {
                        verify_lfs_object(lfs_object_path(store, digest), file.at("asset"));
                        cache[digest] = "verified";
                    } catch (const std::exception &) {
                        cache[digest] = "unavailable";
                    }
                }
                file["cache"] = cache.at(digest);
                if (cache.at(digest) != "verified")
                    errors.push_back({{"path", name},
                                      {"error", "LFS cache object is missing or corrupted; import "
                                                "verified objects first"}});
            } else if (matches(info, file.at("asset"))) {
                file["state"] = "materialized";
                ++materialized;
            } else {
                file["state"] = "modified";
                ++skipped;
            }
        } catch (const std::exception &error) {
            errors.push_back({{"path", name}, {"error", error.what()}});
        }
    }
    if (git_local_head(p) != head || index_digest(p) != index_file_hash ||
        read_git(p, {"ls-files", "--stage", "-z"}) != index ||
        sha256(read_git(p, {"config", "--null", "--list"})) != config)
        throw std::runtime_error("Git context changed during LFS inspection; preview again");
    Json result = head;
    result.update({{"schema", 1},
                   {"project", p.at("id")},
                   {"root", p.at("root")},
                   {"project_hash", sha256(p.dump())},
                   {"selection", selection},
                   {"excluded", inventory.size() - files.size()},
                   {"files", files},
                   {"pending", pending},
                   {"materialized", materialized},
                   {"skipped", skipped},
                   {"bytes", bytes},
                   {"index_hash", sha256(index)},
                   {"index_flags_hash", sha256(index_flags)},
                   {"index_file_hash", index_file_hash},
                   {"config_hash", config},
                   {"lfs_storage", store.empty() ? Json(nullptr) : Json(path_string(store))},
                   {"errors", errors},
                   {"can_materialize", pending > 0 && errors.empty()},
                   {"applied", false},
                   {"plan", nullptr}});
    if (result.dump().size() > 8 * 1024 * 1024)
        throw std::runtime_error("LFS materialization metadata exceeds 8 MiB");
    if (result.at("can_materialize").get<bool>())
        result["plan"] = sha256(result.dump());
    return result;
}
void stream_asset(const fs::path &source, const fs::path &destination, const Json &asset) {
    FILE *raw = nullptr;
#ifdef _WIN32
    _wfopen_s(&raw, destination.c_str(), L"wbx");
#else
    raw = std::fopen(destination.c_str(), "wbx");
#endif
    if (!raw)
        throw std::runtime_error("Cannot reserve LFS staging file exclusively");
    std::unique_ptr<FILE, decltype(&std::fclose)> output(raw, std::fclose);
    secure_state_path(destination, false);
    std::ifstream input(source, std::ios::binary);
    std::vector<char> buffer(64 * 1024);
    picosha2::hash256_one_by_one hash;
    uintmax_t copied = 0;
    while (input) {
        input.read(buffer.data(), buffer.size());
        auto count = static_cast<size_t>(input.gcount());
        if (count > asset.at("size").get<uintmax_t>() - copied ||
            std::fwrite(buffer.data(), 1, count, output.get()) != count)
            throw std::runtime_error("LFS staging copy exceeded its reviewed size or failed");
        hash.process(buffer.data(), buffer.data() + count);
        copied += count;
    }
    hash.finish();
    if (!input.eof() || copied != asset.at("size").get<uintmax_t>() ||
        picosha2::get_hash_hex_string(hash) != asset.at("sha256").get<std::string>())
        throw std::runtime_error("LFS cache bytes changed during staging");
    if (std::fflush(output.get()) ||
#ifdef _WIN32
        _commit(_fileno(output.get()))
#else
        fsync(fileno(output.get()))
#endif
    )
        throw std::runtime_error("Cannot flush staged LFS asset");
}
void permissions(const fs::path &source, const fs::path &target) {
    fs::permissions(target, fs::status(source).permissions());
#ifdef _WIN32
    DWORD bytes = 0;
    GetFileSecurityW(source.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &bytes);
    if (!bytes)
        throw std::runtime_error("Cannot inspect LFS source permissions");
    std::vector<unsigned char> buffer(bytes);
    auto descriptor = reinterpret_cast<PSECURITY_DESCRIPTOR>(buffer.data());
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (!GetFileSecurityW(source.c_str(), DACL_SECURITY_INFORMATION, descriptor, bytes, &bytes) ||
        !GetSecurityDescriptorControl(descriptor, &control, &revision) ||
        !SetFileSecurityW(target.c_str(),
                          DACL_SECURITY_INFORMATION | ((control & SE_DACL_PROTECTED)
                                                           ? PROTECTED_DACL_SECURITY_INFORMATION
                                                           : UNPROTECTED_DACL_SECURITY_INFORMATION),
                          descriptor))
        throw std::runtime_error("Cannot preserve LFS source access permissions");
#endif
}
void publish(const fs::path &source, const fs::path &target) {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("LFS destination appeared or cannot be published exclusively");
#else
    fs::create_hard_link(source, target);
    fs::remove(source);
#endif
}
} // namespace

Json materialize_git_lfs(const fs::path &state, const Json &project, const Json &data) {
    auto result = inspect(project, data);
    if (!data.value("apply", false))
        return result;
    if (!result.at("can_materialize").get<bool>() || !data.contains("plan") ||
        data.at("plan") != result.at("plan"))
        throw std::runtime_error(
            "LFS materialization is blocked or its plan changed; preview again");
    auto root = fs::u8path(project.at("root").get<std::string>());
    auto lock = project_child(root, ".git/cysynck-integration.lock");
    if (!fs::create_directory(lock))
        throw std::runtime_error(
            "Another Git integration or LFS materialization is active; inspect the existing lock");
    struct Unlock {
        fs::path path;
        ~Unlock() {
            std::error_code error;
            fs::remove(path, error);
        }
    } unlock{lock};
    if (inspect(project, data).at("plan") != result.at("plan"))
        throw std::runtime_error("LFS materialization changed while acquiring its lock");
    auto id = random_id();
    auto relative = recovery_name + "/" + id;
    auto recovery = project_child(root, relative);
    auto receipts =
        project_child(state, "git-materializations/" + project.at("id").get<std::string>());
    fs::create_directories(receipts);
    secure_state_path(receipts, true);
    auto receipt = receipts / (id + ".json");
    Json record = result;
    record.update({{"id", id},
                   {"created", timestamp()},
                   {"status", "incomplete"},
                   {"phase", "staging"},
                   {"recovery_directory", relative},
                   {"receipt_path", path_string(receipt)},
                   {"files_written", 0}});
    atomic_write(receipt, record.dump(2), true);
    try {
        auto parent = project_child(root, recovery_name);
        if (fs::create_directory(parent))
            secure_state_path(parent, true);
        plain_directory(parent);
        if (!fs::create_directory(recovery))
            throw std::runtime_error("LFS recovery directory already exists");
        secure_state_path(recovery, true);
        auto store = lfs_local_store(project);
        if (Json(path_string(store)) != result.at("lfs_storage"))
            throw std::runtime_error("LFS storage changed before staging");
        for (const auto &file : record.at("files")) {
            if (file.at("state") != "pointer")
                continue;
            auto staged =
                project_child(root, relative + "/after/" + file.at("path").get<std::string>());
            fs::create_directories(staged.parent_path());
            stream_asset(lfs_object_path(store, file.at("asset").at("sha256")), staged,
                         file.at("asset"));
        }
        if (git_local_head(project).at("base") != record.at("base") ||
            git_local_head(project).at("local_ref") != record.at("local_ref") ||
            index_digest(project) != record.at("index_file_hash").get<std::string>() ||
            sha256(read_git(project, {"ls-files", "--stage", "-z"})) !=
                record.at("index_hash").get<std::string>() ||
            sha256(read_git(project, {"config", "--null", "--list"})) !=
                record.at("config_hash").get<std::string>())
            throw std::runtime_error("Git context changed while staging LFS assets");
        record["phase"] = "publishing";
        atomic_write(receipt, record.dump(2), true);
        size_t written = 0;
        for (const auto &file : record.at("files")) {
            if (file.at("state") != "pointer")
                continue;
            auto name = file.at("path").get<std::string>();
            auto target = project_child(root, name);
            auto before = project_child(root, relative + "/before/" + name);
            auto staged = project_child(root, relative + "/after/" + name);
            fs::create_directories(before.parent_path());
            if (fingerprint(target) != file.at("before") || fs::exists(before))
                throw std::runtime_error(
                    "LFS pointer changed before publication; preserve local work");
            fs::rename(target, before);
            if (fingerprint(before) != file.at("before"))
                throw std::runtime_error(
                    "LFS source changed while moving; recovery file preserved");
            permissions(before, staged);
            publish(staged, project_child(root, name));
            if (!matches(fingerprint(project_child(root, name)), file.at("asset")))
                throw std::runtime_error("Materialized LFS bytes differ from the verified asset");
            if (fingerprint(before) != file.at("before"))
                throw std::runtime_error(
                    "LFS recovery source changed during publication; preserve the recovery file");
            record["files_written"] = ++written;
            if (written % 16 == 0)
                atomic_write(receipt, record.dump(2), true);
        }
        auto head = git_local_head(project);
        if (head.at("base") != record.at("base") ||
            index_digest(project) != record.at("index_file_hash").get<std::string>() ||
            head.at("local_ref") != record.at("local_ref") ||
            sha256(read_git(project, {"ls-files", "--stage", "-z"})) !=
                record.at("index_hash").get<std::string>())
            throw std::runtime_error("Git HEAD or index changed during LFS materialization");
        record.update({{"status", "complete"},
                       {"phase", "complete"},
                       {"applied", true},
                       {"can_materialize", false},
                       {"plan", nullptr}});
        atomic_write(receipt, record.dump(2), true);
    } catch (const std::exception &error) {
        record.update({{"status", "incomplete"},
                       {"applied", false},
                       {"can_materialize", false},
                       {"plan", nullptr}});
        record["error"] = error.what();
        try {
            atomic_write(receipt, record.dump(2), true);
        } catch (const std::exception &) {
        }
        throw std::runtime_error("LFS materialization incomplete; inspect the preserved receipt "
                                 "and recovery directory: " +
                                 path_string(receipt) + ": " + error.what());
    }
    return record;
}
namespace {
fs::path materialization_receipt(const fs::path &state, const Json &p, const std::string &id) {
    if (!hex(id, 32))
        throw std::runtime_error("Invalid LFS materialization receipt identifier");
    return project_child(state, "git-materializations/" + p.at("id").get<std::string>() + "/" + id +
                                    ".json");
}
Json read_materialization(const fs::path &state, const Json &p, const std::string &id) {
    auto path = materialization_receipt(state, p, id);
    if (!fs::is_regular_file(path))
        throw std::runtime_error("LFS materialization receipt is missing");
    auto record = Json::parse(read_file(path, 16 * 1024 * 1024));
    if (record.at("schema") != 1 || record.at("id") != id || record.at("project") != p.at("id") ||
        record.at("root") != p.at("root") ||
        record.at("recovery_directory") != recovery_name + "/" + id ||
        !record.at("created").is_string() || !record.at("base").is_string() ||
        (!hex(record.at("base"), 40) && !hex(record.at("base"), 64)) ||
        !hex(record.at("index_hash"), 64) || !hex(record.at("index_file_hash"), 64) ||
        !hex(record.at("index_flags_hash"), 64) || !record.at("files").is_array() ||
        record.at("files").size() > 4096 ||
        (record.at("status") != "complete" && record.at("status") != "incomplete"))
        throw std::runtime_error("Invalid LFS materialization receipt identity or schema");
    if (!record.at("local_ref").is_null() &&
        (!record.at("local_ref").is_string() ||
         !record.at("local_ref").get<std::string>().starts_with("refs/heads/") ||
         record.at("local_ref").get<std::string>().size() > 1024))
        throw std::runtime_error("Invalid LFS receipt branch identity");
    std::map<std::string, std::string, GitPathLess> names;
    std::set<std::string> unique;
    size_t pending = 0;
    uintmax_t bytes = 0;
    for (const auto &file : record.at("files")) {
        auto name = file.at("path").get<std::string>();
        validate_git_path(name, names);
        if (!unique.insert(name).second)
            throw std::runtime_error("Duplicate LFS receipt path");
        auto state_name = file.at("state").get<std::string>();
        if (!std::set<std::string>{"pointer", "materialized", "modified", "missing", "staged",
                                   "index_flags", "read_only", "attributes", "blocked"}
                 .contains(state_name))
            throw std::runtime_error("Invalid LFS receipt file state");
        if (state_name != "pointer")
            continue;
        if (file.at("mode") != "100644" && file.at("mode") != "100755")
            throw std::runtime_error("Invalid LFS receipt file mode");
        if ((!hex(file.at("oid"), 40) && !hex(file.at("oid"), 64)) ||
            !file.at("pointer_text").is_string() ||
            file.at("pointer_text").get_ref<const std::string &>().size() >= 1024 ||
            git_lfs_pointer(file.at("pointer_text")) != file.at("asset") ||
            file.at("asset").is_null())
            throw std::runtime_error("Invalid LFS receipt pointer identity");
        const auto &before = file.at("before");
        if (before.at("exists") != true ||
            before.at("bytes") != file.at("pointer_text").get_ref<const std::string &>().size() ||
            before.at("sha256") != sha256(file.at("pointer_text")) ||
            !before.at("permissions").is_number_integer() ||
            before.at("permissions").get<int64_t>() < 0 ||
            before.at("permissions").get<uintmax_t>() > 07777 ||
            !before.at("modified").is_number_integer())
            throw std::runtime_error("Invalid LFS receipt pointer fingerprint");
        (void)before.at("modified").get<int64_t>();
        auto size = file.at("asset").at("size").get<uintmax_t>();
        if (size > total_limit - bytes)
            throw std::runtime_error("LFS receipt assets exceed 16 GiB");
        bytes += size;
        ++pending;
    }
    if (record.contains("selection")) {
        const auto &selection = record.at("selection");
        const auto &excluded = record.at("excluded");
        if (!excluded.is_number_integer() ||
            (!excluded.is_number_unsigned() && excluded.get<int64_t>() < 0) ||
            excluded.get<uintmax_t>() > 4096 ||
            (!selection.is_null() && selection_paths({{"paths", selection}}) != selection) ||
            selected_files(record.at("files"), selection) != record.at("files"))
            throw std::runtime_error("Invalid LFS receipt path selection");
    }
    if (!pending || record.at("pending") != pending || record.at("bytes") != bytes ||
        !record.at("files_written").is_number_integer() ||
        record.at("files_written").get<int64_t>() < 0 ||
        record.at("files_written").get<size_t>() > pending || !record.at("applied").is_boolean() ||
        record.at("applied").get<bool>() != (record.at("status") == "complete") ||
        (record.at("status") == "complete" && record.at("files_written") != pending))
        throw std::runtime_error("Invalid LFS receipt progress");
    if (record.contains("acknowledged_at") &&
        (!record.at("acknowledged_at").is_string() || record.at("status") != "incomplete" ||
         !record.contains("acknowledgement") || !record.at("acknowledgement").is_object()))
        throw std::runtime_error("Invalid LFS receipt acknowledgement");
    return record;
}
Json materialization_summary(const Json &record) {
    bool acknowledged = record.contains("acknowledged_at");
    return {{"id", record.at("id")},
            {"selection", record.value("selection", Json(nullptr))},
            {"excluded", record.value("excluded", Json(0))},
            {"created", record.at("created")},
            {"status", record.at("status")},
            {"files", record.at("pending")},
            {"bytes", record.at("bytes")},
            {"files_written", record.at("files_written")},
            {"recovery_directory", record.at("recovery_directory")},
            {"acknowledged", acknowledged},
            {"needs_attention", record.at("status") == "incomplete" && !acknowledged}};
}
bool same_pointer(const Json &info, const Json &before) {
    return info.at("exists").get<bool>() && info.at("bytes") == before.at("bytes") &&
           info.at("sha256") == before.at("sha256") &&
           info.at("permissions") == before.at("permissions");
}
void check_recovery_entries(const fs::path &root, const Json &record) {
    auto op = project_child(root, record.at("recovery_directory"));
    if (!fs::exists(op))
        return;
    plain_directory(op);
    std::set<std::string> files, dirs;
    for (const auto &file : record.at("files"))
        if (file.at("state") == "pointer")
            for (auto prefix : {"before/", "after/"}) {
                auto name = std::string(prefix) + file.at("path").get<std::string>();
                files.insert(name);
                for (auto parent = fs::u8path(name).parent_path(); !parent.empty();
                     parent = parent.parent_path())
                    dirs.insert(path_string(parent));
            }
    size_t count = 0;
    for (const auto &entry : fs::recursive_directory_iterator(op)) {
        if (++count > 32768)
            throw std::runtime_error("LFS recovery listing exceeds its bound");
        auto name = path_string(entry.path().lexically_relative(op));
        auto path = project_child(op, name);
        if (fs::is_directory(path) ? !dirs.contains(name)
                                   : (!fs::is_regular_file(path) || !files.contains(name)))
            throw std::runtime_error(
                "Unexpected LFS recovery entry is preserved; inspect it manually");
    }
}
} // namespace
Json git_materializations(const fs::path &state, const Json &project, const Json &data) {
    auto number = [&](const char *key, size_t fallback, size_t maximum) {
        if (!data.contains(key))
            return fallback;
        const auto &value = data.at(key);
        if (!value.is_number_integer() ||
            (!value.is_number_unsigned() && value.get<int64_t>() < 0) ||
            value.get<uintmax_t>() > maximum || (std::string(key) == "limit" && value == 0))
            throw std::runtime_error("Invalid LFS receipt page bound");
        return value.get<size_t>();
    };
    auto offset = number("offset", 0, 100000), limit = number("limit", 20, 100);
    auto directory =
        project_child(state, "git-materializations/" + project.at("id").get<std::string>());
    plain_directory(directory);
    Json rows = Json::array(), errors = Json::array(), unknown = Json::array();
    size_t count = 0;
    if (fs::exists(directory))
        for (const auto &entry : fs::directory_iterator(directory)) {
            if (++count > 10000)
                throw std::runtime_error("LFS receipt catalog exceeds 10000 entries");
            auto name = path_string(entry.path().filename()), id = path_string(entry.path().stem());
            if (entry.path().extension() != ".json" || !hex(id, 32)) {
                unknown.push_back(name);
                continue;
            }
            try {
                rows.push_back(materialization_summary(read_materialization(state, project, id)));
            } catch (const std::exception &e) {
                errors.push_back({{"id", id}, {"error", e.what()}});
            }
        }
    std::sort(rows.begin(), rows.end(), [](const Json &a, const Json &b) {
        return a.at("created") == b.at("created") ? a.at("id") < b.at("id")
                                                  : a.at("created") > b.at("created");
    });
    Json page = Json::array();
    for (size_t i = std::min(offset, rows.size()); i < rows.size() && page.size() < limit; ++i)
        page.push_back(rows[i]);
    return {{"records", page},
            {"offset", offset},
            {"limit", limit},
            {"has_more", offset < rows.size() && page.size() < rows.size() - offset},
            {"errors", errors},
            {"unindexed", unknown}};
}
Json check_git_materialization(const fs::path &state, const Json &project, const Json &data) {
    auto id = data.at("receipt").get<std::string>();
    auto record = read_materialization(state, project, id);
    Json result{{"record", materialization_summary(record)},
                {"receipt_hash", sha256(record.dump())},
                {"position", "unknown"},
                {"context_matches", false},
                {"index_metadata_matches", nullptr},
                {"files", Json::array()},
                {"errors", Json::array()},
                {"can_acknowledge", false},
                {"plan", nullptr}};
    try {
        layout(project);
        auto root = fs::u8path(project.at("root").get<std::string>());
        auto head = git_local_head(project);
        if (fs::exists(project_child(root, ".git/cysynck-integration.lock")))
            throw std::runtime_error(
                "An integration lock is present; inspect it before checking LFS recovery");
        auto current_index = sha256(read_git(project, {"ls-files", "--stage", "-z"}));
        auto raw_index = index_digest(project);
        auto flag_hash = sha256(read_git(project, {"ls-files", "-v", "-z"}));
        result["current_index_file_hash"] = raw_index;
        result["index_metadata_matches"] = Json(raw_index) == record.at("index_file_hash");
        result["context_matches"] = head.at("base") == record.at("base") &&
                                    head.at("local_ref") == record.at("local_ref") &&
                                    Json(current_index) == record.at("index_hash") &&
                                    Json(flag_hash) == record.at("index_flags_hash");
        if (!result.at("context_matches").get<bool>())
            result["errors"].push_back(
                "Git HEAD or index differs from the LFS materialization receipt");
        auto inventory = committed_files(project, record.at("base"));
        auto selected = selected_files(inventory, record.value("selection", Json(nullptr)));
        std::map<std::string, Json> committed;
        for (const auto &file : selected)
            committed[file.at("path")] = file;
        if (record.contains("selection")) {
            if (record.at("files").size() != selected.size() ||
                record.at("excluded") != inventory.size() - selected.size())
                throw std::runtime_error(
                    "LFS receipt selection differs from its committed inventory");
            for (const auto &file : record.at("files")) {
                auto found = committed.find(file.at("path").get<std::string>());
                if (found == committed.end())
                    throw std::runtime_error("LFS receipt path is outside its committed selection");
                for (auto key : {"mode", "oid", "pointer_text", "asset"})
                    if (file.at(key) != found->second.at(key))
                        throw std::runtime_error(
                            "LFS receipt selected metadata differs from its committed pointer");
            }
        }
        check_recovery_entries(root, record);
        size_t before_count = 0, after_count = 0, interrupted_count = 0, changed_count = 0;
        for (const auto &file : record.at("files")) {
            if (file.at("state") != "pointer")
                continue;
            auto name = file.at("path").get<std::string>();
            auto found = committed.find(name);
            if (found == committed.end())
                throw std::runtime_error("LFS receipt path is absent from its committed tree");
            for (auto key : {"mode", "oid", "pointer_text", "asset"})
                if (file.at(key) != found->second.at(key))
                    throw std::runtime_error("LFS receipt differs from its committed pointer");
            auto target = project_child(root, name),
                 backup = project_child(root, record.at("recovery_directory").get<std::string>() +
                                                  "/before/" + name),
                 staged = project_child(root, record.at("recovery_directory").get<std::string>() +
                                                  "/after/" + name);
            auto bounded = [&](const fs::path &path, bool asset_file) {
                bool hash_file =
                    fs::is_regular_file(path) &&
                    (Json(fs::file_size(path)) == file.at("before").at("bytes") ||
                     (asset_file && Json(fs::file_size(path)) == file.at("asset").at("size")));
                return fingerprint(path, hash_file);
            };
            auto current = bounded(target, true), original = bounded(backup, false),
                 prepared = bounded(staged, true);
            bool valid_backup = original.at("exists").get<bool>() && original == file.at("before");
            if (original.at("exists").get<bool>() && !valid_backup)
                throw std::runtime_error("Preserved LFS pointer changed in the recovery directory");
            if (prepared.at("exists").get<bool>() && !matches(prepared, file.at("asset")))
                throw std::runtime_error("Staged LFS recovery asset is incomplete or changed");
            std::string position = "changed";
            if (same_pointer(current, file.at("before"))) {
                position = "before";
                ++before_count;
            } else if (matches(current, file.at("asset")) &&
                       current.at("permissions") == file.at("before").at("permissions") &&
                       valid_backup) {
                position = "after";
                ++after_count;
            } else if (!current.at("exists").get<bool>() && valid_backup &&
                       prepared.at("exists").get<bool>()) {
                position = "interrupted";
                ++interrupted_count;
            } else
                ++changed_count;
            result["files"].push_back({{"path", name},
                                       {"position", position},
                                       {"current", current},
                                       {"original", original},
                                       {"staged", prepared}});
        }
        auto total = record.at("pending").get<size_t>();
        result["position"] = before_count == total  ? "before"
                             : after_count == total ? "after"
                             : changed_count        ? "changed"
                             : interrupted_count    ? "interrupted"
                                                    : "partial";
        if (git_local_head(project) != head || index_digest(project) != raw_index)
            throw std::runtime_error("Git context changed during the LFS receipt check");
        bool exact = result.at("position") == "before" || result.at("position") == "after";
        result["can_acknowledge"] = result.at("record").at("needs_attention").get<bool>() &&
                                    exact && result.at("errors").empty();
        if (result.at("can_acknowledge").get<bool>())
            result["plan"] = sha256(result.dump());
    } catch (const std::exception &e) {
        result["can_acknowledge"] = false;
        result["plan"] = nullptr;
        result["errors"].push_back(e.what());
    }
    return result;
}
Json acknowledge_git_materialization(const fs::path &state, const Json &project, const Json &data) {
    auto checked = check_git_materialization(state, project, data);
    if (!checked.at("can_acknowledge").get<bool>() || !data.contains("plan") ||
        data.at("plan") != checked.at("plan"))
        throw std::runtime_error(
            "LFS receipt acknowledgement is blocked or its check changed; check again");
    auto id = data.at("receipt").get<std::string>();
    auto record = read_materialization(state, project, id);
    if (Json(sha256(record.dump())) != checked.at("receipt_hash"))
        throw std::runtime_error("LFS receipt changed during acknowledgement");
    checked.erase("plan");
    checked.erase("record");
    record["acknowledged_at"] = timestamp();
    record["acknowledgement"] = checked;
    auto bytes = record.dump();
    if (bytes.size() > 16 * 1024 * 1024)
        throw std::runtime_error("LFS acknowledgement metadata exceeds 16 MiB");
    atomic_write(materialization_receipt(state, project, id), bytes, true);
    return materialization_summary(record);
}

} // namespace cysynck
