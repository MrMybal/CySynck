#include "macos_direct_socket.hpp"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <iostream>
#include <net/if.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
using namespace cysynck;
namespace {
struct Socket {
    int value = -1;
    ~Socket() {
        if (value >= 0)
            close(value);
    }
};
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 5 && std::string(argv[1]) == "--probe") {
            auto binding = macos_direct_interface(argv[2], argv[3], argv[4]);
            check(!binding.loopback, "The actual WireGuard probe selected loopback");
            Socket socket{::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
            check(socket.value >= 0, "Cannot reserve macOS probe socket");
            bind_macos_direct_socket(socket.value, binding);
            std::cout << "PASS actual authorized macOS WireGuard socket without a privileged UAPI "
                         "query\n";
        } else {
            check(argc == 1, "Use --probe LOCAL_IPV4 PROFILE_PEER_ID PUBLIC_KEY or no arguments");
            auto binding = macos_direct_interface("127.0.0.1", "loopback-fixture", "", "127.0.0.1");
            Socket listener{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
            check(listener.value >= 0, "Cannot reserve macOS loopback listener");
            bind_macos_direct_socket(listener.value, binding);
            sockaddr_in endpoint{};
            endpoint.sin_len = sizeof(endpoint);
            endpoint.sin_family = AF_INET;
            inet_pton(AF_INET, "127.0.0.1", &endpoint.sin_addr);
            check(
                !bind(listener.value, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)) &&
                    !listen(listener.value, 1),
                "Cannot listen on the bound macOS loopback interface");
            socklen_t size = sizeof(endpoint);
            check(!getsockname(listener.value, reinterpret_cast<sockaddr *>(&endpoint), &size),
                  "Cannot locate macOS loopback port");
            Socket client{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
            check(client.value >= 0, "Cannot reserve macOS loopback client");
            bind_macos_direct_socket(client.value, binding);
            timeval timeout{5, 0};
            setsockopt(client.value, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            check(!connect(client.value, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)),
                  "Bound macOS TCP connection failed");
            Socket accepted{accept(listener.value, nullptr, nullptr)};
            check(accepted.value >= 0, "Cannot accept bound macOS TCP");
            unsigned inherited = 0;
            size = sizeof(inherited);
            check(!getsockopt(accepted.value, IPPROTO_IP, IP_BOUND_IF, &inherited, &size) &&
                      inherited == binding.index,
                  "Accepted macOS TCP did not inherit the listener interface binding");
            check(send(accepted.value, "M", 1, 0) == 1, "Bound macOS listener reply failed");
            char reply = 0;
            check(recv(client.value, &reply, 1, 0) == 1 && reply == 'M',
                  "Bound macOS TCP reply differs");
            bool external_refused = false;
            try {
                macos_direct_interface("127.0.0.1", "loopback-fixture", "", "198.18.0.1");
            } catch (const std::exception &) {
                external_refused = true;
            }
            check(external_refused, "macOS loopback could select an external peer");
            ifaddrs *raw = nullptr;
            check(!getifaddrs(&raw), "Cannot inspect ordinary macOS adapter negative");
            std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> addresses(raw, freeifaddrs);
            bool ordinary_refused = false;
            for (auto ip = raw; ip && !ordinary_refused; ip = ip->ifa_next) {
                if (!ip->ifa_addr || ip->ifa_addr->sa_family != AF_INET ||
                    !(ip->ifa_flags & IFF_UP) || (ip->ifa_flags & IFF_LOOPBACK))
                    continue;
                char address[INET_ADDRSTRLEN]{};
                auto value = reinterpret_cast<sockaddr_in *>(ip->ifa_addr);
                if (!inet_ntop(AF_INET, &value->sin_addr, address, sizeof(address)))
                    continue;
                try {
                    macos_direct_interface(address, "ordinary-adapter-negative-probe", "");
                } catch (const std::exception &) {
                    ordinary_refused = true;
                }
            }
            check(ordinary_refused, "An ordinary macOS adapter was not refused");
            std::cout << "PASS actual macOS bound TCP, accepted inheritance, reply and ordinary "
                         "adapter refusal\n";
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
