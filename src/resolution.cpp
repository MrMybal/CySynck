#include "backup.hpp"
#include "diff.hpp"
#include "error.hpp"
#include <algorithm>
#include <fstream>
#include <regex>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace cysynck {
namespace {
constexpr uintmax_t file_limit = 100 * 1024 * 1024, text_limit = 512 * 1024;
std::string record_id(const Json &data) {
    auto id = data.at("resolution").get<std::string>();
    if (!std::regex_match(id, std::regex("[a-f0-9]{32}")))
        throw std::runtime_error("Invalid resolution identifier");
    return id;
}
Json fingerprint(const fs::path &path) {
    if (path_is_link(path))
        throw std::runtime_error("Resolution excludes links and junctions");
    if (!fs::exists(path))
        return {{"exists", false},
                {"bytes", 0},
                {"sha256", nullptr},
                {"permissions", 0},
                {"modified_ns", 0}};
    if (!fs::is_regular_file(path))
        throw std::runtime_error("Resolution expects regular files");
    auto bytes = fs::file_size(path);
    if (bytes > file_limit)
        throw std::runtime_error("Conflict resolution is limited to 100 MiB per file");
    auto time = fs::last_write_time(path);
    auto mode = fs::status(path).permissions();
    auto digest = sha256_file(path);
    if (path_is_link(path) || fs::file_size(path) != bytes || fs::last_write_time(path) != time ||
        fs::status(path).permissions() != mode)
        throw std::runtime_error("File changed during preparation; prepare again");
    return {{"exists", true},
            {"bytes", bytes},
            {"sha256", digest},
            {"permissions", static_cast<unsigned>(mode)},
            {"modified_ns", time.time_since_epoch().count()}};
}
void private_directory(const fs::path &path) {
    plain_directory(path);
    if (!fs::exists(path)) {
        if (!fs::create_directory(path))
            throw std::runtime_error("Cannot create recovery directory");
        secure_state_path(path, true);
    }
}
fs::path records_directory(const fs::path &state, const Json &p, bool create) {
    auto directory = state / "resolutions";
    if (create)
        private_directory(directory);
    else
        plain_directory(directory);
    directory /= fs::u8path(p.at("id").get<std::string>());
    if (create)
        private_directory(directory);
    else
        plain_directory(directory);
    return directory;
}
void flush_file(const fs::path &path) {
#ifdef _WIN32
    auto handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot flush recovery file");
    bool ok = FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
#else
    int handle = open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (handle < 0)
        throw std::runtime_error("Cannot flush recovery file");
    bool ok = fsync(handle) == 0;
    close(handle);
#endif
    if (!ok)
        throw std::runtime_error("Cannot flush recovery file");
}
void verify_copy(const fs::path &path, const Json &info) {
    if (path_is_link(path) || !fs::is_regular_file(path) ||
        fs::file_size(path) != info.at("bytes") ||
        sha256_file(path) != info.at("sha256").get<std::string>())
        throw std::runtime_error("Recovery file is missing or its checksum is invalid");
}
void copy_private(const fs::path &from, const fs::path &to, const Json &info) {
    fs::copy_file(from, to);
    secure_state_path(to, false);
    fs::permissions(to, fs::perms::owner_read | fs::perms::owner_write);
    flush_file(to);
    verify_copy(to, info);
}
void source_permissions(const fs::path &from, const fs::path &to, unsigned mode) {
    fs::permissions(to, static_cast<fs::perms>(mode));
#ifdef _WIN32
    DWORD size = 0;
    GetFileSecurityW(from.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &size);
    if (!size)
        throw std::runtime_error("Cannot preserve source file access permissions");
    std::vector<unsigned char> security(size);
    auto descriptor = reinterpret_cast<PSECURITY_DESCRIPTOR>(security.data());
    if (!GetFileSecurityW(from.c_str(), DACL_SECURITY_INFORMATION, descriptor, size, &size))
        throw std::runtime_error("Cannot read source file access permissions");
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (!GetSecurityDescriptorControl(descriptor, &control, &revision) ||
        !SetFileSecurityW(to.c_str(),
                          DACL_SECURITY_INFORMATION | ((control & SE_DACL_PROTECTED)
                                                           ? PROTECTED_DACL_SECURITY_INFORMATION
                                                           : UNPROTECTED_DACL_SECURITY_INFORMATION),
                          descriptor))
        throw std::runtime_error("Cannot preserve source file access permissions");
#else
    (void)from;
#endif
}
void publish_new(const fs::path &from, const fs::path &to) {
    // Destination creation is exclusive. A file created by another app is never replaced.
#ifdef _WIN32
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish file without replacing an existing destination");
#else
    fs::create_hard_link(from, to);
    std::error_code ec;
    fs::remove(from, ec); // The source is a private staging path, never a working file.
#endif
}
void move_source(const fs::path &root, const std::string &relative, const fs::path &archive,
                 const Json &expected) {
    auto path = project_child(root, relative);
    if (fingerprint(path) != expected)
        throw std::runtime_error("Resolution inputs changed; prepare again");
    if (fs::exists(archive) || path_is_link(archive))
        throw std::runtime_error("Recovery destination already exists");
    // The destination is in a newly claimed private directory on the same filesystem.
    // Rename preserves the actual inode, including open writers on POSIX.
    fs::rename(path, archive);
    if (fingerprint(archive) != expected)
        throw std::runtime_error("Source changed while moving; recovery copy preserved");
}
void validate_info(const Json &info) {
    if (!info.at("exists").is_boolean() || !info.at("bytes").is_number_integer() ||
        info.at("bytes").get<int64_t>() < 0 || info.at("bytes").get<uintmax_t>() > file_limit ||
        !info.at("permissions").is_number_integer() || info.at("permissions").get<int64_t>() < 0 ||
        info.at("permissions").get<unsigned>() > 07777 ||
        !info.at("modified_ns").is_number_integer())
        throw std::runtime_error("Invalid resolution file metadata");
    if (info.at("exists").get<bool>()) {
        if (!info.at("sha256").is_string() ||
            !std::regex_match(info.at("sha256").get<std::string>(), std::regex("[a-f0-9]{64}")))
            throw std::runtime_error("Invalid resolution checksum");
    } else if (!info.at("sha256").is_null() || info.at("bytes") != 0)
        throw std::runtime_error("Invalid missing-file metadata");
}
Json read_record(const fs::path &directory, const Json &p, const std::string &id) {
    auto record = directory / id;
    plain_directory(record);
    auto manifest = record / "resolution.json";
    if (path_is_link(manifest) || !fs::is_regular_file(manifest))
        throw std::runtime_error("Resolution journal is missing or linked");
    auto item = Json::parse(read_file(manifest, 65536));
    if (item.at("schema") != 1 || item.at("id") != id || item.at("project") != p.at("id") ||
        item.at("root") != p.at("root") || !item.at("created").is_string())
        throw std::runtime_error("Invalid resolution journal identity");
    auto parsed = conflict_identity(item.at("path"));
    if (parsed.at("original") != item.at("original") ||
        item.at("recovery_directory") != ".cysynck-recovery/" + id)
        throw std::runtime_error("Invalid resolution recovery paths");
    for (auto field : {"current", "conflict", "result"})
        validate_info(item.at(field));
    if (!item.at("conflict").at("exists").get<bool>())
        throw std::runtime_error("Resolution journal has no conflict source");
    auto choice = item.at("choice").get<std::string>(), phase = item.at("phase").get<std::string>();
    if ((choice != "current" && choice != "conflict" && choice != "merged") ||
        (phase != "preparing" && phase != "prepared" && phase != "moving-current" &&
         phase != "moving-conflict" && phase != "publishing" && phase != "applied" &&
         phase != "rolled-back" && phase != "aborted" && phase != "interrupted" &&
         phase != "acknowledged"))
        throw std::runtime_error("Invalid resolution journal state");
    auto metadata = item.at("conflict");
    Json files = {{item.at("original").get<std::string>(),
                   {{"sha256", metadata.at("sha256")},
                    {"size", metadata.at("bytes")},
                    {"permissions", metadata.at("permissions")}}},
                  {item.at("path").get<std::string>(),
                   {{"sha256", metadata.at("sha256")},
                    {"size", metadata.at("bytes")},
                    {"permissions", metadata.at("permissions")}}}};
    validate_snapshot({{"id", id},
                       {"project", p.at("id")},
                       {"created", item.at("created")},
                       {"reason", "resolution"},
                       {"files", files},
                       {"file_count", files.size()},
                       {"bytes", 2 * metadata.at("bytes").get<uintmax_t>()}},
                      fs::u8path(id + ".json"), p.at("id"));
    item["needs_attention"] = phase != "applied" && phase != "rolled-back" && phase != "aborted" &&
                              phase != "acknowledged";
    return item;
}
} // namespace

