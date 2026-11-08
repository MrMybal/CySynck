#pragma once
#ifdef _WIN32
#include <cstdint>
#include <string>
namespace cysynck {
struct WindowsDirectInterface {
    std::string local_address, logical_name;
    uint32_t index = 0;
    uint64_t luid = 0;
    bool loopback = false;
};
WindowsDirectInterface windows_direct_interface(const std::string &local_address,
                                                const std::string &profile_peer_id,
                                                const std::string &remote_address = "");
void bind_windows_direct_socket(uintptr_t socket, const WindowsDirectInterface &binding);
} // namespace cysynck
#endif
