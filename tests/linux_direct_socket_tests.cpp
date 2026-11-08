#include "linux_direct_socket.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
using namespace cysynck;
int main(int argc, char **argv) {
    int descriptor = -1;
    try {
        if (argc != 1 && argc != 5)
            throw std::runtime_error("Expected local IPv4, profile peer ID, remote IPv4 and port");
        auto local = argc == 1 ? std::string("127.0.0.1") : std::string(argv[1]);
        auto peer = argc == 1 ? std::string("loopback-fixture") : std::string(argv[2]);
        auto remote = argc == 1 ? std::string("127.0.0.1") : std::string(argv[3]);
        auto interface = linux_direct_interface(local, peer, remote);
        descriptor = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
        if (descriptor < 0)
            throw std::runtime_error("Cannot reserve probe socket");
        bind_linux_direct_socket(descriptor, interface);
        if (argc == 1) {
            bool refused = false;
            try {
                linux_direct_interface("127.0.0.1", peer, "198.18.0.1");
            } catch (const std::exception &) {
                refused = true;
            }
            if (!refused)
                throw std::runtime_error("Loopback probe could select an external peer");
        } else {
            timeval timeout{5, 0};
            if (setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
                setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)))
                throw std::runtime_error("Cannot bound probe socket deadlines");
            sockaddr_in source{}, target{};
            source.sin_family = target.sin_family = AF_INET;
            auto port = std::stoi(argv[4]);
            if (port < 1 || port > 65535 ||
                inet_pton(AF_INET, local.c_str(), &source.sin_addr) != 1 ||
                inet_pton(AF_INET, remote.c_str(), &target.sin_addr) != 1)
                throw std::runtime_error("Invalid probe address/port");
            target.sin_port = htons(static_cast<uint16_t>(port));
            if (bind(descriptor, reinterpret_cast<sockaddr *>(&source), sizeof(source)) ||
                connect(descriptor, reinterpret_cast<sockaddr *>(&target), sizeof(target)))
                throw std::runtime_error(
                    "The verified interface could not connect to the real kernel peer");
            std::string message = "CySynck direct probe", received(message.size(), '\0');
            for (size_t offset = 0; offset < message.size();) {
                auto sent = send(descriptor, message.data() + offset, message.size() - offset,
                                 MSG_NOSIGNAL);
                if (sent <= 0)
                    throw std::runtime_error("Bound probe send failed");
                offset += static_cast<size_t>(sent);
            }
            for (size_t offset = 0; offset < received.size();) {
                auto size = recv(descriptor, received.data() + offset, received.size() - offset, 0);
                if (size <= 0)
                    throw std::runtime_error("Bound probe reply failed");
                offset += static_cast<size_t>(size);
            }
            if (received != message)
                throw std::runtime_error("Bound probe bytes differ");
        }
        close(descriptor);
        std::cout << "PASS actual interface-bound TCP without a privileged WireGuard query\n";
        return 0;
    } catch (const std::exception &error) {
        if (descriptor >= 0)
            close(descriptor);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
