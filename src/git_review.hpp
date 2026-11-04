#pragma once
#include <cysynck/core.hpp>
namespace cysynck {
Json review_git_branch(const Json &project, const Json &data, bool file);
Json imported_git_branches(const Json &project);
Json git_local_head(const Json &project);
Json git_branch_history(const Json &project, const Json &data);
Json review_git_commit(const Json &project, const Json &data, bool file);
} // namespace cysynck
