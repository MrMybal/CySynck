#pragma once
#include <cysynck/core.hpp>

namespace cysynck {
// Offline exchange uses canonical Git LFS v1 objects; it never runs a filter or a fetch.
Json lfs_inventory(const Json &project, const std::string &head);
Json validate_lfs_manifest(const Json &manifest);
fs::path lfs_local_store(const Json &project);
fs::path lfs_object_path(const fs::path &store, const std::string &digest);
void verify_lfs_object(const fs::path &path, const Json &object);
void copy_lfs_object(const fs::path &source, const fs::path &store, const Json &object);
Json lfs_availability(const fs::path &store, const Json &inventory);
std::string offline_git(const Json &project, const std::vector<std::string> &args,
                        const std::string &input = {});
} // namespace cysynck
