#pragma once
#include <cysynck/core.hpp>
namespace cysynck {
Json review_git_branch(const Json &project, const Json &data, bool file);
Json imported_git_branches(const Json &project);
} // namespace cysynck
