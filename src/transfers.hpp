#pragma once
#include <cysynck/core.hpp>

namespace cysynck {
inline constexpr uintmax_t transfer_chunk_size = 1024 * 1024;
inline constexpr uintmax_t transfer_file_limit = 4096 * transfer_chunk_size;
inline constexpr uintmax_t transfer_quota = 65536 * transfer_chunk_size;
Json transfer_limits(const Json &registry);
void check_incoming_capacity(const fs::path &state, const Json &registry, uintmax_t bytes);
const Json &trusted_peer(const Json &registry, const std::string &project, const std::string &peer);
uintmax_t transfer_reserved(const Json &registry, bool outgoing = false);
} // namespace cysynck
