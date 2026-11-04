#pragma once
#include <cysynck/core.hpp>

namespace cysynck {
void received_name(const std::string &name);
Json received_records(const Json &registry, const std::string &project);
Json received_record(const Json &record, const std::string &project, const std::string &id);
fs::path received_path(const fs::path &state, const std::string &project, const std::string &id);
void verify_received(const fs::path &path, const Json &record);
uintmax_t inbox_used(const fs::path &state);
} // namespace cysynck
