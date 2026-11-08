#include "vpn_tunnel.hpp"
#include <algorithm>
#include <charconv>
#include <ctime>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <winsvc.h>
#endif

namespace cysynck {
std::string vpn_apparmor_rules(const std::string &path) {
    // Quoting alone does not make AppArmor glob/variable syntax literal. Refuse
    // metacharacters rather than generating an unexpectedly broad permission.
    if (path.empty() || path.size() > 4096 || path.front() != '/' ||
        path.find_first_of("*?[]{}^@\"\\,\n\r") != std::string::npos ||
        path.find("//") != std::string::npos ||
        std::any_of(path.begin(), path.end(), [](unsigned char byte) { return byte < 32 || byte == 127; }) ||
        std::regex_search(path, std::regex("/\\.\\.?(/|$)")) ||
        !std::regex_search(path, std::regex("/(cs-[0-9a-f]{12}|cysynck)\\.conf$")))
        throw std::runtime_error("AppArmor export needs an absolute tunnel path without policy metacharacters");
    auto slash = path.rfind('/');
    return "\"" + path.substr(0, slash + 1) + "\" r,\n\"" + path + "\" r,\n";
}
namespace {
std::string trim(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
        value.pop_back();
    return value;
}
void logical_name(const std::string &value) {
    if (!std::regex_match(value, std::regex("(cs-[0-9a-f]{12}|cysynck)")))
        throw std::runtime_error("Invalid CySynck tunnel name");
}
std::string runtime_name(const std::string &logical, const fs::path &mapping_directory) {
    if (!mapping_directory.empty())
        return vpn_mapped_interface(logical, mapping_directory);
    // Local tool wrappers can have a private runtime directory. The mapping only
    // selects a candidate: inspect() still verifies the live interface public key.
    auto configured = env("CYSYNCK_WG_RUNTIME_DIRECTORY");
    if (!configured.empty()) {
        auto directory = fs::u8path(configured);
        if (!directory.is_absolute() || !fs::is_directory(directory) || path_is_link(directory))
            throw std::runtime_error("WireGuard runtime directory must be an absolute, unlinked directory");
        return vpn_mapped_interface(logical, directory);
    }
#ifdef __APPLE__
    return vpn_mapped_interface(logical, "/var/run/wireguard");
#else
    return logical;
#endif
}
std::set<std::string> interfaces(const std::string &tool) {
    auto result = process({tool, "show", "interfaces"}, {}, {}, 10);
    if (result.code)
        throw std::runtime_error(
            "Cannot inspect WireGuard interfaces; check tool availability and permissions");
    std::istringstream names(result.out);
    std::set<std::string> output;
    std::string name;
    while (names >> name) {
        if (output.size() >= 1024 || name.size() > 256 || name.find('\0') != std::string::npos)
            throw std::runtime_error("Invalid WireGuard interface inventory");
        output.insert(name);
    }
    return output;
}
Json service(const std::string &logical, const fs::path &configuration,
             const std::string &activation_tool) {
#ifdef _WIN32
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager)
        throw std::runtime_error("Cannot inspect the Windows WireGuard service manager");
    auto name = std::wstring(L"WireGuardTunnel$") + fs::u8path(logical).wstring();
    SC_HANDLE handle = OpenServiceW(manager, name.c_str(), SERVICE_QUERY_CONFIG);
    auto error = GetLastError();
    CloseServiceHandle(manager);
    if (!handle) {
        if (error == ERROR_SERVICE_DOES_NOT_EXIST)
            return {{"exists", false}, {"owned", false}};
        throw std::runtime_error(
            "Cannot inspect the Windows WireGuard tunnel service configuration");
    }
    DWORD bytes = 0;
    QueryServiceConfigW(handle, nullptr, 0, &bytes);
    if (!bytes || bytes > 65536) {
        CloseServiceHandle(handle);
        throw std::runtime_error("Invalid Windows tunnel service configuration");
    }
    std::vector<unsigned char> buffer(bytes);
    auto config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data());
    bool queried = QueryServiceConfigW(handle, config, bytes, &bytes) != 0;
    CloseServiceHandle(handle);
    if (!queried)
        throw std::runtime_error("Cannot read the Windows tunnel service configuration");
    return {{"exists", true},
            {"owned", vpn_windows_service_command_owned(config->lpBinaryPathName, configuration,
                                                        activation_tool)}};
#else
    (void)logical;
    (void)configuration;
    (void)activation_tool;
    return {{"exists", false}, {"owned", false}};
#endif
}
Json inspect(const std::string &tool, const std::string &logical, const std::string &public_key,
             const std::set<std::string> &names, const fs::path &mapping_directory) {
    auto runtime = runtime_name(logical, mapping_directory);
    bool present = !runtime.empty() && names.contains(runtime);
    bool owned = false;
    if (present) {
        auto result = process({tool, "show", runtime, "public-key"}, {}, {}, 10);
        if (result.code)
            throw std::runtime_error("Cannot verify the existing WireGuard tunnel identity");
        owned = trim(result.out) == public_key;
    }
    return {{"interface", logical},
            {"runtime_interface", runtime},
            {"present", present},
            {"owned", owned}};
}
} // namespace
#ifdef _WIN32
bool vpn_windows_service_command_owned(const std::wstring &command, const fs::path &configuration,
                                       const std::string &activation_tool) {
    int count = 0;
    auto arguments = CommandLineToArgvW(command.c_str(), &count);
    bool owned = false;
    if (arguments) {
        try {
            auto equal_path = [](const fs::path &a, const fs::path &b) {
                auto normalized = [](fs::path path) {
                    if (!path.is_absolute()) {
                        auto input = path.wstring();
                        auto size =
                            SearchPathW(nullptr, input.c_str(), L".exe", 0, nullptr, nullptr);
                        if (size && size < 32768) {
                            std::vector<wchar_t> buffer(size + 1);
                            auto written = SearchPathW(nullptr, input.c_str(), L".exe",
                                                       static_cast<DWORD>(buffer.size()),
                                                       buffer.data(), nullptr);
                            if (written && written < buffer.size())
                                path = fs::path(buffer.data());
                        }
                    }
                    auto full = fs::weakly_canonical(fs::absolute(path)).wstring();
                    auto size = GetLongPathNameW(full.c_str(), nullptr, 0);
                    if (size && size < 32768) {
                        std::vector<wchar_t> buffer(size + 1);
                        auto written = GetLongPathNameW(full.c_str(), buffer.data(),
                                                        static_cast<DWORD>(buffer.size()));
                        if (written && written < buffer.size())
                            full = buffer.data();
                    }
                    return full;
                };
                auto left = normalized(a), right = normalized(b);
                return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) ==
                       CSTR_EQUAL;
            };
            owned = count == 3 && std::wstring(arguments[1]) == L"/tunnelservice" &&
                    equal_path(arguments[0], fs::u8path(activation_tool)) &&
                    equal_path(arguments[2], configuration);
        } catch (...) {
            LocalFree(arguments);
            throw;
        }
        LocalFree(arguments);
    }
    return owned;
}
#endif

