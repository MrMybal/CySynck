#pragma once
#include <cysynck/core.hpp>
namespace cysynck {
// Raw blobs below 1024 bytes, deduplicated and fetched in bounded batches.
std::map<std::string, std::string> git_small_blobs(const Json &project, const Json &entries);
} // namespace cysynck
