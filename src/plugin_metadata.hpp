#pragma once
#include <cysynck/core.hpp>
#include <cysynck/plugin.h>
namespace cysynck {
std::string plugin_text(const char *value, size_t maximum, bool required = false);
Json describe_plugin(const cysynck_plugin_v1 *api, const char *descriptor);
Json validate_mode_options(const Json &mode, const Json &values);
} // namespace cysynck
