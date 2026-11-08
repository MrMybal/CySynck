#include "transfers.hpp"
#include "linux_direct_socket.hpp"
#include "windows_direct_socket.hpp"
#include "macos_direct_socket.hpp"
#include "backup.hpp"
#include "inbox.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <httplib.h>
#include <picosha2.h>
#include <regex>
#include <stdexcept>
#include <thread>

namespace cysynck {
namespace {
std::string identifier(const Json &value) {
    auto id = value.get<std::string>();
    if (!std::regex_match(id, std::regex("[A-Za-z0-9][A-Za-z0-9_-]{0,79}")))
        throw std::runtime_error("Invalid transfer identifier");
    return id;
}
uintmax_t chunk_count(const Json &record) {
    return (record.at("bytes").get<uintmax_t>() + transfer_chunk_size - 1) / transfer_chunk_size;
}
uintmax_t acknowledged(const Json &record, uintmax_t next) {
    return std::min(record.at("bytes").get<uintmax_t>(), next * transfer_chunk_size);
}
Json metadata(const Json &data) {
    if (data.at("protocol") != 1)
        throw std::runtime_error("Unsupported transfer protocol");
    auto copy = data;
    copy["created"] = data.value("created", timestamp());
    auto result = received_record(copy, identifier(data.at("project")), identifier(data.at("id")));
    result["protocol"] = 1;
    return result;
}
bool same_file(const Json &a, const Json &b) {
    for (auto field : {"id", "project", "peer", "name", "bytes", "sha256"})
        if (a.at(field) != b.at(field))
            return false;
    return true;
}
Json incoming_record(const Json &raw) {
    auto record = metadata(raw);
    const auto &chunks = raw.at("chunks");
    if (!chunks.is_array() || chunks.size() > chunk_count(record))
        throw std::runtime_error("Invalid transfer progress");
    for (const auto &hash : chunks)
        if (!hash.is_string() || hash.get_ref<const std::string &>().size() != 64 ||
            !std::all_of(hash.get_ref<const std::string &>().begin(),
                         hash.get_ref<const std::string &>().end(),
                         [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
            throw std::runtime_error("Invalid transfer chunk digest");
    record["chunks"] = chunks;
    return record;
}
Json outgoing_record(const Json &raw) {
    auto record = metadata(raw);
    if (!raw.at("next").is_number_integer() || raw.at("next").get<int64_t>() < 0 ||
        raw.at("next").get<uintmax_t>() > chunk_count(record) ||
        !raw.at("delivered").is_boolean() || !raw.at("error").is_string() ||
        raw.at("error").get<std::string>().size() > 256 ||
        (raw.at("delivered").get<bool>() && raw.at("next").get<uintmax_t>() != chunk_count(record)))
        throw std::runtime_error("Invalid outgoing transfer journal");
    for (const auto &key : {"next", "delivered", "error"})
        record[key] = raw.at(key);
    auto code = raw.value("error_code", "");
    if (code != "" && code != "interrupted" && code != "unreachable" &&
        code != "unsupported_protocol" && code != "peer_refused")
        throw std::runtime_error("Invalid transfer error code");
    record["error_code"] = code;
    return record;
}
fs::path transfer_directory(const fs::path &state, const Json &record, bool outgoing) {
    auto root = project_child(state, "transfers");
    plain_directory(root);
    auto direction = project_child(root, outgoing ? "outgoing" : "incoming");
    plain_directory(direction);
    auto project = project_child(direction, identifier(record.at("project")));
    plain_directory(project);
    auto directory = project_child(project, identifier(record.at("id")));
    plain_directory(directory);
    return directory;
}
void private_directory(const fs::path &directory) {
    plain_directory(directory);
    fs::create_directories(directory);
    secure_state_path(directory, true);
}
fs::path block_path(const fs::path &directory, uintmax_t index) {
    return project_child(directory, std::to_string(index) + ".block");
}
std::string block_bytes(const fs::path &directory, const Json &record, uintmax_t index) {
    auto path = block_path(directory, index);
    if (path_is_link(path) || !fs::is_regular_file(path))
        throw std::runtime_error("Transfer chunk is missing or linked");
    auto bytes = read_file(path, transfer_chunk_size);
    auto expected = acknowledged(record, index + 1) - acknowledged(record, index);
    if (bytes.size() != expected || record.at("chunks").at(index) != sha256(bytes))
        throw std::runtime_error("Stored transfer chunk is corrupted");
    return bytes;
}
void verify_blocks(const fs::path &directory, const Json &record) {
    for (size_t i = 0; i < record.at("chunks").size(); ++i)
        (void)block_bytes(directory, record, i);
}
// Delete only this protocol's exact filenames; never recurse into unexpected content.
void remove_payloads(const fs::path &directory, const Json &record, bool outgoing) {
    if (!fs::exists(directory))
        return;
    for (const auto &entry : fs::directory_iterator(directory)) {
        auto name = path_string(entry.path().filename());
        bool owned = outgoing ? name == "snapshot" : name == "assembled";
        if (!outgoing && name.ends_with(".block")) {
            auto digits = name.substr(0, name.size() - 6);
            uintmax_t index = 0;
            auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), index);
            owned = parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size() &&
                    digits == std::to_string(index) && index < chunk_count(record);
        }
        if (!owned || path_is_link(entry.path()) || !entry.is_regular_file())
            throw std::runtime_error(
                "Transfer directory contains unexpected content; review it locally");
    }
    for (const auto &entry : fs::directory_iterator(directory))
        fs::remove(entry.path());
    // Windows may keep a just-deleted block visible while filesystem filters finish with it.
    // Retry only the empty-directory removal, never delete additional or unexpected content.
    std::error_code error;
    for (int attempt = 0; attempt < 20; ++attempt) {
        fs::remove(directory, error);
        if (!error)
            return;
        if (error != std::errc::directory_not_empty && error != std::errc::permission_denied)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw fs::filesystem_error("Cannot remove empty transfer directory", directory, error);
}
Json progress(const Json &record, uintmax_t next, bool delivered = false) {
    return {{"id", record.at("id")},
            {"protocol", 1},
            {"next", next},
            {"acknowledged_bytes", acknowledged(record, next)},
            {"delivered", delivered}};
}
size_t pending_count(const Json &registry, const std::string &table) {
    size_t count = 0;
    for (const auto &project : registry.value(table, Json::object()))
        for (const auto &record : project)
            if (table == "transfers_in" || !record.value("delivered", false))
                ++count;
    return count;
}
Json freeze_file(const fs::path &source, const fs::path &snapshot, uintmax_t limit) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (path_is_link(source) || !fs::is_regular_file(source))
            throw std::runtime_error("Transfer source must be a regular file");
        auto size = fs::file_size(source);
        auto modified = fs::last_write_time(source);
        if (size > limit)
            throw std::runtime_error("Source exceeds the configured file or outgoing quota");
        atomic_write(snapshot, "", true);
        std::ifstream input(source, std::ios::binary);
        std::ofstream output(snapshot, std::ios::binary | std::ios::app);
        if (!input || !output)
            throw std::runtime_error("Cannot create outgoing snapshot");
        picosha2::hash256_one_by_one digest;
        std::array<char, 65536> buffer;
        uintmax_t copied = 0;
        while (input) {
            input.read(buffer.data(), buffer.size());
            auto count = static_cast<size_t>(input.gcount());
            if (count > limit - copied)
                throw std::runtime_error("Source grew beyond the outgoing transfer limit");
            output.write(buffer.data(), static_cast<std::streamsize>(count));
            if (!output)
                throw std::runtime_error("Cannot write outgoing snapshot");
            digest.process(buffer.data(), buffer.data() + count);
            copied += count;
        }
        output.flush();
        if (!input.eof() || !output)
            throw std::runtime_error("Outgoing snapshot capture failed");
        output.close();
        if (!output)
            throw std::runtime_error("Cannot close outgoing snapshot");
        if (!path_is_link(source) && fs::is_regular_file(source) && copied == size &&
            fs::file_size(source) == size && fs::last_write_time(source) == modified) {
            digest.finish();
            return {{"bytes", copied}, {"sha256", picosha2::get_hash_hex_string(digest)}};
        }
    }
    throw std::runtime_error("Transfer source kept changing; retry when it is stable");
}
Json usage(const fs::path &state, const Json &registry) {
    auto result = transfer_limits(registry);
    result["inbox_bytes"] = inbox_used(state);
    result["reserved_incoming_bytes"] = transfer_reserved(registry);
    result["reserved_outgoing_bytes"] = transfer_reserved(registry, true);
    result["max_file_limit_mib"] = transfer_file_limit / transfer_chunk_size;
    result["max_quota_mib"] = transfer_quota / transfer_chunk_size;
    return result;
}
} // namespace

Json transfer_limits(const Json &registry) {
    auto raw = registry.value("transfer_limits", Json::object());
    if (!raw.is_object())
        throw std::runtime_error("Invalid transfer limits");
    Json result = {{"file_limit_mib", 1024}, {"inbox_quota_mib", 4096}, {"outbox_quota_mib", 4096}};
    for (auto &[key, value] : result.items()) {
        value = raw.value(key, value);
        auto maximum =
            (key == "file_limit_mib" ? transfer_file_limit : transfer_quota) / transfer_chunk_size;
        if (!value.is_number_integer() || value.get<int64_t>() < 1 ||
            value.get<uintmax_t>() > maximum)
            throw std::runtime_error(
                "Transfer limits require whole MiB: file 1..4096, quotas 1..65536");
    }
    if (result.at("file_limit_mib") > result.at("inbox_quota_mib") ||
        result.at("file_limit_mib") > result.at("outbox_quota_mib"))
        throw std::runtime_error("Each transfer quota must cover the file limit");
    return result;
}
uintmax_t transfer_reserved(const Json &registry, bool outgoing) {
    uintmax_t reserved = 0;
    for (const auto &project :
         registry.value(outgoing ? "transfers_out" : "transfers_in", Json::object()))
        for (const auto &raw : project) {
            auto record = outgoing ? outgoing_record(raw) : incoming_record(raw);
            if (outgoing && record.at("delivered").get<bool>())
                continue;
            auto size = record.at("bytes").get<uintmax_t>();
            if (size > transfer_quota || reserved > transfer_quota - size)
                throw std::runtime_error("Pending transfer quota exceeded");
            reserved += size;
        }
    return reserved;
}
void check_incoming_capacity(const fs::path &state, const Json &registry, uintmax_t bytes) {
    auto limits = transfer_limits(registry);
    if (bytes > limits.at("file_limit_mib").get<uintmax_t>() * transfer_chunk_size)
        throw std::runtime_error("File exceeds the configured transfer limit");
    auto quota = limits.at("inbox_quota_mib").get<uintmax_t>() * transfer_chunk_size;
    auto used = inbox_used(state), reserved = transfer_reserved(registry);
    if (used > quota || reserved > quota - used || bytes > quota - used - reserved)
        throw std::runtime_error("Received and pending files exceed the configured inbox quota");
}

Json Core::transfer_receive(const Json &data, const std::string &bytes) {
    // direct_receive holds both locks and has checked the project, trust and VPN source address.
    auto projectId = identifier(data.at("project")), id = identifier(data.at("id"));
    auto action = data.at("transfer_action").get<std::string>();
    auto receipts = received_records(registry_, projectId);
    auto &table = registry_["transfers_in"][projectId];
    if (table.is_null())
        table = Json::object();
    if (receipts.contains(id)) {
        auto receipt = received_record(receipts.at(id), projectId, id);
        if (receipt.at("peer") != data.at("peer") ||
            (action == "begin" && !same_file(receipt, metadata(data))))
            throw std::runtime_error("Transfer ID was already used with different content");
        if (action != "begin" && action != "finish")
            throw std::runtime_error("Transfer already delivered");
        verify_received(received_path(state_, projectId, id), receipt);
        if (table.contains(id)) {
            auto old = incoming_record(table.at(id));
            if (old.at("project") != projectId || old.at("id") != id)
                throw std::runtime_error("Invalid transfer catalog identity");
            try {
                remove_payloads(transfer_directory(state_, old, false), old, false);
                table.erase(id);
                save();
            } catch (const std::exception &) {
                auto result = progress(receipt, chunk_count(receipt), true);
                result["cleanup_pending"] = true;
                return result;
            }
        }
        return progress(receipt, chunk_count(receipt), true);
    }
    if (action == "begin") {
        auto record = metadata(data);
        if (table.contains(id)) {
            auto existing = incoming_record(table.at(id));
            if (!same_file(record, existing))
                throw std::runtime_error("Transfer ID was already used with different content");
            verify_blocks(transfer_directory(state_, existing, false), existing);
            return progress(existing, existing.at("chunks").size());
        }
        for (const auto &message : registry_.at("messages"))
            if (message.at("id") == id)
                throw std::runtime_error("Duplicate message ID");
        if (pending_count(registry_, "transfers_in") >= 32 ||
            receipts.size() + table.size() >= 10000)
            throw std::runtime_error("Incoming transfer catalog limit reached");
        check_incoming_capacity(state_, registry_, record.at("bytes").get<uintmax_t>());
        if (fs::exists(received_path(state_, projectId, id)))
            throw std::runtime_error("Transfer ID already exists in inbox");
        auto directory = transfer_directory(state_, record, false);
        if (fs::exists(directory))
            throw std::runtime_error("Unindexed transfer directory requires local review");
        private_directory(directory);
        record["chunks"] = Json::array();
        table[id] = record;
        save();
        return progress(record, 0);
    }
    if (!table.contains(id))
        throw std::runtime_error("Unknown pending transfer; begin first");
    auto record = incoming_record(table.at(id));
    if (record.at("project") != projectId || record.at("id") != id)
        throw std::runtime_error("Invalid transfer catalog identity");
    if (record.at("peer") != data.at("peer"))
        throw std::runtime_error("Transfer belongs to another peer");
    auto directory = transfer_directory(state_, record, false);
    auto next = record.at("chunks").size();
    if (action == "chunk") {
        const auto &rawIndex = data.at("index");
        if (!rawIndex.is_number_integer() || rawIndex.get<int64_t>() < 0 ||
            rawIndex.get<uintmax_t>() >= chunk_count(record))
            throw std::runtime_error("Invalid chunk index");
        auto index = rawIndex.get<uintmax_t>();
        if (index > next ||
            bytes.size() != acknowledged(record, index + 1) - acknowledged(record, index) ||
            data.at("chunk_sha256") != sha256(bytes))
            throw std::runtime_error("Out-of-order chunk, invalid size or checksum mismatch");
        if (index < next) {
            if (record.at("chunks").at(index) != data.at("chunk_sha256"))
                throw std::runtime_error("Chunk was already accepted with different content");
            (void)block_bytes(directory, record, index);
            return progress(record, next);
        }
        auto path = block_path(directory, index);
        if (path_is_link(path) || (fs::exists(path) && !fs::is_regular_file(path)))
            throw std::runtime_error("Transfer chunk path is not a regular file");
        // The block precedes its journal entry. A retry safely replaces a block left by a crash.
        atomic_write(path, bytes, true);
        table[id]["chunks"].push_back(data.at("chunk_sha256"));
        save();
        return progress(record, next + 1);
    }
    if (action != "finish" || next != chunk_count(record))
        throw std::runtime_error("Transfer is incomplete or action is unknown");
    auto destination = received_path(state_, projectId, id);
    if (fs::exists(destination))
        verify_received(destination, record); // Retry after publish, before receipt was saved.
    else {
        auto assembled = project_child(directory, "assembled");
        if (path_is_link(assembled) || (fs::exists(assembled) && !fs::is_regular_file(assembled)))
            throw std::runtime_error("Transfer assembly path is not a regular file");
        atomic_write(assembled, "", true);
        std::ofstream output(assembled, std::ios::binary | std::ios::app);
        for (size_t i = 0; i < next; ++i) {
            auto block = block_bytes(directory, record, i);
            output.write(block.data(), static_cast<std::streamsize>(block.size()));
        }
        output.flush();
        if (!output)
            throw std::runtime_error("Cannot assemble received transfer");
        output.close();
        if (!output)
            throw std::runtime_error("Cannot close assembled transfer");
        verify_received(assembled, record);
        private_directory(destination.parent_path());
        fs::rename(assembled, destination);
    }
    auto receipt = received_record(record, projectId, id);
    receipts[id] = receipt;
    registry_["received_files"][projectId] = receipts;
    registry_["messages"].push_back({{"id", id},
                                     {"created", receipt.at("created")},
                                     {"project", projectId},
                                     {"peer", receipt.at("peer")},
                                     {"direction", "in"},
                                     {"kind", "file"},
                                     {"body",
                                      {{"name", receipt.at("name")},
                                       {"bytes", receipt.at("bytes")},
                                       {"sha256", receipt.at("sha256")}}}});
    if (registry_["messages"].size() > 2000)
        registry_["messages"].erase(registry_["messages"].begin());
    save(); // Publish the durable receipt before removing any resumable blocks.
    try {
        remove_payloads(directory, record, false);
        table.erase(id);
        save();
    } catch (const std::exception &) {
        auto result = progress(record, next, true);
        result["cleanup_pending"] = true;
        return result;
    }
    return progress(record, next, true);
}

Json Core::transfer_action(const std::string &action, const Json &data) {
    if (action == "transfer-limits")
        return usage(state_, registry_);
    if (action == "transfer-configure") {
        if (!data.is_object() || data.empty())
            throw std::runtime_error("Provide at least one transfer limit");
        auto proposed = transfer_limits(registry_);
        for (const auto &[key, value] : data.items()) {
            if (!proposed.contains(key))
                throw std::runtime_error("Unknown transfer limit");
            proposed[key] = value;
        }
        auto checked = transfer_limits(Json{{"transfer_limits", proposed}});
        auto current = usage(state_, registry_);
        auto quotaIn = checked.at("inbox_quota_mib").get<uintmax_t>() * transfer_chunk_size;
        auto quotaOut = checked.at("outbox_quota_mib").get<uintmax_t>() * transfer_chunk_size;
        auto used = current.at("inbox_bytes").get<uintmax_t>();
        auto incoming = current.at("reserved_incoming_bytes").get<uintmax_t>();
        if (used > quotaIn || incoming > quotaIn - used ||
            current.at("reserved_outgoing_bytes").get<uintmax_t>() > quotaOut)
            throw std::runtime_error(
                "Transfer quotas cannot be lower than current usage and reservations");
        registry_["transfer_limits"] = checked;
        return usage(state_, registry_);
    }
    auto projectId = identifier(data.at("project"));
    project(projectId);
    if (action == "transfers") {
        Json files = Json::array(), errors = Json::array();
        for (bool outgoing : {false, true}) {
            auto table =
                registry_.value(outgoing ? "transfers_out" : "transfers_in", Json::object());
            if (!table.contains(projectId))
                continue;
            for (const auto &[id, raw] : table.at(projectId).items()) {
                try {
                    auto record = outgoing ? outgoing_record(raw) : incoming_record(raw);
                    if (record.at("project") != projectId || record.at("id") != id)
                        throw std::runtime_error("Invalid transfer catalog identity");
                    auto next =
                        outgoing ? raw.at("next").get<uintmax_t>() : record.at("chunks").size();
                    if (next > chunk_count(record))
                        throw std::runtime_error("Invalid transfer progress");
                    record.erase("chunks");
                    record["direction"] = outgoing ? "out" : "in";
                    record["acknowledged_bytes"] = acknowledged(record, next);
                    record["delivered"] = outgoing
                                              ? raw.value("delivered", false)
                                              : received_records(registry_, projectId).contains(id);
                    if (outgoing)
                        record["error"] = raw.value("error", "");
                    files.push_back(record);
                } catch (const std::exception &) {
                    errors.push_back(
                        {{"id", id},
                         {"error", "Invalid transfer journal; review the local profile"}});
                }
            }
        }
        return {{"files", files}, {"errors", errors}, {"chunk_bytes", transfer_chunk_size}};
    }
    DiskLock lease(state_, "transfers-send.lock", false);
    auto id = identifier(data.at("transfer"));
    auto direction = data.at("direction").get<std::string>();
    if (direction != "in" && direction != "out")
        throw std::runtime_error("Choose transfer direction in or out");
    bool outgoing = direction == "out";
    auto &raw = registry_.at(outgoing ? "transfers_out" : "transfers_in").at(projectId).at(id);
    auto record = outgoing ? outgoing_record(raw) : incoming_record(raw);
    if (record.at("project") != projectId || record.at("id") != id)
        throw std::runtime_error("Invalid transfer catalog identity");
    remove_payloads(transfer_directory(state_, record, outgoing), record, outgoing);
    registry_[outgoing ? "transfers_out" : "transfers_in"][projectId].erase(id);
    event(projectId, "transfer", "Local transfer removed: " + id);
    return {{"removed", id}, {"direction", direction}, {"received_files_preserved", true}};
}

Json Core::transfer_send(const Json &data) {
    auto token = env("CYSYNCK_MESH_TOKEN");
    if (token.size() < 24)
        throw std::runtime_error(
            "Set CYSYNCK_MESH_TOKEN (at least 24 random characters) on paired devices");
    DiskLock lease(state_, "transfers-send.lock", false);
    Json record, peer;
    std::string localAddress, sender, localPublicKey;
    auto projectId = identifier(data.at("project"));
    {
        std::lock_guard lock(mutex_);
        DiskLock disk(state_);
        load();
        auto p = project(projectId);
        localAddress = registry_.at("network").at("profile").at("address");
        localPublicKey = registry_.at("network").at("profile").value("public_key", "");
        sender = registry_.at("peer_id");
        if (data.contains("transfer")) {
            auto id = identifier(data.at("transfer"));
            record = outgoing_record(registry_.at("transfers_out").at(projectId).at(id));
            if (record.at("project") != projectId || record.at("id") != id)
                throw std::runtime_error("Invalid transfer catalog identity");
        } else {
            auto peerId = identifier(data.at("peer"));
            (void)trusted_peer(registry_, projectId, peerId);
            if (pending_count(registry_, "transfers_out") >= 32 ||
                registry_.value("transfers_out", Json::object())
                        .value(projectId, Json::object())
                        .size() >= 10000)
                throw std::runtime_error("Outgoing transfer catalog limit reached");
            auto path = project_child(fs::u8path(p.at("root").get<std::string>()), data.at("path"));
            received_name(path_string(path.filename()));
            auto limits = transfer_limits(registry_);
            auto reserved = transfer_reserved(registry_, true);
            auto quota = limits.at("outbox_quota_mib").get<uintmax_t>() * transfer_chunk_size;
            if (reserved > quota)
                throw std::runtime_error("Outgoing snapshots exceed the configured quota");
            auto available =
                std::min(quota - reserved,
                         limits.at("file_limit_mib").get<uintmax_t>() * transfer_chunk_size);
            record = {{"id", random_id()},
                      {"project", projectId},
                      {"peer", peerId},
                      {"created", timestamp()},
                      {"name", path_string(path.filename())},
                      {"protocol", 1},
                      {"next", 0},
                      {"delivered", false},
                      {"error_code", ""},
                      {"error", ""}};
            auto directory = transfer_directory(state_, record, true);
            if (fs::exists(directory))
                throw std::runtime_error("Outgoing transfer directory already exists");
            private_directory(directory);
            auto snapshot = project_child(directory, "snapshot");
            try {
                auto captured = freeze_file(path, snapshot, available);
                record["bytes"] = captured.at("bytes");
                record["sha256"] = captured.at("sha256");
            } catch (...) {
                remove_payloads(directory, record, true);
                throw;
            }
            registry_["transfers_out"][projectId][record.at("id").get<std::string>()] = record;
            save();
        }
        peer = trusted_peer(registry_, projectId, record.at("peer"));
    }
    auto id = record.at("id").get<std::string>();
    auto persist = [&] {
        std::lock_guard lock(mutex_);
        DiskLock disk(state_);
        load();
        (void)trusted_peer(registry_, projectId, record.at("peer"));
        if (record.value("delivered", false) &&
            !registry_.at("transfers_out").at(projectId).at(id).value("delivered", false)) {
            registry_["messages"].push_back({{"id", id},
                                             {"created", timestamp()},
                                             {"project", projectId},
                                             {"peer", record.at("peer")},
                                             {"direction", "out"},
                                             {"kind", "file"},
                                             {"body",
                                              {{"name", record.at("name")},
                                               {"bytes", record.at("bytes")},
                                               {"sha256", record.at("sha256")}}}});
            if (registry_["messages"].size() > 2000)
                registry_["messages"].erase(registry_["messages"].begin());
        }
        registry_["transfers_out"][projectId][id] = record;
        save();
    };
    std::string failure =
        "Transfer interrupted or refused. Check receiver, pairing and VPN, then resume.";
    std::string failureCode = "interrupted";
    try {
        if (record.value("delivered", false))
            return progress(record, chunk_count(record), true);
        auto directory = transfer_directory(state_, record, true);
        auto snapshot = project_child(directory, "snapshot");
        verify_received(snapshot, record);
        httplib::Client client(peer.at("address").get<std::string>(),
                               peer.at("mesh_port").get<int>());
#ifdef __linux__
        auto binding = linux_direct_interface(localAddress, sender, peer.at("address"));
        client.set_socket_options([localAddress, binding](auto socket) {
            bind_linux_direct_socket(socket, binding);
#elif defined(_WIN32)
        auto binding = windows_direct_interface(localAddress, sender, peer.at("address"));
        client.set_socket_options([localAddress, binding](auto socket) {
            bind_windows_direct_socket(socket, binding);
#elif defined(__APPLE__)
        auto binding = macos_direct_interface(localAddress, sender, localPublicKey, peer.at("address"));
        client.set_socket_options([localAddress, binding](auto socket) {
            bind_macos_direct_socket(socket, binding);
#else
        client.set_socket_options([localAddress](auto socket) {
#endif
            if (!httplib::detail::bind_ip_address(socket, localAddress))
                throw std::runtime_error(
                    "The local VPN address is unavailable; connect WireGuard first");
        });
        client.set_connection_timeout(10);
        client.set_read_timeout(60);
        client.set_write_timeout(60);
        client.set_follow_location(false);
        httplib::Headers headers{{"Authorization", "Bearer " + token},
                                 {"X-CySynck-Peer", sender},
                                 {"X-CySynck-Project", projectId},
                                 {"X-CySynck-Id", id}};
        auto post = [&](const std::string &action, const std::string &body,
                        const httplib::Headers &h) {
            // Trust may be revoked while an earlier chunk is in flight.
            {
                std::lock_guard lock(mutex_);
                DiskLock disk(state_);
                load();
                if (trusted_peer(registry_, projectId, record.at("peer")) != peer)
                    throw std::runtime_error("Peer configuration changed; resume again");
                if (registry_.at("network").at("profile").at("address") != localAddress ||
                    registry_.at("peer_id") != sender)
                    throw std::runtime_error("Local network identity changed; resume again");
            }
            auto response =
                client.Post("/transfer/" + action, h, body,
                            action == "chunk" ? "application/octet-stream" : "application/json");
            if (!response) {
                failureCode = "unreachable";
                failure = "Peer unreachable; reconnect the VPN and resume this transfer";
                throw std::runtime_error(failure);
            }
            if (response->status != 200) {
                failureCode = response->status == 404 ? "unsupported_protocol" : "peer_refused";
                failure = response->status == 404 ? "Peer does not support resumable transfers; "
                                                    "upgrade it or send with --legacy true"
                                                  : "Peer refused transfer request (HTTP " +
                                                        std::to_string(response->status) + ")";
                throw std::runtime_error(failure);
            }
            auto result = Json::parse(response->body);
            if (result.at("id") != id || result.at("protocol") != 1 ||
                !result.at("next").is_number_integer() || result.at("next").get<int64_t>() < 0 ||
                result.at("next").get<uintmax_t>() > chunk_count(record) ||
                !result.at("delivered").is_boolean())
                throw std::runtime_error("Invalid transfer acknowledgement");
            return result;
        };
        auto acknowledgement = post("begin", metadata(record).dump(), headers);
        auto next = acknowledgement.at("next").get<uintmax_t>();
        record["next"] = next;
        record["error"] = "";
        record["error_code"] = "";
        persist();
        if (!acknowledgement.at("delivered").get<bool>()) {
            std::ifstream input(snapshot, std::ios::binary);
            input.seekg(static_cast<std::streamoff>(next * transfer_chunk_size));
            for (; next < chunk_count(record); ++next) {
                auto size = acknowledged(record, next + 1) - acknowledged(record, next);
                std::string block(static_cast<size_t>(size), '\0');
                input.read(block.data(), static_cast<std::streamsize>(size));
                if (!input || input.gcount() != static_cast<std::streamsize>(size))
                    throw std::runtime_error("Cannot read outgoing snapshot");
                auto chunkHeaders = headers;
                chunkHeaders.emplace("X-CySynck-Chunk", std::to_string(next));
                chunkHeaders.emplace("X-CySynck-Chunk-SHA256", sha256(block));
                auto result = post("chunk", block, chunkHeaders);
                if (result.at("next") != next + 1 || result.at("delivered").get<bool>())
                    throw std::runtime_error("Unexpected chunk acknowledgement");
                record["next"] = next + 1;
                persist();
            }
            acknowledgement =
                post("finish", Json{{"project", projectId}, {"id", id}}.dump(), headers);
        }
        if (!acknowledgement.at("delivered").get<bool>() ||
            acknowledgement.at("next").get<uintmax_t>() != chunk_count(record))
            throw std::runtime_error("Peer has not confirmed delivery");
        record["delivered"] = true;
        record["next"] = chunk_count(record);
        persist();
        // The journal is durable before its frozen source can be removed.
        auto result = progress(record, chunk_count(record), true);
        try {
            remove_payloads(directory, record, true);
        } catch (const std::exception &) {
            result["cleanup_pending"] = true;
        }
        return result;
    } catch (const std::exception &) {
        // Keep peer responses, tokens and filesystem diagnostics out of the transferable journal.
        record["error"] = failure;
        record["error_code"] = failureCode;
        try {
            persist();
        } catch (...) { /* A revoked peer must not overwrite the last journal. */
        }
        auto result =
            progress(record, record.at("next").get<uintmax_t>(), record.value("delivered", false));
        result["error"] = record.at("error");
        result["error_code"] = failureCode;
        return result;
    }
}
} // namespace cysynck
