#pragma once
#include <cysynck/core.hpp>
#include "process_stream.hpp"

namespace cysynck {
// Offline exchange uses canonical Git LFS v1 objects; it never runs a filter or a fetch.
Json lfs_inventory(const Json &project, const std::string &head);
Json git_lfs_pointer(const std::string &bytes);
void require_default_git_environment();
Json validate_lfs_manifest(const Json &manifest);
fs::path lfs_local_store(const Json &project);
fs::path approved_lfs_directory(const std::string &path);
fs::path lfs_object_path(const fs::path &store, const std::string &digest);
void verify_lfs_object(const fs::path &path, const Json &object);
void copy_lfs_object(const fs::path &source, const fs::path &store, const Json &object);
Json lfs_availability(const fs::path &store, const Json &inventory);
std::string offline_git(const Json &project, const std::vector<std::string> &args,
                        const std::string &input = {});
ProcessResult offline_git_result(const Json &project, const std::vector<std::string> &args,
                                 const std::string &input = {});
void offline_git_stream(const Json &project, const std::vector<std::string> &args,
                        const ProcessSink &sink, int timeout_seconds = 120);
} // namespace cysynck
