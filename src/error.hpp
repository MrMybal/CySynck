#pragma once
#include <cysynck/core.hpp>

namespace cysynck {
// Native filesystem and system messages may contain bytes from a legacy platform encoding
// (for example the ANSI code page of a localized Windows). Strict JSON serialization rejects
// them, so every message persisted or returned as JSON goes through these helpers.
inline std::string utf8_text(const std::string &bytes) {
    auto encoded = Json(bytes).dump(-1, ' ', false, Json::error_handler_t::replace);
    return Json::parse(encoded).get<std::string>();
}
inline std::string error_text(const std::exception &error) {
    return utf8_text(std::string(error.what()));
}
} // namespace cysynck
