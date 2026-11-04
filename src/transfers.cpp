#include "transfers.hpp"
#include "backup.hpp"
#include "inbox.hpp"
#include <algorithm>
#include <fstream>
#include <httplib.h>
#include <regex>
#include <stdexcept>

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
        if (!hash.is_string() ||
            !std::regex_match(hash.get<std::string>(), std::regex("[0-9a-f]{64}")))
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
        if (!outgoing)
            for (uintmax_t i = 0; i < chunk_count(record); ++i)
                owned |= name == std::to_string(i) + ".block";
        if (!owned || path_is_link(entry.path()) || !entry.is_regular_file())
            throw std::runtime_error(
                "Transfer directory contains unexpected content; review it locally");
    }
    for (const auto &entry : fs::directory_iterator(directory))
        fs::remove(entry.path());
    fs::remove(directory);
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
} // namespace

uintmax_t transfer_reserved(const Json &registry) {
    uintmax_t reserved = 0;
    for (const auto &project : registry.value("transfers_in", Json::object()))
        for (const auto &raw : project) {
            auto record = incoming_record(raw);
            auto size = record.at("bytes").get<uintmax_t>();
            if (reserved > transfer_quota - size)
                throw std::runtime_error("Pending transfer quota exceeded");
            reserved += size;
        }
    return reserved;
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
        if (inbox_used(state_) + transfer_reserved(registry_) +
                record.at("bytes").get<uintmax_t>() >
            transfer_quota)
            throw std::runtime_error("Received and pending file quota (1 GiB) reached");
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
    std::string localAddress, sender;
    auto projectId = identifier(data.at("project"));
    {
        std::lock_guard lock(mutex_);
        DiskLock disk(state_);
        load();
        auto p = project(projectId);
        localAddress = registry_.at("network").at("profile").at("address");
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
            if (!fs::is_regular_file(path) || fs::file_size(path) > transfer_file_limit)
                throw std::runtime_error("Direct files are limited to 100 MiB");
            auto sourceSize = fs::file_size(path);
            uintmax_t reserved = sourceSize;
            for (const auto &table : registry_.value("transfers_out", Json::object()))
                for (const auto &item : table)
                    if (!item.value("delivered", false))
                        reserved += metadata(item).at("bytes").get<uintmax_t>();
            if (reserved > transfer_quota)
                throw std::runtime_error("Outgoing snapshot quota (1 GiB) reached");
            auto entry = capture(path);
            if (entry.at("size").get<uintmax_t>() > transfer_file_limit ||
                reserved - sourceSize + entry.at("size").get<uintmax_t>() > transfer_quota)
                throw std::runtime_error("Captured file exceeds the outgoing transfer quota");
            record = {{"id", random_id()},
                      {"project", projectId},
                      {"peer", peerId},
                      {"created", timestamp()},
                      {"name", path_string(path.filename())},
                      {"bytes", entry.at("size")},
                      {"sha256", entry.at("sha256")},
                      {"protocol", 1},
                      {"next", 0},
                      {"delivered", false},
                      {"error_code", ""},
                      {"error", ""}};
            auto directory = transfer_directory(state_, record, true);
            private_directory(directory);
            auto snapshot = project_child(directory, "snapshot");
            fs::copy_file(state_ / "objects" / entry.at("sha256").get<std::string>(), snapshot,
                          fs::copy_options::none);
            secure_state_path(snapshot, false);
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
        client.set_socket_options([localAddress](auto socket) {
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
