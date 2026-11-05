#pragma once
#include <cysynck/core.hpp>
namespace cysynck {
Json materialize_git_lfs(const fs::path &state, const Json &project, const Json &data);
Json git_materializations(const fs::path &state, const Json &project, const Json &data);
Json check_git_materialization(const fs::path &state, const Json &project, const Json &data);
Json resume_git_materialization(const fs::path &state, const Json &project, const Json &data);
Json acknowledge_git_materialization(const fs::path &state, const Json &project, const Json &data);
} // namespace cysynck
