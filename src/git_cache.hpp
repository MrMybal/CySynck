#pragma once
#include <cysynck/core.hpp>
namespace cysynck {
Json inspect_git_lfs_cache(const fs::path &state, const Json &project, const Json &data);
Json recover_git_lfs_cache(const fs::path &state, const Json &project, const Json &data);
Json git_lfs_recoveries(const fs::path &state, const Json &project, const Json &data, bool detail);
} // namespace cysynck
