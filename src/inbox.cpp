#include "inbox.hpp"
#include "backup.hpp"
#include "error.hpp"
#include "transfers.hpp"
#include <algorithm>
#include <regex>
#include <set>
#include <stdexcept>

namespace cysynck {
namespace {
std::string identifier(const std::string &value) {
    if (!std::regex_match(value, std::regex("[A-Za-z0-9][A-Za-z0-9_-]{0,79}")))
        throw std::runtime_error("Invalid received file identifier");
    return value;
}
} // namespace
void received_name(const std::string &name) {
    if (name.empty() || name.size() > 255 || name == "." || name == ".." || name.back() == '.' ||
        name.back() == ' ' || name.find_first_of("/\\:<>\"|?*") != std::string::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; }) ||
        std::regex_match(
            name, std::regex("(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?", std::regex::icase)))
        throw std::runtime_error("Choose a portable received filename without path components");
    // JSON serialization rejects invalid UTF-8 before any payload is stored.
    (void)Json(name).dump();
}
Json received_records(const Json &registry, const std::string &project) {
    Json records = Json::object();
    // Older profiles keep their receipt only in the bounded message history.
    for (const auto &message : registry.at("messages"))
        if (message.at("project") == project && message.at("kind") == "file" &&
            message.at("direction") == "in") {
            auto record = message.at("body");
            record.erase("path");
            for (auto field : {"id", "project", "peer", "created"})
                record[field] = message.at(field);
            records[message.at("id").get<std::string>()] = record;
        }
    if (registry.contains("received_files") && registry.at("received_files").contains(project)) {
        const auto &index = registry.at("received_files").at(project);
        if (!index.is_object())
            throw std::runtime_error("Invalid received file catalog");
        for (const auto &[id, record] : index.items())
            records[id] = record;
    }
    return records;
}
Json received_record(const Json &record, const std::string &project, const std::string &id) {
    identifier(id);
    identifier(project);
    if (record.at("id") != id || record.at("project") != project ||
        !record.at("created").is_string() || record.at("created").get<std::string>().size() > 64 ||
        !record.at("bytes").is_number_integer() || record.at("bytes").get<int64_t>() < 0 ||
        record.at("bytes").get<uintmax_t>() > transfer_file_limit ||
        !std::regex_match(record.at("sha256").get<std::string>(), std::regex("[0-9a-f]{64}")))
        throw std::runtime_error("Invalid received file receipt");
    received_name(record.at("name"));
    identifier(record.at("peer"));
    // Expose only receipt fields, never a path taken from a profile file.
    return {{"id", id},
            {"project", project},
            {"peer", record.at("peer")},
            {"created", record.at("created")},
            {"name", record.at("name")},
            {"bytes", record.at("bytes")},
            {"sha256", record.at("sha256")}};
}
fs::path received_path(const fs::path &state, const std::string &project, const std::string &id) {
    auto directory = project_child(state, "inbox/" + identifier(project));
    plain_directory(directory);
    return project_child(directory, identifier(id) + ".received");
}
void verify_received(const fs::path &path, const Json &record) {
    if (path_is_link(path) || !fs::is_regular_file(path) ||
        fs::file_size(path) != record.at("bytes").get<uintmax_t>() ||
        sha256_file(path) != record.at("sha256").get<std::string>())
        throw std::runtime_error("Received payload is missing, linked or corrupted");
}
uintmax_t inbox_used(const fs::path &state) {
    auto inbox = project_child(state, "inbox");
    plain_directory(inbox);
    uintmax_t total = 0;
    if (fs::exists(inbox))
        for (const auto &directory : fs::directory_iterator(inbox)) {
            plain_directory(directory.path());
            for (const auto &entry : fs::directory_iterator(directory.path())) {
                if (path_is_link(entry.path()) || !entry.is_regular_file())
                    throw std::runtime_error("Inbox contains an unexpected entry");
                auto size = entry.file_size();
                if (size > transfer_quota || total > transfer_quota - size)
                    throw std::runtime_error("Inbox exceeds the supported quota");
                total += size;
            }
        }
    return total;
}
Json Core::direct_inbox(const Json &p, const std::string &action, const Json &data) {
    auto projectId = p.at("id").get<std::string>();
    auto records = received_records(registry_, projectId);
    if (action == "received-files") {
        Json files = Json::array(), errors = Json::array(), unindexed = Json::array();
        std::set<std::string> known;
        for (const auto &[id, raw] : records.items()) {
            try {
                auto record = received_record(raw, projectId, id);
                auto path = received_path(state_, projectId, id);
                known.insert(path_string(path.filename()));
                record["available"] = !path_is_link(path) && fs::is_regular_file(path) &&
                                      fs::file_size(path) == record.at("bytes").get<uintmax_t>();
                files.push_back(record);
            } catch (const std::exception &error) {
                errors.push_back({{"id", id}, {"error", error_text(error)}});
            }
        }
        auto directory = project_child(state_, "inbox/" + identifier(projectId));
        plain_directory(directory);
        if (fs::exists(directory))
            for (const auto &entry : fs::directory_iterator(directory))
                if (!known.contains(path_string(entry.path().filename())))
                    unindexed.push_back(path_string(entry.path().filename()));
        std::sort(files.begin(), files.end(),
                  [](const Json &a, const Json &b) { return a.at("created") > b.at("created"); });
        return {{"files", files}, {"errors", errors}, {"unindexed", unindexed}};
    }
    auto id = identifier(data.at("transfer"));
    if (!records.contains(id))
        throw std::runtime_error("Unknown received file");
    auto record = received_record(records.at(id), projectId, id);
    auto source = received_path(state_, projectId, id);
    verify_received(source, record);
    if (action == "received-verify")
        return {{"ok", true}, {"file", record}, {"checked_at", timestamp()}};
    if (action != "received-export")
        throw std::runtime_error("Unknown received file operation");
    auto target = restore_target(state_, registry_.at("projects"), data);
    auto payload = project_child(target, "payload/" + record.at("name").get<std::string>());
    fs::create_directories(target.parent_path());
    if (!fs::create_directory(target))
        throw std::runtime_error("Export destination already exists");
    secure_state_path(target, true);
    if (!fs::create_directory(target / "payload"))
        throw std::runtime_error("Export payload directory already exists");
    fs::copy_file(source, payload, fs::copy_options::none);
    secure_state_path(payload, false);
    verify_received(payload, record);
    atomic_write(target / "receipt.json", record.dump(2), true);
    event(projectId, "transfer", "Received file exported: " + id);
    return {{"exported", true},
            {"destination", path_string(target)},
            {"path", path_string(payload)},
            {"file", record}};
}
} // namespace cysynck
