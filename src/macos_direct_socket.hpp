#pragma once
#ifdef __APPLE__
#include <cysynck/core.hpp>
namespace cysynck {
struct MacosDirectInterface {
    std::string local_address, peer_id, public_key, runtime_name;
    unsigned index = 0;
    bool loopback = false;
    Json attestation;
};
// Local privileged tunnel control publishes public metadata in the root-owned
// WireGuard runtime directory. Data hosts never read the private UAPI socket.
void authorize_macos_direct_interface(const std::string &local_address, const std::string &peer_id,
                                      const std::string &public_key, const std::string &tool,
                                      const fs::path &configuration);
void forget_macos_direct_interface(const std::string &peer_id);
MacosDirectInterface macos_direct_interface(const std::string &local_address,
                                            const std::string &peer_id,
                                            const std::string &public_key,
                                            const std::string &remote_address = "");
void bind_macos_direct_socket(int socket, const MacosDirectInterface &binding);
} // namespace cysynck
#endif
