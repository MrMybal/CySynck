#pragma once
#include <cysynck/core.hpp>
namespace cysynck {
struct GitWorkingHashes {
    std::map<std::string, std::string> ids;
    size_t batches = 0;
    uintmax_t bytes = 0;
};
// Read current raw file bytes in bounded Git invocations; no object/index writes or filters.
GitWorkingHashes git_working_hashes(const Json &project, const Json &files);
} // namespace cysynck
