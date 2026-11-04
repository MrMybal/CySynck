#pragma once
#include <cysynck/core.hpp>
namespace cysynck {
Json integrate_git_branch(const fs::path &state, const Json &project, const Json &data,
                          bool merge = false);
Json git_integrations(const fs::path &state, const Json &project, const Json &data);
Json check_git_integration(const fs::path &state, const Json &project, const Json &data);
Json acknowledge_git_integration(const fs::path &state, const Json &project, const Json &data);
} // namespace cysynck
