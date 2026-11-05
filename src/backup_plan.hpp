#pragma once
#include <cysynck/core.hpp>

namespace cysynck {
void validate_backup_excludes(const Json &rules);
// Empty means included; otherwise identifies the first matching rule or protected namespace.
Json backup_exclusion(const Json &project, const std::string &relative);
Json preview_backup(const fs::path &state, const Json &project, const Json &data);
} // namespace cysynck
