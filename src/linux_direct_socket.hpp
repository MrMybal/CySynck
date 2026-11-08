#pragma once
#ifdef __linux__
#include <string>
namespace cysynck {
struct LinuxDirectInterface {
    std::string name;
    unsigned index = 0;
    bool loopback = false;
};
LinuxDirectInterface linux_direct_interface(const std::string &local_address,
                                            const std::string &profile_peer_id,
                                            const std::string &remote_address = "");
void bind_linux_direct_socket(int socket, const LinuxDirectInterface &interface);
} // namespace cysynck
#endif
