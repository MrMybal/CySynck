#include "backup_data.hpp"
#include "backup.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <picosha2.h>
#include <regex>
#include <stdexcept>

namespace cysynck {
namespace {
bool digest(const Json &value) {
    return value.is_string() &&
           std::regex_match(value.get<std::string>(), std::regex("[0-9a-f]{64}"));
}
uintmax_t size_value(const Json &value) {
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0))
        throw std::runtime_error("Invalid backup file size");
    return value.get<uintmax_t>();
}
} // namespace
bool block_entry(const Json &entry) {
    auto storage = entry.value("storage", "whole");
    if (storage != "whole" && storage != "blocks-v1")
        throw std::runtime_error("Unsupported backup file storage");
    if (storage == "whole" && entry.contains("blocks"))
        throw std::runtime_error("Whole-file backup cannot contain a block list");
    return storage == "blocks-v1";
}
Json backup_references(const Json &entry) {
    if (!digest(entry.at("sha256")))
        throw std::runtime_error("Invalid backup file digest");
    auto size = size_value(entry.at("size"));
    if (!block_entry(entry))
        return Json::array(
            {{{"namespace", "objects"}, {"sha256", entry.at("sha256")}, {"size", size}}});
    const auto &blocks = entry.at("blocks");
    if (!blocks.is_array() || blocks.empty() || blocks.size() > backup_max_blocks || size == 0 ||
        size > backup_block_size * backup_max_blocks ||
        blocks.size() != (size + backup_block_size - 1) / backup_block_size)
        throw std::runtime_error("Invalid backup block count or file size");
    Json result = Json::array();
    uintmax_t remaining = size;
    for (const auto &block : blocks) {
        if (!digest(block.at("sha256")) ||
            size_value(block.at("size")) != std::min(remaining, backup_block_size))
            throw std::runtime_error("Invalid backup block digest or size");
        result.push_back(
            {{"namespace", "blocks"}, {"sha256", block.at("sha256")}, {"size", block.at("size")}});
        remaining -= block.at("size").get<uintmax_t>();
    }
    return result;
}
fs::path backup_blob(const fs::path &state, const Json &reference) {
    auto space = reference.at("namespace").get<std::string>();
    if ((space != "objects" && space != "blocks") || !digest(reference.at("sha256")))
        throw std::runtime_error("Invalid backup object reference");
    auto directory = state / space;
    plain_directory(directory);
    auto path = directory / reference.at("sha256").get<std::string>();
    if (path_is_link(path) || !fs::is_regular_file(path))
        throw std::runtime_error("Backup object missing, linked or not a regular file");
    if (fs::file_size(path) != size_value(reference.at("size")))
        throw std::runtime_error("Backup object size differs from manifest");
    return path;
}
void check_backup_layout(const fs::path &state, const Json &entry) {
    for (const auto &reference : backup_references(entry))
        (void)backup_blob(state, reference);
}
void stream_backup_file(const fs::path &state, const Json &entry,
                        const std::function<void(const char *, size_t)> &consume,
                        const std::function<void(const Json &, bool)> &object_result) {
    picosha2::hash256_one_by_one whole;
    std::array<char, 65536> buffer;
    uintmax_t total = 0;
    for (const auto &reference : backup_references(entry)) {
        try {
            auto path = backup_blob(state, reference);
            std::ifstream input(path, std::ios::binary);
            if (!input)
                throw std::runtime_error("Cannot read backup object");
            picosha2::hash256_one_by_one object;
            uintmax_t bytes = 0;
            while (input) {
                input.read(buffer.data(), buffer.size());
                auto count = static_cast<size_t>(input.gcount());
                if (count > reference.at("size").get<uintmax_t>() - bytes)
                    throw std::runtime_error("Backup object changed while reading");
                object.process(buffer.data(), buffer.data() + count);
                whole.process(buffer.data(), buffer.data() + count);
                bytes += count;
                if (consume)
                    consume(buffer.data(), count);
            }
            if (!input.eof() || bytes != reference.at("size").get<uintmax_t>())
                throw std::runtime_error("Backup object read failed or size changed");
            object.finish();
            if (reference.at("sha256") != picosha2::get_hash_hex_string(object))
                throw std::runtime_error("Backup object SHA-256 mismatch");
            total += bytes;
            if (object_result)
                object_result(reference, true);
        } catch (...) {
            if (object_result)
                object_result(reference, false);
            throw;
        }
    }
    whole.finish();
    if (total != entry.at("size").get<uintmax_t>() ||
        entry.at("sha256") != picosha2::get_hash_hex_string(whole))
        throw std::runtime_error("Backup file SHA-256 mismatch");
}
BlockCapture capture_blocks(const fs::path &state, const fs::path &source) {
    auto directory = state / "blocks";
    plain_directory(directory);
    fs::create_directories(directory);
    secure_state_path(directory, true);
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto size = fs::file_size(source);
        auto modified = fs::last_write_time(source);
        if (size == 0 || size > backup_block_size * backup_max_blocks)
            throw std::runtime_error(
                "File changed beyond the block backup size range; retry backup");
        std::ifstream input(source, std::ios::binary);
        if (!input)
            throw std::runtime_error("Cannot capture backup blocks");
        BlockCapture result;
        result.entry = {{"storage", "blocks-v1"}, {"blocks", Json::array()}};
        picosha2::hash256_one_by_one hash;
        uintmax_t total = 0;
        std::string block(static_cast<size_t>(backup_block_size), '\0');
        while (input) {
            input.read(block.data(), static_cast<std::streamsize>(block.size()));
            auto count = static_cast<size_t>(input.gcount());
            if (!count)
                break;
            if (result.entry.at("blocks").size() >= backup_max_blocks)
                throw std::runtime_error("File grew beyond the block backup size range");
            hash.process(block.data(), block.data() + count);
            auto bytes = block.substr(0, count);
            auto id = sha256(bytes);
            auto destination = directory / id;
            if (path_is_link(destination) ||
                (fs::exists(destination) && !fs::is_regular_file(destination)))
                throw std::runtime_error("Backup block is linked or is not a regular file");
            if (fs::exists(destination) && fs::file_size(destination) == count &&
                sha256_file(destination) == id) {
                ++result.reused_blocks;
                result.reused_bytes += count;
            } else {
                atomic_write(destination, bytes, true);
                ++result.written_blocks;
                result.written_bytes += count;
            }
            result.entry["blocks"].push_back({{"sha256", id}, {"size", count}});
            total += count;
        }
        if (!input.eof())
            throw std::runtime_error("Block backup read failed");
        if (size != total || fs::file_size(source) != size ||
            fs::last_write_time(source) != modified)
            continue; // Unpublished blocks are safe; explicit storage cleanup can reclaim them.
        hash.finish();
        result.entry["sha256"] = picosha2::get_hash_hex_string(hash);
        result.entry["size"] = size;
        result.entry["permissions"] = static_cast<unsigned>(fs::status(source).permissions());
        (void)backup_references(result.entry);
        return result;
    }
    throw std::runtime_error("File keeps changing during block backup; retry when writes settle");
}
} // namespace cysynck
