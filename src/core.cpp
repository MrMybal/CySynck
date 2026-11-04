#include <algorithm>
#include <chrono>
#include <ctime>
#include <cysynck/core.hpp>
#include <cysynck/plugin.h>
#include <fstream>
#include <iomanip>
#include <picosha2.h>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include "backup.hpp"
#include "diff.hpp"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cysynck {
void secure_state_path(const fs::path &path, bool directory) {
#ifdef _WIN32
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        throw std::runtime_error("Cannot secure the state directory");
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        CloseHandle(token);
        throw std::runtime_error("Cannot identify the current account");
    }
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &sid))
        throw std::runtime_error("Cannot secure private state");
    std::wstring flags = directory ? L"OICI" : L"";
    std::wstring sddl = L"D:P(A;" + flags + L";FA;;;SY)(A;" + flags + L";FA;;;BA)(A;" + flags +
                        L";FA;;;" + sid + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                              &descriptor, nullptr))
        throw std::runtime_error("Cannot build private state permissions");
    bool secured = SetFileSecurityW(path.c_str(),
                                    DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                    descriptor) != FALSE;
    LocalFree(descriptor);
    if (!secured)
        throw std::runtime_error("Cannot apply private state permissions");
#else
    fs::permissions(
        path, directory ? fs::perms::owner_all : (fs::perms::owner_read | fs::perms::owner_write),
        fs::perm_options::replace);
#endif
}
std::string env(const char *name, const std::string &fallback) {
    auto value = std::getenv(name);
    return value ? value : fallback;
}
fs::path default_state() {
    if (!env("CYSYNCK_STATE").empty())
        return fs::u8path(env("CYSYNCK_STATE"));
#ifdef _WIN32
    return fs::u8path(env("LOCALAPPDATA", env("USERPROFILE"))) / "CySynck";
#elif defined(__APPLE__)
    return fs::u8path(env("HOME")) / "Library/Application Support/CySynck";
#else
    return fs::u8path(env("XDG_STATE_HOME", env("HOME") + "/.local/state")) / "cysynck";
#endif
}
std::string random_id() {
    std::array<unsigned char, 16> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG))
        throw std::runtime_error("Random generator failed");
#else
    std::ifstream source("/dev/urandom", std::ios::binary);
    source.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
    if (!source)
        throw std::runtime_error("Random generator failed");