Json Core::resolution_plan(const Json &p, const Json &data) {
    auto mode = p.at("mode").get<std::string>();
    if (mode != "backup" && mode != "sendreceive" && mode != "sendonly" && mode != "receiveonly")
        throw std::runtime_error(
            "Conflict resolution requires a file synchronization or backup project");
    auto item = inspect_conflict(p, data.at("path"));
    auto root = fs::u8path(p.at("root").get<std::string>());
    auto current = fingerprint(project_child(root, item.at("original")));
    auto conflict = fingerprint(project_child(root, item.at("path")));
    for (const auto &[field, info] : std::vector<std::pair<std::string, Json>>{
             {"expected_current", current}, {"expected_conflict", conflict}}) {
        if (!data.contains(field))
            continue;
        const auto &expected = data.at(field);
        // JavaScript cannot round-trip filesystem timestamps beyond 2^53 as numbers.
        // Preserve the legacy numeric field for native callers and prefer exact text.
        bool same_time = expected.contains("modified_ns_exact")
                             ? expected.at("modified_ns_exact").is_string() &&
                                   expected.at("modified_ns_exact").get<std::string>() ==
                                       std::to_string(info.at("modified_ns").get<int64_t>())
                             : !expected.contains("modified_ns") ||
                                   expected.at("modified_ns") == info.at("modified_ns");
        if (expected.at("exists") != info.at("exists") ||
            expected.at("bytes") != info.at("bytes") ||
            (!expected.at("sha256").is_null() && expected.at("sha256") != info.at("sha256")) ||
            !same_time)
            throw std::runtime_error("Files changed since comparison; compare again");
    }
    auto choice = data.at("choice").get<std::string>();
    Json result;
    uintmax_t manual_bytes = 0;
    if (choice == "current")
        result = current;
    else if (choice == "conflict")
        result = conflict;
    else if (choice == "merged") {
        auto content = data.at("content").get<std::string>();
        if (content.size() > text_limit || content.find('\0') != std::string::npos)
            throw std::runtime_error(
                "Merged content must be UTF-8 text of at most 512 KiB without NUL bytes");
        (void)Json(content).dump();
        manual_bytes = content.size();
        result = {{"exists", true},
                  {"bytes", manual_bytes},
                  {"sha256", sha256(content)},
                  {"permissions", conflict.at("permissions")},
                  {"modified_ns", 0}};
    } else
        throw std::runtime_error("Choose current, conflict or merged content");
    if (result.at("exists").get<bool>())
        result["permissions"] = current.at("exists").get<bool>() ? current.at("permissions")
                                                                 : conflict.at("permissions");
    result["modified_ns"] = 0;
    Json plan = {{"project", p.at("id")},   {"root", p.at("root")},
                 {"path", item.at("path")}, {"original", item.at("original")},
                 {"choice", choice},        {"current", current},
                 {"conflict", conflict},    {"result", result}};
    plan["plan"] = sha256(Json{
        {"state", path_string(state_)},
        {"peer", registry_.at("peer_id")},
        {"input", plan}}.dump());
    auto state_bytes = current.at("bytes").get<uintmax_t>() +
                       conflict.at("bytes").get<uintmax_t>() + manual_bytes + 65536;
    auto project_bytes = result.at("bytes").get<uintmax_t>() + 65536;
    auto state_space = fs::space(state_).available, project_space = fs::space(root).available;
    plan["state_required_bytes"] = state_bytes;
    plan["project_required_bytes"] = project_bytes;
    plan["space_available"] =
        state_space >= state_bytes + project_bytes && project_space >= state_bytes + project_bytes;
    plan["requires_pause"] = !p.at("paused").get<bool>();
    return plan;
}
Json Core::resolve_conflict(const Json &p, const Json &data) {
    auto project_id = p.at("id").get<std::string>();
    if (registry_.value("resolution_pending", Json::object()).contains(project_id))
        throw std::runtime_error("Review the interrupted resolution before applying another one");
    auto plan = resolution_plan(p, data);
    if (data.value("plan", std::string{}) != plan.at("plan").get<std::string>())
        throw std::runtime_error("Resolution inputs changed or plan is missing; prepare again");
    if (!p.at("paused").get<bool>())
        throw std::runtime_error("Pause this project before applying a resolution");
    if (!plan.at("space_available").get<bool>())
        throw std::runtime_error("Insufficient free space for recovery copies and the result");
    auto root = fs::u8path(p.at("root").get<std::string>());
    auto &patterns = project(p.at("id")).at("excludes");
    if (std::find(patterns.begin(), patterns.end(), Json(".cysynck-recovery")) == patterns.end()) {
        patterns.push_back(".cysynck-recovery");
        save();
    }
    if (p.at("mode") != "backup") {
        auto folder = "cysynck-" + p.at("id").get<std::string>();
        auto config = engine_request("GET", "config/folders/" + folder);
        if (!config.at("paused").get<bool>() ||
            !fs::equivalent(root, fs::u8path(config.at("path").get<std::string>())))
            throw std::runtime_error("The matching Syncthing folder must be paused");
        auto ignores =
            engine_request("GET", "db/ignores?folder=" + folder).value("ignore", Json::array());
        if (ignores.is_null())
            ignores = Json::array();
        if (!ignores.is_array())
            throw std::runtime_error("Invalid Syncthing ignore rules");
        const std::string rule = "(?i).cysynck-recovery";
        Json safe = Json::array({rule});
        for (const auto &line : ignores)
            if (line != rule)
                safe.push_back(line);
        engine_request("POST", "db/ignores?folder=" + folder, {{"ignore", safe}});
        auto installed = engine_request("GET", "db/ignores?folder=" + folder).at("ignore");
        if (!installed.is_array() || installed.empty() || installed.at(0) != rule)
            throw std::runtime_error("Recovery directory exclusion was not installed");
    }
    auto id = random_id();
    auto directory = records_directory(state_, p, true) / id;
    if (!fs::create_directory(directory))
        throw std::runtime_error("Resolution record already exists");
    secure_state_path(directory, true);
    auto manifest = directory / "resolution.json";
    auto relative = plan.at("path").get<std::string>(),
         original = plan.at("original").get<std::string>();
    auto recovery_relative = ".cysynck-recovery/" + id;
    Json record = plan;
    record.erase("plan");
    record["schema"] = 1;
    record["id"] = id;
    record["created"] = timestamp();
    record["phase"] = "preparing";
    record["recovery_directory"] = recovery_relative;
    auto phase = [&](const std::string &value) {
        record["phase"] = value;
        atomic_write(manifest, record.dump(2), true);
    };
    fs::path recovery;
    bool published = false;
    try {
        phase("preparing");
        if (plan.at("current").at("exists").get<bool>())
            copy_private(project_child(root, original), directory / "current.bin",
                         plan.at("current"));
        copy_private(project_child(root, relative), directory / "conflict.bin",
                     plan.at("conflict"));
        if (plan.at("choice") == "merged") {
            atomic_write(directory / "merged.bin", data.at("content"), true);
            flush_file(directory / "merged.bin");
            verify_copy(directory / "merged.bin", plan.at("result"));
        }
        auto recovery_root = project_child(root, ".cysynck-recovery");
        auto owner = Json{{"schema", 1},
                          {"peer", registry_.at("peer_id")},
                          {"profile", sha256(path_string(state_))},
                          {"project", p.at("id")},
                          {"root", p.at("root")}};
        if (fs::exists(recovery_root)) {
            plain_directory(recovery_root);
            auto marker = project_child(recovery_root, "owner.json");
            if (!fs::is_regular_file(marker) || Json::parse(read_file(marker, 65536)) != owner)
                throw std::runtime_error(
                    "Recovery directory belongs to another profile or is unrecognized");
        } else {
            private_directory(recovery_root);
            atomic_write(recovery_root / "owner.json", owner.dump(), true);
        }
        recovery = project_child(root, recovery_relative);
        if (fs::exists(recovery) || !fs::create_directory(recovery))
            throw std::runtime_error("Recovery operation directory already exists");
        secure_state_path(recovery, true);
        // Test exclusive publication support on this filesystem before moving any source.
        atomic_write(recovery / "probe-a", "", true);
        publish_new(recovery / "probe-a", recovery / "probe-b");
        fs::remove(recovery / "probe-b");
        if (plan.at("result").at("exists").get<bool>()) {
            auto selected = plan.at("choice") == "merged"    ? "merged.bin"
                            : plan.at("choice") == "current" ? "current.bin"
                                                             : "conflict.bin";
            copy_private(directory / selected, recovery / "result.part", plan.at("result"));
            auto permissions_source =
                plan.at("current").at("exists").get<bool>() ? original : relative;
            source_permissions(project_child(root, permissions_source), recovery / "result.part",
                               plan.at("result").at("permissions"));
        }
        if (fingerprint(project_child(root, original)) != plan.at("current") ||
            fingerprint(project_child(root, relative)) != plan.at("conflict"))
            throw std::runtime_error("Resolution inputs changed; prepare again");
        phase("prepared");
        registry_["resolution_pending"][project_id] = id;
        save();
        if (plan.at("current").at("exists").get<bool>()) {
            phase("moving-current");
            move_source(root, original, recovery / "current", plan.at("current"));
        }
        phase("moving-conflict");
        move_source(root, relative, recovery / "conflict", plan.at("conflict"));
        phase("publishing");
        if (plan.at("result").at("exists").get<bool>()) {
            publish_new(recovery / "result.part", project_child(root, original));
            published = true;
            verify_copy(project_child(root, original), plan.at("result"));
        } else if (fs::exists(project_child(root, original)))
            throw std::runtime_error("A new original appeared; existing files were preserved");
        phase("applied");
        registry_["resolution_pending"].erase(project_id);
        save();
        event(p.at("id"), "resolution", "Conflict resolved; recovery record " + id);
        return record;
    } catch (const std::exception &e) {
        bool moved = false;
        if (!recovery.empty()) {
            for (const auto &[name, path] : std::vector<std::pair<std::string, std::string>>{
                     {"current", original}, {"conflict", relative}}) {
                try {
                    auto source = project_child(recovery, name), target = project_child(root, path);
                    if (fs::is_regular_file(source)) {
                        if (!published && !fs::exists(target))
                            publish_new(source, target);
                        else
                            moved = true;
                    }
                } catch (...) {
                    moved = true;
                }
            }
        }
        auto message = error_text(e);
        record["error"] = message;
        try {
            phase(moved || published ? "interrupted" : "rolled-back");
        } catch (...) {
        }
        try {
            if (!moved && !published && registry_.contains("resolution_pending")) {
                registry_["resolution_pending"].erase(project_id);
                save();
            }
        } catch (...) {
        }
        throw std::runtime_error(message + "; recovery record: " + id);
    }
}
Json Core::resolutions(const Json &p) {
    auto directory = records_directory(state_, p, false);
    Json list = Json::array(), errors = Json::array();
    if (fs::exists(directory))
        for (const auto &entry : fs::directory_iterator(directory)) {
            try {
                auto id = record_id({{"resolution", path_string(entry.path().filename())}});
                auto item = read_record(directory, p, id);
                if (registry_.value("resolution_pending", Json::object())
                        .value(p.at("id").get<std::string>(), "") == id)
                    item["needs_attention"] = true;
                list.push_back(item);
            } catch (const std::exception &e) {
                errors.push_back(
                    {{"path", path_string(entry.path().filename())}, {"error", error_text(e)}});
            }
        }
    std::sort(list.begin(), list.end(),
              [](const Json &a, const Json &b) { return a.at("created") > b.at("created"); });
    return {{"records", list}, {"errors", errors}};
}
Json Core::export_resolution(const Json &p, const Json &data) {
    auto id = record_id(data);
    auto directory = records_directory(state_, p, false);
    auto record = read_record(directory, p, id);
    auto target = restore_target(state_, registry_.at("projects"), data);
    directory /= id;
    std::vector<std::pair<fs::path, fs::path>> files;
    for (auto field : {"current", "conflict", "merged"}) {
        auto info = record.at(std::string(field) == "merged" ? "result" : field);
        if (!info.at("exists").get<bool>() ||
            (std::string(field) == "merged" && record.at("choice") != "merged"))
            continue;
        auto source = directory / (std::string(field) + ".bin");
        verify_copy(source, info);
        auto relative =
            std::string(field) + "/" +
            record.at(std::string(field) == "conflict" ? "path" : "original").get<std::string>();
        files.push_back({source, project_child(target, relative)});
    }
    fs::create_directories(target.parent_path());
    if (!fs::create_directory(target))
        throw std::runtime_error("Export destination already exists");
    for (const auto &[source, destination] : files) {
        fs::create_directories(destination.parent_path());
        fs::copy_file(source, destination);
        if (sha256_file(source) != sha256_file(destination))
            throw std::runtime_error("Export checksum mismatch; export incomplete");
    }
    atomic_write(target / "resolution.json", record.dump(2));
    return {{"destination", path_string(target)}, {"resolution", id}, {"files", files.size()}};
}
Json Core::acknowledge_resolution(const Json &p, const Json &data) {
    auto id = record_id(data), project_id = p.at("id").get<std::string>();
    if (!p.at("paused").get<bool>() ||
        registry_.value("resolution_pending", Json::object()).value(project_id, "") != id)
        throw std::runtime_error("Pause the project and select its interrupted resolution");
    auto directory = records_directory(state_, p, false);
    auto record = read_record(directory, p, id);
    record["phase"] = "acknowledged";
    record["needs_attention"] = false;
    record["acknowledged_at"] = timestamp();
    atomic_write(directory / id / "resolution.json", record.dump(2), true);
    registry_["resolution_pending"].erase(project_id);
    save();
    event(project_id, "resolution", "Recovery reviewed for " + id);
    return record;
}
} // namespace cysynck
