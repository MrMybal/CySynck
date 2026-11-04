#pragma once
#include <cysynck/core.hpp>
namespace cysynck {
Json git_exchange_manifest(const fs::path &file, const std::string &project);
Json git_exchange_storage(const Json &project, const std::string &peer, const Json &data);
} // namespace cysynck
