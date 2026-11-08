#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include "windows_direct_socket.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace cysynck;
namespace {
struct Socket {
    SOCKET value = INVALID_SOCKET;
    ~Socket() {
        if (value != INVALID_SOCKET)
            closesocket(value);
    }
};
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void timeout(SOCKET socket) {
    DWORD milliseconds = 5000;
    check(!setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&milliseconds),
                      sizeof(milliseconds)) &&
              !setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char *>(&milliseconds),
                          sizeof(milliseconds)),
          "Cannot bound the direct probe socket deadline");
}
} // namespace
int main(int argc, char **argv) {
    WSADATA startup{};
    if (WSAStartup(MAKEWORD(2, 2), &startup))
        return 1;
    int result = 1;
    try {
        if (argc == 4 && std::string(argv[1]) == "--probe") {
            auto binding = windows_direct_interface(argv[2], argv[3]);
            check(!binding.loopback, "The actual WireGuardNT probe selected loopback");
            Socket socket{::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
            check(socket.value != INVALID_SOCKET, "Cannot reserve actual adapter probe socket");
            bind_windows_direct_socket(socket.value, binding);
            std::cout << "PASS actual official WireGuardNT adapter inventory and socket binding\n";
        } else {
            check(argc == 1, "Use --probe LOCAL_IPV4 PROFILE_PEER_ID or no arguments");
            auto binding = windows_direct_interface("127.0.0.1", "loopback-fixture", "127.0.0.1");
            Socket listener{::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
            check(listener.value != INVALID_SOCKET, "Cannot reserve loopback listener");
            bind_windows_direct_socket(listener.value, binding);
            sockaddr_in endpoint{};
            endpoint.sin_family = AF_INET;
            InetPtonA(AF_INET, "127.0.0.1", &endpoint.sin_addr);
            check(
                !bind(listener.value, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)) &&
                    !listen(listener.value, 1),
                "Cannot bind the isolated loopback listener");
            int size = sizeof(endpoint);
            check(!getsockname(listener.value, reinterpret_cast<sockaddr *>(&endpoint), &size),
                  "Cannot locate loopback listener");
            Socket client{::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
            check(client.value != INVALID_SOCKET, "Cannot reserve loopback client");
            timeout(client.value);
            bind_windows_direct_socket(client.value, binding);
            check(!connect(client.value, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)),
                  "Interface-bound TCP connection failed");
            Socket accepted{accept(listener.value, nullptr, nullptr)};
            check(accepted.value != INVALID_SOCKET, "Cannot accept interface-bound TCP");
            timeout(accepted.value);
            DWORD inherited = 0;
            size = sizeof(inherited);
            check(!getsockopt(accepted.value, IPPROTO_IP, IP_UNICAST_IF,
                              reinterpret_cast<char *>(&inherited), &size) &&
                      inherited == binding.index,
                  "Accepted TCP did not inherit the listener's outgoing interface binding");
            check(send(accepted.value, "W", 1, 0) == 1, "Bound listener reply failed");
            char reply = 0;
            check(recv(client.value, &reply, 1, 0) == 1 && reply == 'W',
                  "Bound TCP reply bytes differ");
            bool external_refused = false;
            try {
                windows_direct_interface("127.0.0.1", "loopback-fixture", "198.18.0.1");
            } catch (const std::exception &) {
                external_refused = true;
            }
            check(external_refused, "A loopback direct socket could select an external peer");
            ULONG length = 0;
            check(GetAdaptersAddresses(AF_INET, 0, nullptr, nullptr, &length) ==
                          ERROR_BUFFER_OVERFLOW &&
                      length <= 1024 * 1024,
                  "Cannot size the ordinary adapter negative probe");
            std::vector<unsigned char> bytes(length);
            check(GetAdaptersAddresses(AF_INET, 0, nullptr,
                                       reinterpret_cast<IP_ADAPTER_ADDRESSES *>(bytes.data()),
                                       &length) == NO_ERROR,
                  "Cannot inspect the ordinary adapter negative probe");
            bool ordinary_refused = false;
            for (auto adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(bytes.data());
                 adapter && !ordinary_refused; adapter = adapter->Next) {
                if (adapter->OperStatus != IfOperStatusUp ||
                    adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
                    adapter->IfType == IF_TYPE_PROP_VIRTUAL)
                    continue;
                for (auto ip = adapter->FirstUnicastAddress; ip && !ordinary_refused;
                     ip = ip->Next) {
                    if (!ip->Address.lpSockaddr || ip->Address.lpSockaddr->sa_family != AF_INET)
                        continue;
                    char address[INET_ADDRSTRLEN]{};
                    auto value = reinterpret_cast<sockaddr_in *>(ip->Address.lpSockaddr);
                    if (!InetNtopA(AF_INET, &value->sin_addr, address, sizeof(address)))
                        continue;
                    try {
                        windows_direct_interface(address, "ordinary-adapter-negative-probe");
                    } catch (const std::exception &) {
                        ordinary_refused = true;
                    }
                }
            }
            check(ordinary_refused,
                  "No real ordinary adapter was rejected by the WireGuard-only policy");
            std::cout << "PASS actual Windows TCP interface binding, accepted-socket inheritance, "
                         "replies and ordinary-adapter refusal\n";
        }
        result = 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
    }
    WSACleanup();
    return result;
}
