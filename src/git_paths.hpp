#pragma once
#include <cysynck/core.hpp>
#include <algorithm>
#include <map>
#include <regex>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace cysynck {
struct GitPathLess {
    bool operator()(const std::string &a, const std::string &b) const {
#ifdef _WIN32
        auto x = fs::u8path(a).native(), y = fs::u8path(b).native();
        return CompareStringOrdinal(x.c_str(), static_cast<int>(x.size()), y.c_str(),
                                    static_cast<int>(y.size()), TRUE) == CSTR_LESS_THAN;
#else
        auto fold = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
        return std::lexicographical_compare(
            a.begin(), a.end(), b.begin(), b.end(),
            [&](unsigned char x, unsigned char y) { return fold(x) < fold(y); });
#endif
    }
};
inline void validate_git_path(const std::string &name,
                              std::map<std::string, std::string, GitPathLess> &paths) {
    auto path = fs::u8path(name);
    (void)Json(name).dump();
    if (name.empty() || name.size() > 4096 || name.front() == '/' || name.back() == '/' ||
        name.find("//") != name.npos || name.find_first_of("\\:") != name.npos ||
        name.find('\0') != name.npos || path.is_absolute())
        throw std::runtime_error("Git extraction contains an unsafe relative path");
    fs::path prefix;
    for (const auto &part : path) {
        auto segment = path_string(part), lower = segment;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : static_cast<char>(c);
        });
        if (segment.empty() || segment == "." || segment == ".." || lower == ".git" ||
            std::regex_match(lower, std::regex("git~[0-9]+")))
            throw std::runtime_error("Git extraction excludes repository metadata paths");
#ifdef _WIN32
        if (segment.back() == '.' || segment.back() == ' ' ||
            segment.find_first_of("<>\"|?*") != segment.npos ||
            std::any_of(segment.begin(), segment.end(), [](unsigned char c) { return c < 32; }) ||
            std::regex_match(segment, std::regex("(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?",
                                                 std::regex::icase)))
            throw std::runtime_error("Git extraction contains a reserved Windows filename");
#endif
        prefix /= part;
        auto spelling = path_string(prefix);
        auto [it, inserted] = paths.emplace(spelling, spelling);
        if (!inserted && it->second != spelling)
            throw std::runtime_error("Git extraction paths collide on this platform");
    }
}
} // namespace cysynck
