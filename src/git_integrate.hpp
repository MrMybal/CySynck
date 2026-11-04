#pragma once
#include <cysynck/core.hpp>
namespace cysynck {
Json integrate_git_branch(const fs::path &state, const Json &project, const Json &data);
} // namespace cysynck
