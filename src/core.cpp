#include <algorithm>
#include <chrono>
#include <ctime>
#include <cysynck/core.hpp>
#include <cysynck/plugin.h>
#include "plugin_metadata.hpp"
#include "help.hpp"
#include <fstream>
#include <iomanip>
#include <picosha2.h>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include "backup.hpp"
#include "backup_data.hpp"
#include "backup_plan.hpp"
#include "snapshot.hpp"
#include "diff.hpp"
#include "git_lfs.hpp"
#include "git_review.hpp"
#include "git_setup.hpp"
#include "git_extract.hpp"
#include "git_integrate.hpp"
#include "git_materialize.hpp"
#include "git_cache.hpp"
#include "git_storage.hpp"
#include "cyrevision_exchange.hpp"
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
#ifndef _WIN32
    struct stat previous{};
    // A sudo tunnel command must not make an existing user's registry unreadable.
    // Inspect the entry itself; never follow a destination link for ownership.
    bool preserve_owner = geteuid() == 0 && lstat(path.c_str(), &previous) == 0 &&
                          S_ISREG(previous.st_mode);
#endif
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
        int fd = open(temp.c_str(), O_RDONLY | O_NOFOLLOW);
        if (fd < 0)
            throw std::runtime_error("Cannot flush file");
        if (preserve_owner && fchown(fd, previous.st_uid, previous.st_gid) != 0) {
            close(fd);
            throw std::runtime_error("Cannot preserve the existing file owner during privileged replacement");
        }
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
    for (const auto &part : p)
        if (part == ".." || part == ".")
            throw std::runtime_error("Invalid relative path");
    auto output = root / p;
    auto current = root;
    for (const auto &part : p) {
        current /= part;
        if (path_is_link(current))
            throw std::runtime_error("Links and junctions are excluded");
    }
    if (!within(fs::weakly_canonical(output), fs::weakly_canonical(root)))
        throw std::runtime_error("Path escapes project");
    return output;
}
bool project_excludes(const Json &project, const std::string &relative) {
    return !backup_exclusion(project, relative).is_null();
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
    fs::path path;
    Json catalog;
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
    for (const auto &ext : plugins_)
        for (const auto &mode : ext->catalog.at("modes"))
            result.push_back(mode);
    return result;
}
Json Core::plugin_catalog() const {
    Json plugins = Json::array();
    for (const auto &ext : plugins_)
        plugins.push_back(ext->catalog);
    return {{"abi_version", 1}, {"plugins", plugins}};
}
Json Core::normalized_mode_options(const Json &p) const {
    for (const auto &mode : modes())
        if (mode.at("id") == p.at("mode"))
            return validate_mode_options(mode, p.value("mode_options", Json::object()));
    throw std::runtime_error("Load the plugin providing this mode");
}
Json Core::mode_configuration(const Json &p) const {
    Json result{{"mode", p.at("mode")},       {"available", false},      {"valid", false},
                {"plugin", nullptr},          {"fields", Json::array()}, {"values", Json::object()},
                {"defaults", Json::object()}, {"errors", Json::array()}, {"plan", nullptr}};
    for (const auto &mode : modes()) {
        if (mode.at("id") != p.at("mode"))
            continue;
        result["available"] = true;
        result["plugin"] = mode.value("plugin", Json());
        result["fields"] = mode.value("fields", Json::array());
        result["defaults"] = validate_mode_options(mode, Json::object());
        result["values"] = result.at("defaults");
        try {
            result["values"] = normalized_mode_options(p);
            result["valid"] = true;
        } catch (const std::exception &e) {
            result["errors"].push_back(e.what());
        }
        result["schema_hash"] = sha256(mode.dump());
        result["plan"] = sha256(Json{{"state", path_string(state_)},
                                     {"project", p.at("id")},
                                     {"root", p.at("root")},
                                     {"mode", p.at("mode")},
                                     {"schema", result.at("schema_hash")},
                                     {"stored", p.value("mode_options", Json::object())}}
                                    .dump());
        return result;
    }
    result["errors"].push_back("Load the plugin providing this mode");
    return result;
}
void Core::load_plugin(const fs::path &path) {
    std::lock_guard lock(mutex_);
    if (plugins_.size() >= 32)
        throw std::runtime_error("At most 32 native plugins can be loaded");
    auto extension = std::make_unique<Extension>();
    auto absolute = fs::canonical(path);
    if (!fs::is_regular_file(absolute))
        throw std::runtime_error("Choose a native plugin library file");
    for (const auto &loaded : plugins_) {
        std::error_code error;
        if (loaded->path == absolute || fs::equivalent(loaded->path, absolute, error))
            throw std::runtime_error("Plugin library is already loaded");
    }
    auto digest = sha256_file(absolute);
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
#ifdef _WIN32
    auto descriptor = reinterpret_cast<cysynck_plugin_descriptor_v1>(GetProcAddress(
        static_cast<HMODULE>(extension->library), "cysynck_get_plugin_descriptor_v1"));
#else
    auto descriptor = reinterpret_cast<cysynck_plugin_descriptor_v1>(
        dlsym(extension->library, "cysynck_get_plugin_descriptor_v1"));
#endif
    extension->catalog = describe_plugin(api, descriptor ? descriptor() : nullptr);
    if (descriptor && !extension->catalog.at("descriptor").get<bool>())
        throw std::runtime_error("Plugin descriptor export returned no metadata");
    if (sha256_file(absolute) != digest)
        throw std::runtime_error("Plugin library changed while loading");
    extension->path = absolute;
    extension->catalog["path"] = path_string(absolute);
    extension->catalog["sha256"] = digest;
    for (auto &mode : extension->catalog["modes"])
        mode["schema_hash"] = sha256(Json{
            {"plugin", extension->catalog.at("id")},
            {"library", digest},
            {"mode", mode}}.dump());
    for (const auto &loaded : plugins_)
        if (loaded->catalog.at("id") == extension->catalog.at("id"))
            throw std::runtime_error("Plugin identifier is already loaded");
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
                auto resolved = p;
                resolved["mode_options"] = normalized_mode_options(p);
                auto projectJson = resolved.dump(),
                     context = Json{{"state", path_string(state_)},
                                    {"plugin", ext->catalog.at("id")},
                                    {"mode_options", resolved.at("mode_options")}}
                                   .dump();
                auto result = mode.run(projectJson.c_str(), context.c_str());
                if (!result)
                    throw std::runtime_error("Plugin returned no result");
                std::string copy;
                try {
                    copy = plugin_text(result, 1024 * 1024, true);
                } catch (...) {
                    if (ext->api->release_string)
                        ext->api->release_string(result);
                    throw;
                }
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
    auto storage_policy = p.value("backup_storage", "auto");
    if (storage_policy != "auto" && storage_policy != "whole" && storage_policy != "cdc")
        throw std::runtime_error("Choose backup_storage auto, whole or cdc");
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
    Json block_stats = {
        {"block_size", backup_block_size}, {"chunked_files", 0}, {"new_block_bytes", 0},
        {"reused_block_bytes", 0},         {"new_blocks", 0},    {"reused_blocks", 0},
        {"content_defined_files", 0}};
    for (auto &[name, path] : project_files(p)) {
        Json entry;
        auto size = fs::file_size(path);
        bool cdc = storage_policy == "cdc";
        auto block_limit = (cdc ? backup_cdc_min : backup_block_size) * backup_max_blocks;
        if (storage_policy != "whole" && size >= backup_block_threshold && size <= block_limit) {
            auto blocks = capture_blocks(state_, path, cdc);
            entry = blocks.entry;
            block_stats["chunked_files"] = block_stats["chunked_files"].get<uintmax_t>() + 1;
            if (cdc)
                block_stats["content_defined_files"] =
                    block_stats["content_defined_files"].get<uintmax_t>() + 1;
            for (auto [key, value] : {std::pair{"new_block_bytes", blocks.written_bytes},
                                      {"reused_block_bytes", blocks.reused_bytes},
                                      {"new_blocks", blocks.written_blocks},
                                      {"reused_blocks", blocks.reused_blocks}})
                block_stats[key] = block_stats[key].get<uintmax_t>() + value;
        } else
            entry = capture(path);
        total += entry.at("size").get<uintmax_t>();
        manifest["files"][name] = entry;
    }
    manifest["format"] = 2;
    manifest["block_storage"] = block_stats;
    manifest["bytes"] = total;
    manifest["file_count"] = manifest["files"].size();
    auto directory = state_ / "snapshots" / p.at("id").get<std::string>();
    auto manifestPath = directory / (manifest.at("id").get<std::string>() + ".json");
    validate_snapshot(manifest, manifestPath, p.at("id"));
    auto encoded = manifest.dump();
    if (encoded.size() > 64 * 1024 * 1024)
        throw std::runtime_error("Snapshot manifest exceeds the 64 MiB catalog limit");
    atomic_write(manifestPath, encoded, true);
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
    Json result;
    if (previous["files"].contains(relative)) {
        result = compare_backup(state_, previous["files"][relative], path, relative);
    } else
        result = compare_paths({}, path, relative);
    result["baseline"] = previous["id"];
    return result;
}
Json Core::request(const std::string &method, const std::string &path, const Json &data) {
    if (path == "/api/maintenance/tick") {
        if (method != "POST") throw std::runtime_error("Method not allowed");
        if (!data.is_object() || !data.empty()) throw std::runtime_error("Maintenance tick accepts an empty JSON object");
        // tick() owns its locks. Keep this outside the request lock, as for transfers.
        tick();
        return {{"maintained", true}};
    }
    if (path == "/api/network/transfer-resume") {
        if (method != "POST")
            throw std::runtime_error("Method not allowed");
        return transfer_send(data);
    }
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
    if (path == "/api/help/location") {
        if (method != "POST") throw std::runtime_error("Method not allowed");
        return help::location(data.value("page", "overview"), data.value("section", ""),
                              data.value("language", "en"));
    }
    if (path == "/api/cyrevision/identity") {
        if (method != "GET" && method != "POST") throw std::runtime_error("Method not allowed");
        return cyrevision_action(state_, nullptr, "identity", method == "GET" ? Json::object() : data);
    }
    if (path == "/api/doctor" && (method == "GET" || method == "POST"))
        return diagnostics(method == "GET" ? Json::object() : data);
    if (method == "POST" && (path == "/api/service/plan" || path == "/api/service/export"))
        return service(path.substr(13), data);
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
    if (path == "/api/plugins" && method == "GET")
        return plugin_catalog();
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
        if (path == "/api/network/transfer-limits" || path == "/api/network/transfer-configure") {
            if (method != (path.ends_with("-limits") ? "GET" : "POST"))
                throw std::runtime_error("Method not allowed");
            auto result = transfer_action(path.substr(13), data);
            if (method == "POST")
                save();
            return result;
        }
        if (path == "/api/network/transfers" || path == "/api/network/transfer-cancel") {
            if (method != "POST")
                throw std::runtime_error("Method not allowed");
            auto result = transfer_action(path.substr(13), data);
            save();
            return result;
        }
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
        p["mode_options"] = normalized_mode_options(p);
        if (!p.at("backup_interval_minutes").is_number_integer() ||
            p.at("backup_interval_minutes").get<int>() < 0 ||
            p.at("backup_interval_minutes").get<int>() > 525600 ||
            !p.at("backup_keep").is_number_integer() || p.at("backup_keep").get<int>() < 1 ||
            p.at("backup_keep").get<int>() > 10000)
            throw std::runtime_error("Invalid backup interval or retention");
        if (!p.at("paused").is_boolean() || !p.at("backup_before_sync").is_boolean() ||
            !p.at("excludes").is_array())
            throw std::runtime_error("Invalid project settings");
        auto backupStorage = p.value("backup_storage", "auto");
        if (backupStorage != "auto" && backupStorage != "whole" && backupStorage != "cdc")
            throw std::runtime_error("Choose backup_storage auto, whole or cdc");
        p["backup_storage"] = backupStorage;
        if (!p.value("git_lfs", Json(true)).is_boolean() ||
            !p.value("git_lfs_storage", Json("")).is_string())
            throw std::runtime_error("Invalid project Git settings");
        p["git_lfs"] = p.value("git_lfs", true);
        auto approved = p.value("git_lfs_storage", "");
        p["git_lfs_storage"] =
            approved.empty() ? "" : path_string(approved_lfs_directory(approved));
        validate_backup_excludes(p.at("excludes"));
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
        if (!approved.empty()) {
            auto cache = fs::u8path(p.at("git_lfs_storage").get<std::string>());
            auto protectedPaths = paths;
            protectedPaths.push_back(state_);
            for (const auto &existing : registry_["projects"])
                for (auto key : {"root", "exchange_path"})
                    if (!existing.at(key).get<std::string>().empty())
                        protectedPaths.push_back(fs::u8path(existing.at(key).get<std::string>()));
            for (const auto &protectedPath : protectedPaths)
                if (within(cache, protectedPath) || within(protectedPath, cache))
                    throw std::runtime_error("Custom LFS storage must be separate from working "
                                             "trees, exchange and profile state");
        }
        for (auto &existing : registry_["projects"]) {
            if (existing["id"] == p["id"]) {
                if (!update)
                    throw std::runtime_error("Project ID already exists");
                continue;
            }
            for (auto &a : paths)
                if (!existing.value("git_lfs_storage", "").empty()) {
                    auto cache = fs::u8path(existing.at("git_lfs_storage").get<std::string>());
                    if (within(a, cache) || within(cache, a))
                        throw std::runtime_error(
                            "Project directories overlap an approved LFS cache");
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
    if (action.starts_with("cyrevision-")) {
        auto operation = action.substr(11);
        bool read = operation == "status" || operation == "inbox";
        if (method != (read ? "GET" : "POST")) throw std::runtime_error("Method not allowed");
        auto result = cyrevision_action(state_, p, operation, data);
        if (!read) {
            event(projectId, "git", "CyRevision " + operation + " completed");
            save();
        }
        return result;
    }
    if (action == "mode-options") {
        auto config = mode_configuration(p);
        if (method == "GET")
            return config;
        if (method != "PUT")
            throw std::runtime_error("Method not allowed");
        if (!config.at("available").get<bool>() || !data.contains("plan") ||
            data.at("plan") != config.at("plan"))
            throw std::runtime_error("Mode options or schema changed; load them again");
        auto candidate = p;
        candidate["mode_options"] = data.at("values");
        p["mode_options"] = normalized_mode_options(candidate);
        event(projectId, "project", "Plugin mode options saved");
        save();
        return mode_configuration(p);
    }
    if (action == "backup-excludes") {
        if (method != "PUT")
            throw std::runtime_error("Method not allowed");
        auto preview = preview_backup(state_, p, {{"excludes", data.at("excludes")}});
        if (!data.contains("plan") || data.at("plan") != preview.at("plan"))
            throw std::runtime_error(
                "Backup rules or inventory changed; preview again before saving");
        p["excludes"] = preview.at("excludes");
        event(projectId, "project", "Backup exclusion rules saved");
        save();
        return {{"saved", true}, {"excludes", p.at("excludes")}};
    }
    if (action == "snapshots" && method == "GET") {
        result = snapshots(projectId);
        for (auto &item : result)
            item.erase("files");
        return result;
    }
    if (method != "POST")
        throw std::runtime_error("Method not allowed");
    if (action == "backup-preview")
        return preview_backup(state_, p, data);
    if (action == "backup") {
        if (data.contains("plan") &&
            data.at("plan") != preview_backup(state_, p, Json::object()).at("plan"))
            throw std::runtime_error(
                "Backup rules or inventory changed; preview again before backing up");
        result = backup(p, "manual");
    } else if (action == "changes")
        result = changes(p, data);
    else if (action == "snapshot-files" || action == "snapshot-compare" ||
             action == "snapshot-compare-file")
        return snapshot_query(state_, p, action, data);
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
    else if (action == "git-branches")
        result = imported_git_branches(p);
    else if (action == "git-setup") {
        result = setup_git_repository(p, data);
        if (result.value("applied", false))
            event(p.at("id"), "git", "Local Git repository initialized without checking out files");
    } else if (action == "git-extract") {
        result = extract_git_revision(state_, registry_.at("projects"), p, data);
        if (result.value("applied", false))
            event(p.at("id"), "git", "Reviewed Git revision extracted to a new directory");
    } else if (action == "git-integrate" || action == "git-merge") {
        result = integrate_git_branch(state_, p, data, action == "git-merge");
        if (result.value("applied", false))
            event(p.at("id"), "git",
                  action == "git-merge" ? "Reviewed divergent Git branches merged"
                                        : "Reviewed imported branch integrated by fast-forward");
    } else if (action == "git-lfs-cache")
        return inspect_git_lfs_cache(state_, p, data);
    else if (action == "git-lfs-recover") {
        result = recover_git_lfs_cache(state_, p, data);
        if (result.value("applied", false))
            event(projectId, "git", "Missing Git LFS cache objects recovered from exchange");
    } else if (action == "git-lfs-recoveries" || action == "git-lfs-recovery")
        return git_lfs_recoveries(state_, p, data, action == "git-lfs-recovery");
    else if (action == "git-lfs-recovery-check")
        return check_git_lfs_recovery(state_, p, data);
    else if (action == "git-lfs-recovery-acknowledge") {
        result = acknowledge_git_lfs_recovery(state_, p, data);
        event(projectId, "git",
              "Interrupted LFS cache recovery marked as reviewed; receipt preserved");
    } else if (action == "git-materialize") {
        result = materialize_git_lfs(state_, p, data);
        if (result.value("applied", false))
            event(p.at("id"), "git",
                  result.value("operation", "materialize") == "dehydrate"
                      ? "Committed LFS pointers restored; unchanged working assets preserved in "
                        "recovery"
                      : "Committed LFS assets materialized from verified local cache");
    } else if (action == "git-materializations")
        result = git_materializations(state_, p, data);
    else if (action == "git-materialization-check")
        result = check_git_materialization(state_, p, data);
    else if (action == "git-materialization-resume") {
        result = resume_git_materialization(state_, p, data);
        if (result.value("resumed", false))
            event(p.at("id"), "git",
                  "Interrupted LFS publication completed from verified staging; original pointers "
                  "preserved");
    } else if (action == "git-materialization-export") {
        result = export_git_materialization(state_, p, registry_.at("projects"), data);
        if (result.value("exported", false))
            event(p.at("id"), "git", "Preserved LFS assets exported to a separate verified copy");
    } else if (action == "git-materialization-acknowledge") {
        result = acknowledge_git_materialization(state_, p, data);
        event(p.at("id"), "git",
              "Interrupted LFS materialization marked as reviewed; recovery files preserved");
    } else if (action == "git-integrations")
        result = git_integrations(state_, p, data);
    else if (action == "git-integration-check")
        result = check_git_integration(state_, p, data);
    else if (action == "git-integration-acknowledge") {
        result = acknowledge_git_integration(state_, p, data);
        event(p.at("id"), "git",
              "Interrupted Git integration marked as reviewed; receipt preserved");
    } else if (action == "git-review" || action == "git-review-file")
        result = review_git_branch(p, data, action == "git-review-file");
    else if (action == "git-merge-review" || action == "git-merge-file")
        result = review_git_merge(state_, p, data, action == "git-merge-file");
    else if (action == "git-history")
        result = git_branch_history(p, data);
    else if (action == "git-commit" || action == "git-commit-file")
        result = review_git_commit(p, data, action == "git-commit-file");
    else if (action == "git-storage") {
        result = git_exchange_storage(p, registry_.at("peer_id"), data);
        if (result.value("applied", false))
            event(p.at("id"), "git", "Reviewed local Git export cleanup applied");
    } else
        result = sync_action(p, action, data);
    save();
    return result;
}
void Core::tick() {
    std::lock_guard lock(mutex_);
    DiskLock disk(state_);
    load();
    maintain_engine();
    maintain_network();
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