std::string vpn_interface_name(const std::string &peer_id) {
    if (peer_id.empty() || peer_id.size() > 80)
        throw std::runtime_error("Invalid local peer identity");
    return "cs-" + sha256(peer_id).substr(0, 12);
}
std::string vpn_mapped_interface(const std::string &logical, const fs::path &mapping_directory) {
    logical_name(logical);
    auto file = mapping_directory / (logical + ".name");
    if (!fs::exists(file))
        return "";
    if (path_is_link(file) || !fs::is_regular_file(file))
        throw std::runtime_error("WireGuard runtime mapping is linked or invalid");
    auto name = trim(read_file(file, 64));
    if (!std::regex_match(name, std::regex("utun[0-9]{1,5}")))
        throw std::runtime_error("Invalid macOS WireGuard runtime interface mapping");
    return name;
}
Json vpn_tunnel_status(const std::string &tool, const std::string &logical,
                       const std::string &public_key, const fs::path &configuration,
                       const fs::path &mapping_directory) {
    logical_name(logical);
    auto names = interfaces(tool);
    auto result = inspect(tool, logical, public_key, names, mapping_directory);
    auto legacy = inspect(tool, "cysynck", public_key, names, mapping_directory);
    legacy["configuration_present"] =
        fs::is_regular_file(configuration.parent_path() / "cysynck.conf") &&
        !path_is_link(configuration.parent_path() / "cysynck.conf");
    result["legacy"] = legacy;
    result["handshakes"] = Json::array();
    auto active = result.at("present").get<bool>() ? result : legacy;
    if (active.at("present") == true && active.at("owned") == true) {
        auto response = process(
            {tool, "show", active.at("runtime_interface").get<std::string>(), "latest-handshakes"},
            {}, {}, 10);
        if (response.code)
            throw std::runtime_error("Cannot inspect owned WireGuard handshakes");
        std::istringstream rows(response.out);
        std::string line;
        while (std::getline(rows, line)) {
            std::istringstream row(line);
            std::string key, extra;
            long long time;
            if (!(row >> key >> time) || (row >> extra) || result["handshakes"].size() >= 4096 ||
                time < 0 ||
                !std::regex_match(key, std::regex("[A-Za-z0-9+/]{42}[AEIMQUYcgkosw048]=")))
                throw std::runtime_error("Invalid WireGuard handshake inventory");
            auto now = std::time(nullptr);
            result["handshakes"].push_back(
                {{"public_key", key},
                 {"timestamp", time},
                 {"recent", time > 0 && time <= now && now - time < 180}});
        }
    }
    return result;
}
uint16_t vpn_owned_listen_port(const std::string &tool, const std::string &activation_tool,
                               const std::string &logical, const std::string &public_key,
                               const fs::path &configuration) {
    logical_name(logical);
    auto current = inspect(tool, logical, public_key, interfaces(tool), {});
    auto configured_service = service(logical, configuration, activation_tool);
    if (current.at("present") != true || current.at("owned") != true ||
        (configured_service.at("exists") == true && configured_service.at("owned") != true))
        throw std::runtime_error("Router mapping requires this profile's owned live WireGuard tunnel");
    auto response = process({tool, "show", current.at("runtime_interface"), "listen-port"}, {}, {}, 10);
    auto text = trim(response.out);
    unsigned port = 0;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), port);
    if (response.code || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !port || port > 65535 || text != std::to_string(port))
        throw std::runtime_error("Cannot identify the owned WireGuard UDP listener");
    return static_cast<uint16_t>(port);
}
Json vpn_tunnel_control(const std::string &action, const std::string &tool,
                        const std::string &activation_tool, const std::string &logical,
                        const std::string &public_key, const fs::path &configuration,
                        const std::string &safe_configuration, const fs::path &mapping_directory,
                        const std::function<Json(const std::string &, const fs::path &,
                                                 const std::string &)> &service_query) {
    logical_name(logical);
    if (action != "up" && action != "down")
        throw std::runtime_error("Unknown WireGuard tunnel action");
    auto names = interfaces(tool);
    auto current = inspect(tool, logical, public_key, names, mapping_directory);
    auto query = [&](const std::string &name, const fs::path &path) {
        return service_query ? service_query(name, path, activation_tool)
                             : service(name, path, activation_tool);
    };
    auto current_service = query(logical, configuration);
    if ((current.at("present") == true && current.at("owned") != true) ||
        (current_service.at("exists") == true && current_service.at("owned") != true))
        throw std::runtime_error("Preserve the existing foreign WireGuard tunnel or service");
    auto actual_logical = logical;
    auto actual_config = configuration;
    if (current.at("present") != true && current_service.at("exists") != true) {
        auto legacy_config = configuration.parent_path() / "cysynck.conf";
        auto legacy = inspect(tool, "cysynck", public_key, names, mapping_directory);
        auto legacy_service = query("cysynck", legacy_config);
        bool legacy_live = legacy.at("present") == true && legacy.at("owned") == true;
        bool legacy_owned_service =
            legacy_service.at("exists") == true && legacy_service.at("owned") == true;
        if (legacy_live || legacy_owned_service) {
            if (legacy_service.at("exists") == true && legacy_service.at("owned") != true)
                throw std::runtime_error("Preserve the foreign legacy WireGuard service");
            if (action == "up")
                throw std::runtime_error("Stop the owned legacy tunnel with vpn down before "
                                         "starting the profile-specific tunnel");
            if ((!legacy_owned_service &&
                 (!fs::is_regular_file(legacy_config) || path_is_link(legacy_config))) ||
                (legacy.at("present") == true && legacy.at("owned") != true))
                throw std::runtime_error(
                    "Cannot verify ownership of the legacy WireGuard configuration");
            actual_logical = "cysynck";
            actual_config = legacy_config;
            current = legacy;
            current_service = legacy_service;
        }
    }
    bool exists = current.at("present") == true || current_service.at("exists") == true;
    if ((action == "down" && !exists) || (action == "up" && exists))
        return {{"requested", action},
                {"interface", actual_logical},
                {"changed", false},
                {"already_up", action == "up" && current.at("present") == true},
                {"already_down", action == "down"},
                {"service_exists", current_service.at("exists")},
                {"configuration_applied", false}};
    if (path_is_link(actual_config))
        throw std::runtime_error("WireGuard configuration must not be linked");
    atomic_write(actual_config, safe_configuration, true);
    std::vector<std::string> arguments;
#ifdef _WIN32
    arguments =
        action == "up"
            ? std::vector<std::string>{activation_tool, "/installtunnelservice",
                                       path_string(actual_config)}
            : std::vector<std::string>{activation_tool, "/uninstalltunnelservice", actual_logical};
#else
    arguments = {activation_tool, action, path_string(actual_config)};
#endif
    if (process(arguments, {}, {}, 45).code)
        throw std::runtime_error(
            "WireGuard activation failed; check administrator/root permissions and WireGuard logs");
    return {{"requested", action}, {"interface", actual_logical}, {"changed", true}};
}
Json vpn_set_peer(const std::string &tool, const std::string &activation_tool,
                  const std::string &logical, const std::string &public_key,
                  const fs::path &configuration, const std::string &peer_key,
                  const std::string &address, const std::string &endpoint, bool remove,
                  bool clear_endpoint, const fs::path &mapping_directory,
                  const std::string &safe_configuration) {
    logical_name(logical);
    if (!std::regex_match(peer_key, std::regex("[A-Za-z0-9+/]{42}[AEIMQUYcgkosw048]=")))
        throw std::runtime_error("Invalid WireGuard peer key");
    if (!remove) {
        std::smatch match;
        if (!std::regex_match(address, match, std::regex("10\\.77\\.([0-9]{1,3})\\.([0-9]{1,3})")) ||
            std::stoi(match[1]) > 255 || std::stoi(match[2]) > 255 ||
            address != "10.77." + std::to_string(std::stoi(match[1])) + "." + std::to_string(std::stoi(match[2])))
            throw std::runtime_error("Invalid WireGuard peer host address");
        if (!endpoint.empty() &&
            (!std::regex_match(endpoint, match, std::regex("([a-zA-Z0-9.-]+|\\[[a-fA-F0-9:]+\\]):([0-9]{1,5})")) ||
             std::stoi(match[2]) < 1 || std::stoi(match[2]) > 65535))
            throw std::runtime_error("Invalid WireGuard peer endpoint");
    }
    auto current = inspect(tool, logical, public_key, interfaces(tool), mapping_directory);
    auto configured_service = service(logical, configuration, activation_tool);
    if ((current.at("present") == true && current.at("owned") != true) ||
        (configured_service.at("exists") == true && configured_service.at("owned") != true))
        throw std::runtime_error("Preserve the foreign WireGuard tunnel or service");
    // Keep a service restart from restoring a revoked peer or expired endpoint.
    // Only the owning caller's regenerated, key-checked configuration is accepted here.
    if (!safe_configuration.empty()) {
        if (safe_configuration.size() > 128 * 1024 || path_is_link(configuration))
            throw std::runtime_error("Invalid owned WireGuard configuration update");
        atomic_write(configuration, safe_configuration, true);
    }
    if (current.at("present") != true)
        return {{"active", false}, {"changed", false}};
    auto runtime = current.at("runtime_interface").get<std::string>();
    // Removing then adding clears a previous configured/roamed endpoint on expiry.
    // If addition fails, the peer stays disconnected and the caller retries safely.
    if (remove || clear_endpoint) {
        if (process({tool, "set", runtime, "peer", peer_key, "remove"}, {}, {}, 10).code)
            throw std::runtime_error("Cannot remove the peer from the owned WireGuard tunnel");
        if (remove) return {{"active", true}, {"changed", true}};
    }
    std::vector<std::string> args{tool, "set", runtime, "peer", peer_key, "allowed-ips", address + "/32",
                                  "persistent-keepalive", endpoint.empty() ? "0" : "25"};
    if (!endpoint.empty()) {
        args.push_back("endpoint");
        args.push_back(endpoint);
    }
    if (process(args, {}, {}, 10).code)
        throw std::runtime_error("Cannot update the peer on the owned WireGuard tunnel");
    return {{"active", true}, {"changed", true}};
}
} // namespace cysynck
