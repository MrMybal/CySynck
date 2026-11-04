#pragma once
#include <cysynck/core.hpp>
namespace cysynck {
Json extract_git_revision(const fs::path &state, const Json &projects, const Json &project,
                          const Json &data);
}
