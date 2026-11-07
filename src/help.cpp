#include "help.hpp"
#include "help_contexts.hpp"
#include <algorithm>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace cysynck::help {
namespace {
std::string encode(const std::string &value, bool slashes = false) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~' || (slashes && c == '/'))
            out += static_cast<char>(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
bool complete(const fs::path &path) {
    return fs::is_regular_file(path / "Documentation.html") &&
           fs::is_regular_file(path / "DocData/manifest.json") &&
           fs::is_regular_file(path / "DocData/data.js") &&
           fs::is_regular_file(path / "DocData/viewer.js") &&
           fs::is_regular_file(path / "DocData/viewer.css");
}
} // namespace
fs::path executable() {
#ifdef _WIN32
    std::wstring buffer(32768, 0);
    auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size == buffer.size())
        throw std::runtime_error("Cannot locate the executable");
    buffer.resize(size);
    return fs::canonical(buffer);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, 0);
    if (_NSGetExecutablePath(buffer.data(), &size))
        throw std::runtime_error("Cannot locate the executable");
    return fs::canonical(fs::u8path(buffer.c_str()));
#else
    return fs::canonical("/proc/self/exe");
#endif
}
fs::path directory() {
    const auto override = env("CYSYNCK_DOC_ROOT");
    if (!override.empty()) {
        auto path = fs::absolute(fs::u8path(override));
        if (!complete(path))
            throw std::runtime_error("CYSYNCK_DOC_ROOT must contain a complete CyDoc export");
        return fs::canonical(path);
    }
    auto bin = executable().parent_path();
    for (const auto &path : {bin / "Documentation", bin / "../Resources/Documentation",
                             bin / "../Documentation", bin / "../share/cysynck/Documentation"})
        if (complete(path))
            return fs::canonical(path);
    throw std::runtime_error(
        "Offline documentation missing; install the complete package or set CYSYNCK_DOC_ROOT");
}
std::string file_url(const fs::path &path) {
    // Encode UTF-8 path bytes, including literal %, # and ?, before adding the CyDoc fragment.
    const auto bytes = fs::absolute(path).lexically_normal().generic_u8string();
    const std::string value(bytes.begin(), bytes.end());
#ifdef _WIN32
    if (value.rfind("//", 0) == 0) {
        auto separator = value.find('/', 2);
        if (separator == std::string::npos)
            throw std::runtime_error("A UNC documentation path requires a share");
        return "file://" + encode(value.substr(2, separator - 2)) +
               encode(value.substr(separator), true);
    }
    if (value.size() < 3 || value[1] != ':' || value[2] != '/')
        throw std::runtime_error("Expected an absolute documentation path");
    return "file:///" + value.substr(0, 2) + encode(value.substr(2), true);
#else
    return "file://" + encode(value, true);
#endif
}
std::string route(const std::string &page, const std::string &section,
                  const std::string &language) {
    return "#/" + encode(language == "fr" ? "fr" : "en") + "/" + encode(page) +
           (section.empty() ? "" : "/" + encode(section));
}
Json location(const std::string &requested, const std::string &section,
              const std::string &language) {
    auto root = directory();
    const auto manifest = Json::parse(read_file(root / "DocData/manifest.json", 8 * 1024 * 1024));
    auto page = requested.empty() ? "overview" : requested;
    auto anchor = section;
    if (page.rfind("docs/", 0) == 0)
        page.erase(0, 5);
    if (auto hash = page.find('#'); hash != std::string::npos) {
        if (anchor.empty())
            anchor = page.substr(hash + 1);
        page.resize(hash);
    }
    if (page.ends_with(".md"))
        page.resize(page.size() - 3);
    const auto aliases = manifest.value("aliases", Json::object());
    if (aliases.contains(page)) {
        page = aliases.at(page).get<std::string>();
        if (auto hash = page.find('#'); hash != std::string::npos) {
            if (anchor.empty())
                anchor = page.substr(hash + 1);
            page.resize(hash);
        }
    }
    auto locale = language == "fr" ? "fr" : "en";
    const auto &pages = manifest.at("pages");
    const auto found = std::find_if(pages.begin(), pages.end(), [&](const Json &entry) {
        return entry.at("id") == page && entry.at("locale") == locale;
    });
    if (found == pages.end())
        throw std::runtime_error("Unknown documentation page: " + page);
    if (!anchor.empty()) {
        const auto &headings = found->at("headings");
        const auto &anchors = found->value("anchors", Json::array());
        if (std::none_of(headings.begin(), headings.end(),
                         [&](const Json &h) { return h.at("id") == anchor; }) &&
            std::find(anchors.begin(), anchors.end(), Json(anchor)) == anchors.end())
            throw std::runtime_error("Unknown documentation section: " + anchor);
    }
    auto fragment = route(page, anchor, locale);
    return {{"path", path_string(root / "Documentation.html")},
            {"url", file_url(root / "Documentation.html") + fragment},
            {"route", fragment},
            {"page", page},
            {"section", anchor},
            {"language", locale}};
}
Json context(const std::string &view, const std::string &language) {
    const auto contexts = Json::parse(cysynck_help_contexts);
    return location(contexts.value(view, "overview"), {}, language);
}
void open(const Json &target) {
    const auto url = target.at("url").get<std::string>();
    if (url.rfind("file://", 0) != 0)
        throw std::runtime_error("Documentation must use a local file URL");
#ifdef _WIN32
    auto wide = fs::u8path(url);
    if (reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        throw std::runtime_error("Cannot open the local manual in a browser; use docs --print");
#else
#ifdef __APPLE__
    const auto result = process({"open", url});
#else
    const auto result = process({"xdg-open", url});
#endif
    if (result.code != 0)
        throw std::runtime_error("Cannot open the local manual in a browser; use docs --print");
#endif
}
Json command(const std::vector<std::string> &arguments) {
    std::string page = "overview", section, language = "en";
    bool print = false, has_page = false;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const auto &arg = arguments[i];
        if (arg == "--print")
            print = true;
        else if (arg == "--language" || arg == "--section") {
            if (++i == arguments.size())
                throw std::runtime_error("Missing documentation option value");
            (arg == "--language" ? language : section) = arguments[i];
        } else if (arg.starts_with('-') || has_page)
            throw std::runtime_error("Use docs [PAGE] [--language en|fr] [--section ID] [--print]");
        else {
            page = arg;
            has_page = true;
        }
    }
    auto target = location(page, section, language);
    if (!print)
        open(target);
    target["opened"] = !print;
    return target;
}
} // namespace cysynck::help
