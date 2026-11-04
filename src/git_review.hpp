#pragma once
#include <cysynck/core.hpp>
namespace cysynck {
Json review_git_branch(const Json &project, const Json &data, bool file);
Json imported_git_branches(const Json &project);
Json git_local_head(const Json &project);
Json git_revision_context(const Json &project, const Json &data);
Json git_branch_history(const Json &project, const Json &data);
Json review_git_commit(const Json &project, const Json &data, bool file);
Json review_git_merge(const fs::path &state, const Json &project, const Json &data, bool file);
} // namespace cysynck
