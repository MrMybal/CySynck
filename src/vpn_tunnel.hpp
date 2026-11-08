#pragma once
#include <cysynck/core.hpp>
#include <functional>
namespace cysynck {
std::string vpn_interface_name(const std::string &peer_id);
std::string vpn_mapped_interface(const std::string &logical, const fs::path &mapping_directory);
Json vpn_tunnel_status(const std::string &tool, const std::string &logical,
                       const std::string &public_key, const fs::path &configuration,
                       const fs::path &mapping_directory = {});
Json vpn_tunnel_control(const std::string &action, const std::string &tool,
                        const std::string &activation_tool, const std::string &logical,
                        const std::string &public_key, const fs::path &configuration,
                        const std::string &safe_configuration,
                        const fs::path &mapping_directory = {},
                        const std::function<Json(const std::string &, const fs::path &,
                                                 const std::string &)> &service_query = {});
#ifdef _WIN32
bool vpn_windows_service_command_owned(const std::wstring &command, const fs::path &configuration,
                                       const std::string &activation_tool);
#endif
} // namespace cysynck
