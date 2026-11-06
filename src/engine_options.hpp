#pragma once
#include <cctype>
#include <initializer_list>
#include <stdexcept>
#include <string>
namespace cysynck {
inline std::string syncthing_option(const std::string &help,
                                    std::initializer_list<const char *> alternatives) {
    for (auto name : alternatives) {
        std::string option(name);
        for (size_t start = 0; (start = help.find(option, start)) != help.npos; ++start) {
            auto end = start + option.size();
            bool left = start == 0 || std::isspace(static_cast<unsigned char>(help[start - 1])) ||
                        help[start - 1] == '[';
            bool right = end == help.size() || help[end] == '=' || help[end] == ',' ||
                         help[end] == ']' || std::isspace(static_cast<unsigned char>(help[end]));
            if (left && right)
                return option;
        }
    }
    throw std::runtime_error("Syncthing help does not expose a supported " +
                             std::string(*alternatives.begin()) + " option");
}
} // namespace cysynck
