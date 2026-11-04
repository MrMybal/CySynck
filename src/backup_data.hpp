#pragma once
#include <cysynck/core.hpp>
#include <functional>

namespace cysynck {
inline constexpr uintmax_t backup_block_size = 4ull * 1024 * 1024;
inline constexpr uintmax_t backup_block_threshold = 2 * backup_block_size;
inline constexpr size_t backup_max_blocks = 65536;
inline constexpr uintmax_t backup_cdc_min = 1024ull * 1024;
inline constexpr uintmax_t backup_cdc_max = 8ull * 1024 * 1024;
bool block_entry(const Json &entry);
// Each item is {namespace, sha256, size}; namespaces never come from a manifest path.
Json backup_references(const Json &entry);
fs::path backup_blob(const fs::path &state, const Json &reference);
struct BlockCapture {
    Json entry;
    uintmax_t written_bytes = 0, reused_bytes = 0, written_blocks = 0, reused_blocks = 0;
};
BlockCapture capture_blocks(const fs::path &state, const fs::path &source,
                            bool content_defined = false);
// Streams verified bytes in bounded buffers and checks the whole-file digest as well.
void stream_backup_file(const fs::path &state, const Json &entry,
                        const std::function<void(const char *, size_t)> &consume = {},
                        const std::function<void(const Json &, bool)> &object_result = {});
void check_backup_layout(const fs::path &state, const Json &entry);
} // namespace cysynck
