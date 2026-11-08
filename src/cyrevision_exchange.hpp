#pragma once
#include <cysynck/core.hpp>
namespace cysynck {
Json cyrevision_action(const fs::path &state, const Json &project, const std::string &action,
                       const Json &data);
}
