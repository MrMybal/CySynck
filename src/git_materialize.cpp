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
    for (const auto &line : records(read_git(p, {"ls-files", "-v", "-z"}))) {
        if (line.size() < 3 || line[1] != ' ')
            throw std::runtime_error("Invalid Git index flags");
        flags[line.substr(2)] = line.substr(0, 1);
    }
    auto files = committed_files(p, head.at("base"));
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
                   {"files", files},
                   {"pending", pending},
                   {"materialized", materialized},
                   {"skipped", skipped},
                   {"bytes", bytes},
                   {"index_hash", sha256(index)},
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
} // namespace cysynck
