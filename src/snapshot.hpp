#pragma once
#include <cysynck/core.hpp>
namespace cysynck {
Json snapshot_manifest(const fs::path &state, const Json &project, const std::string &id);
Json select_snapshot_files(const Json &manifest, const Json &data);
Json snapshot_query(const fs::path &state, const Json &project, const std::string &action,
                    const Json &data);
} // namespace cysynck
