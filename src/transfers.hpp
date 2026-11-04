#pragma once
#include <cysynck/core.hpp>

namespace cysynck {
inline constexpr uintmax_t transfer_chunk_size = 1024 * 1024;
inline constexpr uintmax_t transfer_file_limit = 100 * transfer_chunk_size;
inline constexpr uintmax_t transfer_quota = 1024 * transfer_chunk_size;
const Json &trusted_peer(const Json &registry, const std::string &project, const std::string &peer);
uintmax_t transfer_reserved(const Json &registry);
} // namespace cysynck
