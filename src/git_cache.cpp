#include "git_cache.hpp"
#include "backup.hpp"
#include "file_time.hpp"
#include "git_lfs.hpp"
#include "git_review.hpp"
#include <algorithm>
#include <regex>
#include <set>

namespace cysynck {
namespace {
bool hex(const std::string &value, size_t count) {
    return value.size() == count && value.find_first_not_of("0123456789abcdef") == value.npos;
}
std::string identifier(const Json &value) {
    auto text = value.get<std::string>();
    if (!std::regex_match(text, std::regex("[A-Za-z0-9][A-Za-z0-9_-]{0,79}")))
        throw std::runtime_error("Invalid LFS cache peer identifier");
    return text;
}
size_t bound(const Json &data, const char *key, size_t fallback, size_t maximum) {
    if (!data.contains(key))
        return fallback;
    const auto &value = data.at(key);
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0) ||
        value.get<uint64_t>() > maximum || (std::string(key) == "limit" && value == 0))
        throw std::runtime_error("Invalid LFS cache page bound");
    return value.get<size_t>();
}
Json context(const Json &p, const Json &data) {
    if (p.at("mode") != "git-bundles")
        throw std::runtime_error("LFS cache inspection requires git-bundles mode");
    require_default_git_environment();
    for (auto key : {"GIT_CONFIG_PARAMETERS", "GIT_CONFIG_COUNT", "GIT_ATTR_SOURCE", "GIT_CONFIG",
                     "GIT_CONFIG_GLOBAL", "GIT_CONFIG_SYSTEM"})
        if (!env(key).empty())
            throw std::runtime_error(
                "LFS cache inspection requires Git configuration overrides to be unset");
    if (data.contains("peer") || data.contains("branch"))
        return git_revision_context(p, data);
    auto local = git_local_head(p);
    if (local.at("base").is_null())
        throw std::runtime_error(
            "Commit locally or select an imported branch before inspecting LFS cache");
    local["head"] = local.at("base");
    return local;
}
Json identity(const fs::path &path) {
    if (path_is_link(path) || !fs::is_regular_file(path))
        throw std::runtime_error("LFS cache path is linked or not a regular file");
    return {{"bytes", fs::file_size(path)},
            {"modified", std::to_string(file_time_ticks(fs::last_write_time(path)))},
            {"permissions", static_cast<unsigned>(fs::status(path).permissions())}};
}
Json status(const fs::path &store, const Json &object, bool verify) {
    Json result{{"state", "missing"}, {"identity", nullptr}};
    try {
        auto path = lfs_object_path(store, object.at("sha256"));
        if (path_is_link(path))
            throw std::runtime_error("LFS cache links are preserved and refused");
        if (!fs::exists(path))
            return result;
        auto before = identity(path);
        result["identity"] = before;
        if (before.at("bytes") != object.at("size"))
            result["state"] = "size_mismatch";
        else if (!verify)
            result["state"] = "present";
        else
            result["state"] = sha256_file(path) == object.at("sha256").get<std::string>()
                                  ? "verified"
                                  : "corrupt";
        if (identity(path) != before)
            throw std::runtime_error("LFS cache object changed during inspection");
    } catch (const std::exception &e) {
        result["state"] = "unsafe";
        result["error"] = e.what();
    }
    return result;
}
fs::path exchange_peers(const Json &p) {
    auto root = fs::u8path(p.at("exchange_path").get<std::string>());
    fs::path parent;
    for (const auto &part : fs::absolute(root)) {
        parent /= part;
        plain_directory(parent);
    }
    return project_child(root, "peers");
}
Json source_peers(const Json &p) {
    auto peers = exchange_peers(p);
    plain_directory(peers);
    std::set<std::string> names;
    size_t visited = 0;
    if (fs::exists(peers))
        for (const auto &entry : fs::directory_iterator(peers)) {
            if (++visited > 1000)
                throw std::runtime_error("LFS source catalog exceeds 1000 entries");
            if (path_is_link(entry.path()) || !entry.is_directory())
                continue;
            auto name = path_string(entry.path().filename());
            try {
                names.insert(identifier(Json(name)));
            } catch (const std::exception &) {
            }
        }
    return names;
}
struct Inspection {
    Json result, rows, revision;
    fs::path store;
};
Inspection inspect(const fs::path &state, const Json &p, const Json &data, bool verify) {
    auto revision = context(p, data);
    auto inventory = lfs_inventory(p, revision.at("head"));
    auto store = lfs_local_store(p);
    auto config_hash = sha256(offline_git(p, {"config", "--null", "--list"}));
    Json rows = Json::array();
    size_t missing = 0, present = 0, damaged = 0, verified = 0;
    for (auto object : inventory.at("objects")) {
        auto info = status(store, object, verify);
        object.update(info);
        rows.push_back(object);
        auto value = info.at("state").get<std::string>();
        if (value == "missing")
            ++missing;
        else if (value == "present" || value == "verified") {
            ++present;
            if (value == "verified")
                ++verified;
        } else
            ++damaged;
    }
    if (context(p, data) != revision || lfs_local_store(p) != store ||
        sha256(offline_git(p, {"config", "--null", "--list"})) != config_hash)
        throw std::runtime_error("Git revision or LFS configuration changed; inspect again");
    auto hash = sha256(Json{
        {"state", path_string(fs::absolute(state))},
        {"project", p},
        {"revision", revision},
        {"store", path_string(store)},
        {"config", config_hash},
        {"inventory", inventory},
        {"rows", rows},
        {"verify", verify}}.dump());
    return {{{"revision", revision.at("head")},
             {"context", revision},
             {"scope", "branch-history"},
             {"store", path_string(store)},
             {"config_hash", config_hash},
             {"total", rows.size()},
             {"bytes", inventory.at("bytes")},
             {"missing", missing},
             {"present", present},
             {"damaged", damaged},
             {"verified", verified},
             {"verification_requested", verify},
             {"content_verified", verify && missing == 0 && damaged == 0},
             {"inventory_hash", hash},
             {"source_peers", source_peers(p)}},
            rows,
            revision,
            store};
}
Json recovery_preview(const fs::path &state, const Json &p, const Json &data) {
    auto scanned = inspect(state, p, data, false);
    auto peer = identifier(data.at("source_peer"));
    auto source = project_child(exchange_peers(p), peer + "/lfs/objects");
    plain_directory(source);
    std::set<std::string> selection;
    if (data.contains("objects")) {
        const auto &values = data.at("objects");
        if (!values.is_array() || values.empty() || values.size() > 4096)
            throw std::runtime_error("Select 1 to 4096 committed LFS objects");
        for (const auto &value : values) {
            auto id = value.get<std::string>();
            if (!hex(id, 64) || !selection.insert(id).second)
                throw std::runtime_error("Invalid or duplicate LFS cache object selection");
        }
    }
    Json eligible = Json::array(), unavailable = Json::array(), observed = Json::array();
    uintmax_t bytes = 0;
    size_t preserved = 0;
    for (const auto &row : scanned.rows) {
        auto digest = row.at("sha256").get<std::string>();
        if (data.contains("objects") && !selection.erase(digest))
            continue;
        if (row.at("state") != "missing") {
            ++preserved;
            continue;
        }
        auto info = status(source, row, true);
        Json object{{"sha256", digest}, {"size", row.at("size")}, {"source", info}};
        observed.push_back(object);
        if (info.at("state") == "verified") {
            eligible.push_back(object);
            bytes += row.at("size").get<uintmax_t>();
        } else
            unavailable.push_back(object);
    }
    if (!selection.empty())
        throw std::runtime_error("LFS selection includes an object outside the committed history");
    Json result{{"project", p.at("id")},
                {"root", p.at("root")},
                {"state", path_string(fs::absolute(state))},
                {"source_peer", peer},
                {"source_store", path_string(source)},
                {"cache", scanned.result},
                {"objects", eligible},
                {"unavailable", unavailable},
                {"preserved", preserved},
                {"bytes", bytes},
                {"can_recover", !eligible.empty()},
                {"applied", false},
                {"plan", nullptr}};
    auto token = result;
    token["observed"] = observed;
    token["selection"] = data.value("objects", Json(nullptr));
    if (!eligible.empty())
        result["plan"] = sha256(token.dump());
    if (result.dump().size() > 8 * 1024 * 1024)
        throw std::runtime_error("LFS recovery preview exceeds 8 MiB");
    return result;
}
fs::path receipt_file(const fs::path &state, const Json &p, const std::string &id) {
    if (!hex(id, 32))
        throw std::runtime_error("Invalid LFS recovery receipt identifier");
    return project_child(state, "git-lfs-recoveries/" + p.at("id").get<std::string>() + "/" + id +
                                    ".json");
}
Json read_receipt(const fs::path &state, const Json &p, const std::string &id) {
    auto record = Json::parse(read_file(receipt_file(state, p, id), 16 * 1024 * 1024));
    if (!record.at("version").is_number_integer() || record.at("version") != 1 ||
        record.at("id") != id || record.at("project") != p.at("id") ||
        record.at("root") != p.at("root") ||
        record.at("state") != path_string(fs::absolute(state)) ||
        !record.at("created").is_string() ||
        (record.at("status") != "complete" && record.at("status") != "incomplete") ||
        !record.at("installed").is_array() || record.at("installed").size() > 4096 ||
        !record.at("objects").is_array() || record.at("objects").empty() ||
        record.at("objects").size() > 4096)
        throw std::runtime_error("Invalid LFS recovery receipt identity or schema");
    (void)identifier(record.at("source_peer"));
    if (!record.at("applied").is_boolean() || record.at("created").get<std::string>().size() > 64 ||
        !record.at("bytes").is_number_unsigned())
        throw std::runtime_error("Invalid LFS recovery receipt metadata");
    std::set<std::string> planned, installed;
    uintmax_t bytes = 0;
    for (const auto &object : record.at("objects")) {
        auto digest = object.at("sha256").get<std::string>();
        if (!hex(digest, 64) || !planned.insert(digest).second ||
            !object.at("size").is_number_unsigned() ||
            object.at("size").get<uintmax_t>() > 4ull * 1024 * 1024 * 1024)
            throw std::runtime_error("Invalid LFS recovery receipt object");
        bytes += object.at("size").get<uintmax_t>();
    }
    if (bytes > 16ull * 1024 * 1024 * 1024 || record.at("bytes") != bytes)
        throw std::runtime_error("Invalid LFS recovery receipt byte count");
    for (const auto &digest : record.at("installed")) {
        auto value = digest.get<std::string>();
        if (!planned.contains(value) || !installed.insert(value).second)
            throw std::runtime_error("Invalid installed LFS receipt identity");
    }
    if ((record.at("status") == "complete") != record.at("applied").get<bool>() ||
        (record.at("status") == "complete" && installed != planned))
        throw std::runtime_error("Inconsistent LFS recovery receipt completion");
    return record;
}
Json summary(const Json &record) {
    return {{"id", record.at("id")},
            {"created", record.at("created")},
            {"status", record.at("status")},
            {"source_peer", record.at("source_peer")},
            {"planned", record.at("objects").size()},
            {"installed", record.at("installed").size()},
            {"bytes", record.at("bytes")},
            {"error", record.value("error", Json(nullptr))}};
}
} // namespace
Json inspect_git_lfs_cache(const fs::path &state, const Json &p, const Json &data) {
    if (!data.is_object() || (data.contains("verify") && !data.at("verify").is_boolean()))
        throw std::runtime_error("LFS cache options require an object and Boolean verify");
    auto offset = bound(data, "offset", 0, 4096), limit = bound(data, "limit", 100, 200);
    auto scanned = inspect(state, p, data, data.value("verify", false));
    if (data.contains("inventory_hash") &&
        data.at("inventory_hash") != scanned.result.at("inventory_hash"))
        throw std::runtime_error("LFS cache inventory changed; load the first page again");
    auto result = scanned.result;
    result.update({{"offset", offset}, {"limit", limit}, {"objects", Json::array()}});
    auto end = std::min(scanned.rows.size(), offset + limit);
    for (size_t i = offset; i < end; ++i)
        result["objects"].push_back(scanned.rows.at(i));
    result["has_more"] = end < scanned.rows.size();
    return result;
}
Json recover_git_lfs_cache(const fs::path &state, const Json &p, const Json &data) {
    if (!data.is_object() || (data.contains("apply") && !data.at("apply").is_boolean()))
        throw std::runtime_error("LFS recovery options require an object and Boolean apply");
    auto preview = recovery_preview(state, p, data);
    if (!data.value("apply", false))
        return preview;
    if (!preview.at("can_recover").get<bool>() || !data.contains("plan") ||
        data.at("plan") != preview.at("plan"))
        throw std::runtime_error("LFS recovery is blocked or changed; preview again");
    auto store = lfs_local_store(p);
    plain_directory(store.parent_path());
    fs::create_directories(store.parent_path());
    DiskLock cache_lock(store.parent_path(), "cysynck-cache.lock", false);
    if (recovery_preview(state, p, data).at("plan") != preview.at("plan"))
        throw std::runtime_error(
            "LFS recovery changed while acquiring the cache lock; preview again");
    auto id = random_id();
    auto path = receipt_file(state, p, id);
    if (fs::exists(path.parent_path())) {
        size_t count = 0;
        for (const auto &entry : fs::directory_iterator(path.parent_path()))
            if (++count >= 10000)
                throw std::runtime_error("LFS recovery receipt catalog is full");
    }
    Json record = preview;
    record.update({{"version", 1},
                   {"id", id},
                   {"created", timestamp()},
                   {"status", "incomplete"},
                   {"installed", Json::array()},
                   {"plan", nullptr}});
    atomic_write(path, record.dump(2), true);
    try {
        auto source = fs::u8path(preview.at("source_store").get<std::string>());
        for (const auto &object : preview.at("objects")) {
            copy_lfs_object(lfs_object_path(source, object.at("sha256")), store, object);
            record["installed"].push_back(object.at("sha256"));
        }
        if (context(p, data) != preview.at("cache").at("context") ||
            path_string(lfs_local_store(p)) != preview.at("cache").at("store").get<std::string>() ||
            sha256(offline_git(p, {"config", "--null", "--list"})) !=
                preview.at("cache").at("config_hash").get<std::string>())
            throw std::runtime_error("Git revision or LFS storage changed during recovery");
        record.update({{"status", "complete"}, {"applied", true}, {"can_recover", false}});
        atomic_write(path, record.dump(2), true);
    } catch (const std::exception &e) {
        record.update({{"status", "incomplete"}, {"applied", false}, {"can_recover", false}});
        record["error"] = e.what();
        try {
            atomic_write(path, record.dump(2), true);
        } catch (const std::exception &) {
        }
        throw std::runtime_error("LFS cache recovery incomplete; inspect receipt " + id + ": " +
                                 e.what());
    }
    return record;
}
Json git_lfs_recoveries(const fs::path &state, const Json &p, const Json &data, bool detail) {
    if (p.at("mode") != "git-bundles")
        throw std::runtime_error("LFS recovery history requires git-bundles mode");
    if (detail)
        return read_receipt(state, p, data.at("receipt"));
    auto offset = bound(data, "offset", 0, 10000), limit = bound(data, "limit", 20, 100);
    auto directory = project_child(state, "git-lfs-recoveries/" + p.at("id").get<std::string>());
    plain_directory(directory);
    Json records = Json::array(), errors = Json::array();
    size_t count = 0;
    if (fs::exists(directory))
        for (const auto &entry : fs::directory_iterator(directory)) {
            if (++count > 10000)
                throw std::runtime_error("LFS recovery catalog exceeds 10000 entries");
            auto id = path_string(entry.path().stem());
            try {
                if (entry.path().extension() == ".json")
                    records.push_back(summary(read_receipt(state, p, id)));
            } catch (const std::exception &e) {
                errors.push_back(
                    {{"file", path_string(entry.path().filename())}, {"error", e.what()}});
            }
        }
    std::sort(records.begin(), records.end(), [](const Json &a, const Json &b) {
        return a.at("created") == b.at("created") ? a.at("id") > b.at("id")
                                                  : a.at("created") > b.at("created");
    });
    Json page = Json::array();
    auto end = std::min(records.size(), offset + limit);
    for (size_t i = offset; i < end; ++i)
        page.push_back(records.at(i));
    return {{"total", records.size()},          {"offset", offset}, {"limit", limit},
            {"has_more", end < records.size()}, {"records", page},  {"errors", errors}};
}
} // namespace cysynck
