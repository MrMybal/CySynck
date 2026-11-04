#pragma once
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace cysynck {
// These are opaque clock ticks used to detect changes, not a Unix timestamp.
// libc++ may use a 128-bit clock representation; JSON stores signed 64-bit integers.
inline int64_t file_time_ticks(std::filesystem::file_time_type time) {
    auto ticks = time.time_since_epoch().count();
    if (ticks < std::numeric_limits<int64_t>::min() || ticks > std::numeric_limits<int64_t>::max())
        throw std::runtime_error("File timestamp exceeds the supported range");
    return static_cast<int64_t>(ticks);
}
} // namespace cysynck
