#pragma once
#include <cysynck/core.hpp>

namespace cysynck {
// Shared validation for retention, restoration and storage reference collection.
Json read_snapshot(const fs::path &path, const std::string &project);
void validate_snapshot(const Json &manifest, const fs::path &path, const std::string &project);
void plain_directory(const fs::path &path);
void secure_state_path(const fs::path &path, bool directory);
fs::path restore_target(const fs::path &state, const Json &projects, const Json &data);
} // namespace cysynck
