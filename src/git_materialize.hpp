#pragma once
#include <cysynck/core.hpp>
namespace cysynck {
Json materialize_git_lfs(const fs::path &state, const Json &project, const Json &data);
} // namespace cysynck
