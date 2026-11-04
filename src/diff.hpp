#pragma once
#include <cysynck/core.hpp>

namespace cysynck {
std::vector<std::pair<std::string, fs::path>> project_files(const Json &project);
bool project_excludes(const Json &project, const std::string &relative);
Json inspect_conflict(const Json &project, const std::string &relative);
Json conflict_identity(const std::string &relative);
Json compare_paths(const fs::path &left, const fs::path &right, const std::string &target,
                   const std::string &expected_left_hash = {});
} // namespace cysynck