#endif
    std::ostringstream out;
    for (auto b : bytes)
        out << std::hex << std::setw(2) << std::setfill('0') << int(b);
    return out.str();
}
std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() %
        1000;
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms
        << 'Z';
    return out.str();
}
std::string sha256(const std::string &bytes) { return picosha2::hash256_hex_string(bytes); }
std::string sha256_file(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Cannot read file: " + path_string(path));
    picosha2::hash256_one_by_one hash;
    std::array<char, 65536> buffer;
    while (stream) {
        stream.read(buffer.data(), buffer.size());
        auto n = stream.gcount();
        hash.process(buffer.data(), buffer.data() + n);
    }
    if (!stream.eof())
        throw std::runtime_error("File read failed");
    hash.finish();
    return picosha2::get_hash_hex_string(hash);
}
std::string read_file(const fs::path &path, size_t limit) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Cannot read file: " + path_string(path));
    auto size = fs::file_size(path);
    if (size > limit)
        throw std::runtime_error("File exceeds size limit");
    std::string bytes(static_cast<size_t>(size), '\0');
    stream.read(bytes.data(), static_cast<std::streamsize>(size));
    if (static_cast<uintmax_t>(stream.gcount()) != size)
        throw std::runtime_error("File changed while reading");
    return bytes;
}
void atomic_write(const fs::path &path, const std::string &bytes, bool secret) {
    fs::create_directories(path.parent_path());
    auto temp = path;
    temp += fs::u8path(".part-" + random_id());
    try {
        {
            std::ofstream stream(temp, std::ios::binary);
            if (!stream)
                throw std::runtime_error("Cannot write file");
            stream.write(bytes.data(), bytes.size());
            stream.flush();
            if (!stream)
                throw std::runtime_error("File write failed");
        }
        if (secret)
            secure_state_path(temp, false);
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Atomic file replacement failed");
#else
        int fd = open(temp.c_str(), O_RDONLY);
        if (fd < 0)
            throw std::runtime_error("Cannot flush file");
        auto flushed = fsync(fd);
        close(fd);
        if (flushed)
            throw std::runtime_error("Cannot flush file");
        fs::rename(temp, path);
        int dir = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
        if (dir >= 0) {
            fsync(dir);
            close(dir);
        }
#endif
    } catch (...) {
        std::error_code ec;
        fs::remove(temp, ec);
        throw;
    }
}
static std::string id(const std::string &s) {
    if (!std::regex_match(s, std::regex("[A-Za-z0-9][A-Za-z0-9_-]{0,79}")))
        throw std::runtime_error("Invalid identifier");
    return s;
}
static bool within(const fs::path &a, const fs::path &b) {
    auto left = a.lexically_normal(), right = b.lexically_normal();
    auto x = left.begin(), y = right.begin();
    for (; y != right.end(); ++x, ++y) {
        if (x == left.end())
            return false;
#ifdef _WIN32
        auto px = x->native(), py = y->native();
        if (CompareStringOrdinal(px.c_str(), static_cast<int>(px.size()), py.c_str(),
                                 static_cast<int>(py.size()), TRUE) != CSTR_EQUAL)
            return false;
#else
        if (*x != *y)
            return false;
#endif
    }
    return true;
}
bool path_is_link(const fs::path &path) {
#ifdef _WIN32
    auto attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return fs::is_symlink(fs::symlink_status(path));
#endif
}
fs::path project_child(const fs::path &root, const std::string &relative) {
    if (path_is_link(root))
        throw std::runtime_error("Project root is linked");
    auto p = fs::u8path(relative);
    if (relative.empty() || relative.find('\0') != std::string::npos ||
        relative.find('\\') != std::string::npos || relative.find(':') != std::string::npos ||
        p.is_absolute())
        throw std::runtime_error("Expected a relative project path");
    for (auto &part : p)
        if (part == ".." || part == ".")
            throw std::runtime_error("Invalid relative path");
    auto output = root / p;
    auto current = root;
    for (auto &part : p) {
        current /= part;
        if (path_is_link(current))
            throw std::runtime_error("Links and junctions are excluded");
    }
    if (!within(fs::weakly_canonical(output), fs::weakly_canonical(root)))
        throw std::runtime_error("Path escapes project");
    return output;
}
static bool glob(const std::string &text, const std::string &pattern) {
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
bool project_excludes(const Json &project, const std::string &relative) {
    for (const auto &part : fs::u8path(relative)) {
        auto name = path_string(part);
#ifdef _WIN32
        for (auto &ch : name)
            if (ch >= 'A' && ch <= 'Z')
                ch += 'a' - 'A';
#endif
        if (name == ".cysynck-recovery")
            return true;
    }
    for (const auto &pattern : project.at("excludes")) {
        auto p = pattern.get<std::string>();
        if (glob(relative, p))
            return true;
        for (auto &part : fs::u8path(relative)) {
            auto component = path_string(part);
#ifdef _WIN32
            if (p == ".git" || p == ".stfolder" || p == ".stversions" || p == ".stignore")
                for (auto &ch : component)
                    if (ch >= 'A' && ch <= 'Z')
                        ch += 'a' - 'A';
#endif
            if (glob(component, p))
                return true;
        }
    }
    return false;
}
std::vector<std::pair<std::string, fs::path>> project_files(const Json &project) {
    auto root = fs::u8path(project.at("root").get<std::string>());
    if (!fs::is_directory(root) || path_is_link(root))
        throw std::runtime_error("Project root is missing or linked");
    std::vector<std::pair<std::string, fs::path>> output;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator();
         ++it) {
        auto relative = it->path().lexically_relative(root);
        bool skip = path_is_link(it->path()) || project_excludes(project, path_string(relative));
        if (skip) {
            if (it->is_directory())
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file())
            output.emplace_back(path_string(relative), it->path());
    }
    std::sort(output.begin(), output.end());
    return output;
}
struct Core::Extension {
    void *library = nullptr;
    const cysynck_plugin_v1 *api = nullptr;
    ~Extension() {
#ifdef _WIN32
        if (library)
            FreeLibrary(static_cast<HMODULE>(library));
#else
        if (library)
            dlclose(library);
#endif
    }
};
Core::Core(fs::path state) : state_(fs::weakly_canonical(fs::absolute(state))) {
    fs::create_directories(state_);
    secure_state_path(state_, true);
    fs::create_directories(state_ / "objects");
    DiskLock lock(state_);
    if (!fs::exists(state_ / "registry.json")) {
        registry_ = {{"version", 1},
                     {"peer_id", random_id()},
                     {"projects", Json::array()},
                     {"events", Json::array()},
                     {"settings", Json::object()},
                     {"messages", Json::array()}};
        save();
    } else
        load();
}
Core::~Core() {
    if (engine_child_)
        try {
            std::lock_guard lock(mutex_);
            DiskLock disk(state_);
            load();
            engine_action("stop", Json::object());
            save();
        } catch (...) {
            engine_child_.reset();
            engine_lease_.reset();
        }
}
void Core::load() {
    registry_ = Json::parse(read_file(state_ / "registry.json", 32 * 1024 * 1024));
    if (registry_.at("version") != 1)
        throw std::runtime_error("Unsupported state schema");
}
void Core::save() { atomic_write(state_ / "registry.json", registry_.dump(2), true); }
Json &Core::project(const std::string &value) {
    id(value);
    for (auto &p : registry_["projects"])
        if (p["id"] == value)
            return p;
    throw std::runtime_error("Unknown project");
}
void Core::event(const std::string &project, const std::string &kind, const std::string &message) {
    auto &events = registry_["events"];
    events.push_back({{"id", random_id()},
                      {"created", timestamp()},
                      {"project", project},
                      {"kind", kind},
                      {"message", message}});
    if (events.size() > 2000)
        events.erase(events.begin(), events.begin() + (events.size() - 2000));
}
Json Core::modes() const {
    Json result = Json::array();
    for (auto [key, label] :
         std::vector<std::pair<std::string, std::string>>{{"sendreceive", "Two-way sync"},
                                                          {"sendonly", "Send only"},
                                                          {"receiveonly", "Receive only"},
                                                          {"backup", "Backup only"},
                                                          {"git-bundles", "Git exchange"}})
        result.push_back({{"id", key}, {"label", label}});
    for (auto &ext : plugins_)
        for (size_t i = 0; i < ext->api->mode_count; ++i)
            result.push_back({{"id", ext->api->modes[i].id}, {"label", ext->api->modes[i].label}});
    return result;
}
void Core::load_plugin(const fs::path &path) {
    std::lock_guard lock(mutex_);
    auto extension = std::make_unique<Extension>();
    auto absolute = fs::canonical(path);
#ifdef _WIN32
    extension->library =
        LoadLibraryExW(absolute.c_str(), nullptr,
                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    auto entry = extension->library
                     ? reinterpret_cast<cysynck_plugin_entry_v1>(GetProcAddress(
                           static_cast<HMODULE>(extension->library), "cysynck_get_plugin_v1"))
                     : nullptr;
#else
    extension->library = dlopen(absolute.c_str(), RTLD_NOW | RTLD_LOCAL);
    auto entry = extension->library ? reinterpret_cast<cysynck_plugin_entry_v1>(
                                          dlsym(extension->library, "cysynck_get_plugin_v1"))
                                    : nullptr;
#endif
    if (!entry)
        throw std::runtime_error("Plugin entry point unavailable");
    extension->api = entry();
    auto api = extension->api;
    if (!api || api->abi_version != 1 || !api->id || api->mode_count > 100 ||
        (!api->modes && api->mode_count))
        throw std::runtime_error("Plugin ABI mismatch");
    std::set<std::string> names;
    for (auto &mode : modes())
        names.insert(mode["id"].get<std::string>());
    for (size_t i = 0; i < api->mode_count; ++i) {
        auto &mode = api->modes[i];
        if (!mode.id || !mode.label || !mode.run || !names.insert(id(mode.id)).second)
            throw std::runtime_error("Invalid or duplicate plugin mode");
    }
    plugins_.push_back(std::move(extension));
}
void Core::hook(const std::string &name, const Json &context) {
    auto json = context.dump();
    for (auto &e : plugins_)
        if (e->api->before_event) {
            auto error = e->api->before_event(name.c_str(), json.c_str());
            if (error) {
                std::string copy = error;
                if (e->api->release_string)
                    e->api->release_string(error);
                if (!copy.empty())
                    throw std::runtime_error(copy);
            }
        }
}
Json Core::request_plugin_mode(const Json &p) {
    for (auto &ext : plugins_)
        for (size_t i = 0; i < ext->api->mode_count; ++i) {
            auto &mode = ext->api->modes[i];
            if (p.at("mode") == mode.id) {
                auto projectJson = p.dump(), context = Json{{"state", path_string(state_)}}.dump();
                auto result = mode.run(projectJson.c_str(), context.c_str());
                if (!result)
                    throw std::runtime_error("Plugin returned no result");
                std::string copy = result;
                if (ext->api->release_string)
                    ext->api->release_string(result);
                return Json::parse(copy);
            }
        }
    throw std::runtime_error("Load the plugin providing this mode");
}
Json Core::capture(const fs::path &path) {
    plain_directory(state_ / "objects");
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto size = fs::file_size(path);
        auto time = fs::last_write_time(path);
        auto temp = state_ / "objects" / ("part-" + random_id());
        try {
            picosha2::hash256_one_by_one hash;
            std::ifstream in(path, std::ios::binary);
            std::ofstream out(temp, std::ios::binary);
            if (!in || !out)
                throw std::runtime_error("Cannot capture file");
            std::array<char, 65536> buffer;
            while (in) {
                in.read(buffer.data(), buffer.size());
                auto count = in.gcount();
                hash.process(buffer.data(), buffer.data() + count);
                out.write(buffer.data(), count);
            }
            out.flush();
            if (!in.eof() || !out)
                throw std::runtime_error("Backup read/write failed");
            out.close();
            in.close();
            if (size != fs::file_size(path) || time != fs::last_write_time(path)) {
                fs::remove(temp);
                continue;
            }
            hash.finish();
            auto digest = picosha2::get_hash_hex_string(hash);
            auto destination = state_ / "objects" / digest;
            if (path_is_link(destination) ||
                (fs::exists(destination) && !fs::is_regular_file(destination)))
                throw std::runtime_error("Backup object is linked or is not a regular file");
            if (!fs::exists(destination))
                fs::rename(temp, destination);
            else if (sha256_file(destination) == digest)
                fs::remove(temp);
            else {
                // The captured bytes have this digest; replace a damaged deduplicated copy
                // atomically so older manifests referring to it become usable again.
                secure_state_path(temp, false);
#ifdef _WIN32
                if (!MoveFileExW(temp.c_str(), destination.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("Cannot repair damaged backup object");
#else
                fs::rename(temp, destination);
#endif
            }
            return {{"sha256", digest},
                    {"size", size},
                    {"permissions", static_cast<unsigned>(fs::status(path).permissions())}};
        } catch (...) {
            std::error_code ec;
            fs::remove(temp, ec);
            throw;
        }
    }
    throw std::runtime_error("File keeps changing during backup; retry when writes settle");
}
Json Core::snapshots(const std::string &value) const {
    Json output = Json::array();
    auto directory = state_ / "snapshots" / id(value);
    plain_directory(state_ / "snapshots");
    plain_directory(directory);
    if (!fs::exists(directory))
        return output;
    for (auto &entry : fs::directory_iterator(directory))
        if (entry.path().extension() == ".json")
            output.push_back(read_snapshot(entry.path(), value));
    std::sort(output.begin(), output.end(), [](const Json &a, const Json &b) {
        auto orderA = a.value("created_ns", int64_t{0}), orderB = b.value("created_ns", int64_t{0});
        return orderA == orderB
                   ? a.at("created").get<std::string>() > b.at("created").get<std::string>()
                   : orderA > orderB;
    });
    return output;
}
Json Core::backup(const Json &p, const std::string &reason) {
    hook("before_backup", p);
    auto previous = snapshots(p.at("id"));
    auto order = std::chrono::duration_cast<std::chrono::nanoseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
    if (!previous.empty())
        order = std::max<int64_t>(order, previous[0].value("created_ns", int64_t{0}) + 1);
    Json manifest = {{"id", random_id()},   {"project", p.at("id")}, {"created", timestamp()},
                     {"created_ns", order}, {"reason", reason},      {"files", Json::object()},
                     {"bytes", 0}};
    uintmax_t total = 0;
    for (auto &[name, path] : project_files(p)) {
        auto entry = capture(path);
        total += entry.at("size").get<uintmax_t>();
        manifest["files"][name] = entry;
    }
    manifest["bytes"] = total;
    manifest["file_count"] = manifest["files"].size();
    auto directory = state_ / "snapshots" / p.at("id").get<std::string>();
    auto manifestPath = directory / (manifest.at("id").get<std::string>() + ".json");
    validate_snapshot(manifest, manifestPath, p.at("id"));
    atomic_write(manifestPath, manifest.dump(), true);
    auto list = snapshots(p.at("id"));
    for (size_t i = p.at("backup_keep").get<size_t>(); i < list.size(); ++i)
        fs::remove(directory / (list[i]["id"].get<std::string>() + ".json"));
    event(p.at("id"), "backup",
          "Snapshot " + manifest.at("id").get<std::string>() + " (" +
              std::to_string(manifest["file_count"].get<size_t>()) + " files)");
    manifest.erase("files");
    for (auto &e : plugins_)
        if (e->api->after_event)
            e->api->after_event("after_backup", manifest.dump().c_str());
    return manifest;
}
static Json baseline(const Json &list, const Json &data) {
    auto value = data.value("snapshot", std::string{});
    if (value.empty())
        return list.empty() ? Json{{"files", Json::object()}, {"id", nullptr}} : list[0];
    for (auto &s : list)
        if (s["id"] == value)
            return s;
    throw std::runtime_error("Unknown snapshot");
}
Json Core::changes(const Json &p, const Json &data) {
    auto before = baseline(snapshots(p.at("id")), data);
    Json current = Json::object();
    for (auto &[name, path] : project_files(p))
        current[name] = sha256_file(path);
    std::set<std::string> names;
    for (auto &[name, _] : before["files"].items())
        names.insert(name);
    for (auto &[name, _] : current.items())
        names.insert(name);
    Json result = Json::array();
    for (auto &name : names) {
        bool old = before["files"].contains(name), now = current.contains(name);
        if (old && now && before["files"][name]["sha256"] == current[name])
            continue;
        result.push_back({{"path", name},
                          {"status", !old   ? "added"
                                     : !now ? "deleted"
                                            : "modified"}});
    }
    return {{"baseline", before["id"]}, {"files", result}};
}
Json Core::diff(const Json &p, const Json &data) {
    auto relative = data.at("path").get<std::string>();
    auto path = project_child(fs::u8path(p.at("root").get<std::string>()), relative);
    auto previous = baseline(snapshots(p.at("id")), data);
    fs::path old;
    std::string digest;
    if (previous["files"].contains(relative)) {
        plain_directory(state_ / "objects");
        digest = previous["files"][relative]["sha256"];
        old = state_ / "objects" / digest;
        if (!fs::exists(old))
            throw std::runtime_error("Snapshot object is missing; verify this backup");
    }
    auto result = compare_paths(old, path, relative, digest);
    result["baseline"] = previous["id"];
    return result;
}
Json Core::git_action(const Json &p, const std::string &action, const Json &data) {
    if (action == "git-status")
        return {
            {"branch", git(p, {"symbolic-ref", "--short", "HEAD"})},
            {"head", git(p, {"rev-parse", "HEAD"})},
            {"changes", git(p, {"status", "--short"})},
            {"tracking", git(p, {"for-each-ref", "--format=%(refname:short) %(objectname:short)",
                                 "refs/remotes/cysynck/"})}};
    if (p.at("mode") != "git-bundles")
        throw std::runtime_error("This operation requires git-bundles mode");
    auto exchange = fs::u8path(p.at("exchange_path").get<std::string>());
    if (path_is_link(exchange))
        throw std::runtime_error("Exchange directory is linked");
    if (action == "export") {
        auto branch = git(p, {"symbolic-ref", "--short", "HEAD"});
        auto peer = registry_.at("peer_id").get<std::string>();
        auto directory = exchange / "peers" / peer;
        fs::create_directories(directory);
        if (path_is_link(exchange / "peers") || path_is_link(directory))
            throw std::runtime_error("Exchange links are excluded");
        auto bundle = random_id();
        auto temp = directory / (bundle + ".part"), destination = directory / (bundle + ".bundle");
        try {
            git(p, {"bundle", "create", path_string(temp), "refs/heads/" + branch});
            fs::rename(temp, destination);
        } catch (...) {
            std::error_code ec;
            fs::remove(temp, ec);
            throw;
        }
        Json manifest = {{"version", 1},
                         {"peer", peer},
                         {"id", bundle},
                         {"project", p.at("id")},
                         {"branch", branch},
                         {"head", git(p, {"rev-parse", "refs/heads/" + branch})},
                         {"sha256", sha256_file(destination)},
                         {"created", timestamp()}};
        atomic_write(directory / (bundle + ".json"), manifest.dump(2));
        event(p.at("id"), "git", "Committed branch exported");
        return manifest;
    }
    Json incoming = Json::array();
    auto peers = exchange / "peers";
    if (fs::exists(peers) && !path_is_link(peers))
        for (auto &dir : fs::directory_iterator(peers))
            if (dir.is_directory() && !path_is_link(dir.path()))
                for (auto &entry : fs::directory_iterator(dir.path()))
                    if (entry.path().extension() == ".json" && !path_is_link(entry.path())) {
                        try {
                            auto m = Json::parse(read_file(entry.path(), 16384));
                            auto peer = id(m.at("peer")), bundle = id(m.at("id"));
                            if (path_string(dir.path().filename()) != peer ||
                                path_string(entry.path().stem()) != bundle ||
                                m.at("project") != p.at("id") ||
                                !fs::exists(dir.path() / (bundle + ".bundle")))
                                continue;
                            incoming.push_back(m);
                        } catch (const std::exception &) {
                        }
                    }
    if (action == "inbox")
        return incoming;
    auto peer = id(data.at("peer")), bundle = id(data.at("bundle"));
    for (auto &m : incoming)
        if (m["peer"] == peer && m["id"] == bundle) {
            auto branch = m.at("branch").get<std::string>();
            git(p, {"check-ref-format", "refs/heads/" + branch});
            auto source = peers / peer / (bundle + ".bundle");
            if (path_is_link(source))
                throw std::runtime_error("Bundle links are excluded");
            auto temp = state_ / ("incoming-" + random_id() + ".bundle");
            try {
                fs::copy_file(source, temp);
                if (sha256_file(temp) != m.at("sha256").get<std::string>())
                    throw std::runtime_error("Bundle checksum mismatch");
                git(p, {"bundle", "verify", path_string(temp)});
                auto heads =
                    git(p, {"bundle", "list-heads", path_string(temp), "refs/heads/" + branch});
                if (heads != m.at("head").get<std::string>() + " refs/heads/" + branch)
                    throw std::runtime_error("Bundle head mismatch");
                auto target = "refs/remotes/cysynck/" + peer + "/" + branch;
                git(p, {"-c", "protocol.file.allow=always", "fetch", "--no-tags",
                        "--no-write-fetch-head", path_string(temp),
                        "+refs/heads/" + branch + ":" + target});
                fs::remove(temp);
                event(p.at("id"), "git", "Peer branch imported for review");
                return {{"tracking_ref", target},
                        {"head", m.at("head")},
                        {"working_tree_changed", false}};
            } catch (...) {
                std::error_code ec;
                fs::remove(temp, ec);
                throw;
            }
        }
    throw std::runtime_error("No complete bundle found for this project");
}
Json Core::request(const std::string &method, const std::string &path, const Json &data) {
    if (path == "/api/network/chat" || path == "/api/network/send-file") {
        if (method != "POST")
            throw std::runtime_error("Method not allowed");
        return direct_send(data, path == "/api/network/send-file");
    }
    std::lock_guard lock(mutex_);
    DiskLock disk(state_);
    load();
    if (!data.is_object())
        throw std::runtime_error("Expected a JSON object");
    if (path == "/api/overview" && method == "GET") {
        auto projects = registry_["projects"];
        for (auto &p : projects) {
            p.erase("resolution_pending");
            auto pending = registry_.value("resolution_pending", Json::object());
            if (pending.contains(p.at("id").get<std::string>()))
                p["resolution_pending"] = pending.at(p.at("id").get<std::string>());
            try {
                auto list = snapshots(p.at("id"));
                p["latest_snapshot"] = list.empty() ? Json(nullptr) : list[0];
                if (!list.empty())
                    p["latest_snapshot"].erase("files");
            } catch (const std::exception &e) {
                p["latest_snapshot"] = nullptr;
                p["backup_error"] = e.what();
            }
        }
        return {{"version", version},
                {"peer_id", registry_.at("peer_id")},
                {"projects", projects},
                {"modes", modes()},
                {"syncthing_url", engine_config().at("url")},
                {"syncthing_configured", engine_config().at("key_configured")}};
    }
    if (path == "/api/events" && method == "GET") {
        auto list = registry_["events"];
        std::reverse(list.begin(), list.end());
        return list;
    }
    if ((path == "/api/storage" && method == "GET") ||
        (path == "/api/storage/prune" && method == "POST")) {
        auto result = storage(path == "/api/storage" ? Json::object() : data);
        if (result.at("applied").get<bool>())
            save();
        return result;
    }
    if (path.rfind("/api/engine/", 0) == 0) {
        auto action = path.substr(12);
        bool read = action == "status" || action == "devices" || action == "log";
        if (method != (read ? "GET" : "POST"))
            throw std::runtime_error("Method not allowed");
        auto result = engine_action(action, data);
        save();
        return result;
    }
    if (path == "/api/settings" && method == "GET")
        return registry_["settings"];
    if (path == "/api/settings" && method == "PUT") {
        for (auto &[key, value] : data.items())
            if (key.rfind("ui_", 0) == 0)
                registry_["settings"][key] = value;
        save();
        return registry_["settings"];
    }
    if (path.rfind("/api/network/", 0) == 0) {
        auto result = network_action(path.substr(13), data);
        save();
        return result;
    }
    auto prefix = std::string("/api/projects/");
    auto suffix = path.rfind(prefix, 0) == 0 ? path.substr(prefix.size()) : "";
    auto slash = suffix.find('/');
    bool update = !suffix.empty() && slash == std::string::npos && method == "PUT";
    if ((path == "/api/projects" && method == "POST") || update) {
        Json p = update ? project(suffix)
                        : Json{{"id", random_id()},
                               {"mode", "sendreceive"},
                               {"backup_interval_minutes", 60},
                               {"backup_keep", 30},
                               {"backup_before_sync", true},
                               {"paused", false},
                               {"exchange_path", ""},
                               {"excludes",
                                {".git", ".stfolder", ".stversions", ".stignore",
                                 "*.sync-conflict-*", ".cysynck-recovery"}}};
        auto original = p;
        for (auto &[key, value] : data.items())
            p[key] = value;
        for (auto key : {"resolution_pending", "latest_snapshot", "backup_error"})
            p.erase(key);
        id(p.at("id"));
        auto name = p.at("name").get<std::string>();
        if (name.empty() || name.size() > 160)
            throw std::runtime_error("Project name required (160 bytes maximum)");
        auto root = fs::canonical(fs::u8path(p.at("root").get<std::string>()));
        if (!fs::is_directory(root) || within(root, state_) || within(state_, root))
            throw std::runtime_error("Project and state directories must be separate");
        p["root"] = path_string(root);
        bool known = false;
        for (auto &m : modes())
            if (m["id"] == p.at("mode"))
                known = true;
        if (!known)
            throw std::runtime_error("Unknown mode; load the appropriate plugin");
        if (!p.at("backup_interval_minutes").is_number_integer() ||
            p.at("backup_interval_minutes").get<int>() < 0 ||
            p.at("backup_interval_minutes").get<int>() > 525600 ||
            !p.at("backup_keep").is_number_integer() || p.at("backup_keep").get<int>() < 1 ||
            p.at("backup_keep").get<int>() > 10000)
            throw std::runtime_error("Invalid backup interval or retention");
        if (!p.at("paused").is_boolean() || !p.at("backup_before_sync").is_boolean() ||
            !p.at("excludes").is_array())
            throw std::runtime_error("Invalid project settings");
        for (auto &pattern : p["excludes"])
            if (!pattern.is_string() || pattern.get<std::string>().empty())
                throw std::runtime_error("Invalid exclusion glob");
        auto exchange = p.at("exchange_path").get<std::string>();
        if (!exchange.empty())
            p["exchange_path"] =
                path_string(fs::weakly_canonical(fs::absolute(fs::u8path(exchange))));
        if (p["mode"] == "git-bundles" && exchange.empty())
            throw std::runtime_error("Git exchange requires a separate exchange_path");
        std::vector<fs::path> paths{root};
        if (!exchange.empty())
            paths.push_back(fs::u8path(p["exchange_path"].get<std::string>()));
        for (auto &a : paths)
            for (auto &b : std::vector<fs::path>{state_})
                if (within(a, b) || within(b, a))
                    throw std::runtime_error("Exchange and state directories overlap");
        if (paths.size() > 1 && (within(paths[0], paths[1]) || within(paths[1], paths[0])))
            throw std::runtime_error("Working tree and exchange must be separate");
        for (auto &existing : registry_["projects"]) {
            if (existing["id"] == p["id"]) {
                if (!update)
                    throw std::runtime_error("Project ID already exists");
                continue;
            }
            for (auto &a : paths)
                for (auto &key : {"root", "exchange_path"})
                    if (!existing[key].get<std::string>().empty()) {
                        auto b = fs::u8path(existing[key].get<std::string>());
                        if (within(a, b) || within(b, a))
                            throw std::runtime_error("Project directories may not overlap");
                    }
        }
        if (update) {
            for (auto &key : {"id", "root", "mode", "exchange_path"})
                if (p[key] != original[key])
                    throw std::runtime_error("ID, root, mode and exchange path are immutable");
            project(suffix) = p;
        } else
            registry_["projects"].push_back(p);
        event(p.at("id"), "project", "Project settings saved");
        save();
        return p;
    }
    if (suffix.empty() || slash == std::string::npos)
        throw std::runtime_error("Unknown endpoint");
    auto projectId = suffix.substr(0, slash);
    auto action = suffix.substr(slash + 1);
    auto &p = project(projectId);
    Json result;
    if (action == "snapshots" && method == "GET") {
        result = snapshots(projectId);
        for (auto &item : result)
            item.erase("files");
        return result;
    }
    if (method != "POST")
        throw std::runtime_error("Method not allowed");
    if (action == "backup")
        result = backup(p, "manual");
    else if (action == "changes")
        result = changes(p, data);
    else if (action == "diff")
        result = diff(p, data);
    else if (action == "compare")
        result = compare(p, data);
    else if (action == "conflicts")
        result = conflicts(p, Json::object());
    else if (action == "conflict-diff")
        result = conflicts(p, {{"path", data.at("path")}});
    else if (action == "resolution-plan")
        result = resolution_plan(p, data);
    else if (action == "resolve-conflict")
        result = resolve_conflict(p, data);
    else if (action == "resolutions")
        result = resolutions(p);
    else if (action == "resolution-export")
        result = export_resolution(p, data);
    else if (action == "resolution-acknowledge")
        result = acknowledge_resolution(p, data);
    else if (action == "restore")
        result = restore(p, data);
    else if (action == "restore-plan")
        result = restore_plan(p, data);
    else if (action == "verify")
        result = verify(p, data);
    else if (action == "received-files" || action == "received-verify" ||
             action == "received-export")
        result = direct_inbox(p, action, data);
    else if (action == "git-status" || action == "export" || action == "inbox" ||
             action == "import")
        result = git_action(p, action, data);
    else
        result = sync_action(p, action, data);
    save();
    return result;
}
void Core::tick() {
    std::lock_guard lock(mutex_);
    DiskLock disk(state_);
    load();
    maintain_engine();
    for (auto &p : registry_["projects"])
        try {
            if (p.at("paused").get<bool>() || p.at("backup_interval_minutes") == 0)
                continue;
            auto list = snapshots(p.at("id"));
            bool due = list.empty();
            if (!due) {
                auto path = state_ / "snapshots" / p["id"].get<std::string>() /
                            (list[0]["id"].get<std::string>() + ".json");
                auto age = fs::file_time_type::clock::now() - fs::last_write_time(path);
                due = age >= std::chrono::minutes(p["backup_interval_minutes"].get<int>());
            }
            if (due)
                backup(p, "scheduled");
        } catch (const std::exception &error) {
            event(p.at("id"), "error", error.what());
        }
    save();
}
std::string Core::api_token() {
    std::lock_guard lock(mutex_);
    DiskLock disk(state_);
    auto path = state_ / "api-token";
    if (!fs::exists(path))
        atomic_write(path, random_id() + random_id(), true);
    return read_file(path, 256);
}
} // namespace cysynck
