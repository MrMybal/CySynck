#pragma once
#include <cysynck/core.hpp>

namespace cysynck {
inline std::string error_text(const std::exception &error) {
    // Native filesystem messages may contain bytes from a legacy platform encoding.
    auto bytes =
        Json(std::string(error.what())).dump(-1, ' ', false, Json::error_handler_t::replace);
    return Json::parse(bytes).get<std::string>();
}
} // namespace cysynck
