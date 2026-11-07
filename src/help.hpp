#pragma once
#include <cysynck/core.hpp>

namespace cysynck::help {
fs::path executable();
fs::path directory();
std::string file_url(const fs::path &path);
std::string route(const std::string &page, const std::string &section = {},
                  const std::string &language = "en");
Json location(const std::string &page = "overview", const std::string &section = {},
              const std::string &language = "en");
Json context(const std::string &view, const std::string &language = "en");
void open(const Json &location);
Json command(const std::vector<std::string> &arguments);
} // namespace cysynck::help
