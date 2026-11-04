#include <cysynck/core.hpp>
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace cysynck {
namespace {
bool executable_file(const fs::path &path) {
    std::error_code error;
    if (!fs::is_regular_file(path, error))
        return false;
#ifdef _WIN32
    auto extension = path_string(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".exe" || extension == ".com";
#else
    return access(path.c_str(), X_OK) == 0;
#endif
}
fs::path locate(const std::string &name) {
    if (name.empty() || name.find('\0') != std::string::npos)
        return {};
    auto path = fs::u8path(name);
    if (path.has_parent_path())
        return executable_file(path) ? fs::absolute(path) : fs::path{};
    std::istringstream directories(env("PATH"));
    std::string directory;
#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    while (std::getline(directories, directory, separator)) {
        if (directory.empty())
            continue;
#ifdef _WIN32
        if (directory.size() >= 2 && directory.front() == '"' && directory.back() == '"')
            directory = directory.substr(1, directory.size() - 2);
#endif
        auto candidate = fs::u8path(directory) / path;
        if (executable_file(candidate))
            return fs::absolute(candidate);
#ifdef _WIN32
        if (!path.has_extension())
            for (const auto *extension : {".exe", ".com"}) {
                candidate = fs::u8path(directory) / fs::u8path(name + extension);
                if (executable_file(candidate))
                    return fs::absolute(candidate);
            }
#endif
    }
    return {};
}
} // namespace
Json Core::diagnostics(const Json &data) {
    bool probe = data.value("probe_tools", false), probe_engine = data.value("probe_engine", false);
    bool sync = false, git_mode = false;
    for (const auto &p : registry_.at("projects")) {
        auto mode = p.at("mode").get<std::string>();
        git_mode |= mode == "git-bundles";
        sync |= mode == "sendreceive" || mode == "sendonly" || mode == "receiveonly" ||
                mode == "git-bundles";
    }
    bool vpn = registry_.contains("network") && registry_.at("network").contains("profile");
    Json checks = Json::array();
    auto add = [&](const std::string &id, const std::string &label, const std::string &status,
                   const std::string &message, const std::string &hint,
                   Json details = nlohmann::json::object()) {
        checks.push_back({{"id", id},
                          {"label", label},
                          {"status", status},
                          {"message", message},
                          {"hint", hint},
                          {"details", std::move(details)}});
    };
    auto tool = [&](const std::string &id, const std::string &label, const std::string &name,
                    bool required, const std::string &hint, const std::string &pattern = "") {
        auto executable = locate(name);
        Json details{{"required", required}, {"path", path_string(executable)}, {"probed", false}};
        if (executable.empty()) {
            add(id, label, required ? "error" : "warning", "Tool not found", hint, details);
            return;
        }
        if (probe && !pattern.empty()) {
            details["probed"] = true;
            try {
                auto result = process({path_string(executable), "--version"}, {}, {}, 5);
                std::smatch match;
                if (result.code || !std::regex_search(result.out, match, std::regex(pattern)))
                    throw std::runtime_error("Version probe failed");
                details["version"] = match[1].str();
            } catch (const std::exception &) {
                // Keep arbitrary tool output, environment values and exception text private.
                add(id, label, required ? "error" : "warning", "Version probe failed", hint,
                    details);
                return;
            }
        }
        add(id, label, "ok", "Tool available", "", details);
    };
    auto engine = engine_config();
    bool managed = engine.value("kind", std::string{}) == "managed";
    tool("tool.syncthing", "Syncthing",
         managed ? engine.value("executable", std::string{}) : "syncthing", sync && managed,
         "Install Syncthing or configure an existing instance.",
         R"(syncthing v([0-9][0-9A-Za-z.+_-]{0,63}))");
    tool("tool.git", "Git", "git", git_mode, "Install Git and add it to PATH.",
         R"(git version ([0-9][0-9A-Za-z.+_-]{0,63}))");
    tool("tool.wg", "WireGuard tools", env("CYSYNCK_WG", "wg"), vpn,
         "Install WireGuard tools or configure CYSYNCK_WG.",
         R"(wireguard-tools v([0-9][0-9A-Za-z.+_-]{0,63}))");
#ifdef _WIN32
    tool("tool.tunnel", "WireGuard activation",
         env("CYSYNCK_WIREGUARD", "C:/Program Files/WireGuard/wireguard.exe"), vpn,
         "Install the official WireGuard application.");
#else
    tool("tool.tunnel", "WireGuard activation", env("CYSYNCK_WG_QUICK", "wg-quick"), vpn,
         "Install wg-quick or configure CYSYNCK_WG_QUICK.");
#endif
    bool key = engine.at("key_configured").get<bool>();
    add("engine.configuration", "Syncthing connection",
        key    ? "ok"
        : sync ? "error"
               : "warning",
        key ? "Connection configured" : "Connection not configured",
        key ? "" : "Configure Syncthing in the Engine view.");
    if (probe_engine && key) {
        try {
            auto status = engine_request("GET", "system/status", Json::object(), 1);
            if (!status.contains("myID") || !status.at("myID").is_string())
                throw std::runtime_error("Invalid engine status");
            add("engine.reachable", "Syncthing API", "ok", "Connection available", "");
        } catch (const std::exception &) {
            add("engine.reachable", "Syncthing API", sync ? "error" : "warning",
                "Connection unavailable", "Start Syncthing and verify the configured connection.");
        }
    }
    for (const auto &p : registry_.at("projects")) {
        auto id = p.at("id").get<std::string>();
        auto name = p.at("name").get<std::string>();
        auto mode = p.at("mode").get<std::string>();
        for (const auto *field : {"root", "exchange_path"}) {
            if (std::string(field) == "exchange_path" && mode != "git-bundles")
                continue;
            bool available = false;
            auto directory = fs::u8path(p.at(field).get<std::string>());
            try {
                available = fs::is_directory(directory) && !path_is_link(directory);
            } catch (const std::exception &) {
            }
            add("project." + id + "." + field, name, available ? "ok" : "error",
                available ? "Directory available" : "Directory missing or linked",
                available ? "" : "Restore the project directory or review its path.",
                {{"project", id}, {"field", field}, {"path", path_string(directory)}});
        }
        std::error_code error;
        bool repository = fs::exists(fs::u8path(p.at("root").get<std::string>()) / ".git", error);
        if ((mode == "git-bundles" && !repository) ||
            (repository && (mode == "sendreceive" || mode == "sendonly" || mode == "receiveonly")))
            add("project." + id + ".git", name, "error", "Git mode needs attention",
                "Use Git exchange for repositories; keep each active .git local.",
                {{"project", id}});
        if (registry_.value("resolution_pending", Json::object()).contains(id))
            add("project." + id + ".recovery", name, "error", "Recovery review required",
                "Review interrupted resolutions before resuming this project.", {{"project", id}});
    }
    size_t errors = 0, warnings = 0;
    for (const auto &check : checks) {
        errors += check.at("status") == "error";
        warnings += check.at("status") == "warning";
    }
    return {{"version", version},   {"ready", errors == 0}, {"errors", errors},
            {"warnings", warnings}, {"probe_tools", probe}, {"probe_engine", probe_engine},
            {"checks", checks}};
}
} // namespace cysynck
