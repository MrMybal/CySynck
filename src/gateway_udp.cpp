#include "gateway_udp.hpp"
#include <array>
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace cysynck {
struct GatewayUdp::Impl {
#ifdef _WIN32
    SOCKET socket = INVALID_SOCKET;
    bool initialized = false;
#else
    int socket = -1;
#endif
    std::string source;
    Impl(const std::string &gateway, uint16_t port) {
        if (gateway.empty() || gateway.size() > 15 || gateway.find('\0') != std::string::npos ||
            !port)
            throw std::runtime_error("Port mapping requires an explicit IPv4 gateway and UDP port");
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data))
            throw std::runtime_error("Cannot initialize port-mapping sockets");
        initialized = true;
#endif
        try {
            sockaddr_in endpoint{};
            endpoint.sin_family = AF_INET;
            endpoint.sin_port = htons(port);
            if (inet_pton(AF_INET, gateway.c_str(), &endpoint.sin_addr) != 1)
                throw std::runtime_error("Port-mapping gateway must be a literal IPv4 address");
            socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
            if (socket == INVALID_SOCKET)
#else
            if (socket < 0)
#endif
                throw std::runtime_error("Cannot create gateway UDP socket");
#ifndef _WIN32
            if (socket >= FD_SETSIZE)
                throw std::runtime_error("Gateway UDP socket exceeds the select descriptor limit");
#endif
            if (::connect(socket, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)))
                throw std::runtime_error("Cannot reach the selected port-mapping gateway");
            sockaddr_in local{};
#ifdef _WIN32
            int length = sizeof(local);
#else
            socklen_t length = sizeof(local);
#endif
            std::array<char, INET_ADDRSTRLEN> address{};
            if (getsockname(socket, reinterpret_cast<sockaddr *>(&local), &length) ||
                !inet_ntop(AF_INET, &local.sin_addr, address.data(), address.size()))
                throw std::runtime_error("Cannot identify the local gateway-facing IPv4 address");
            source = address.data();
        } catch (...) {
            close();
            throw;
        }
    }
    void close() {
#ifdef _WIN32
        if (socket != INVALID_SOCKET) {
            closesocket(socket);
            socket = INVALID_SOCKET;
        }
        if (initialized) {
            WSACleanup();
            initialized = false;
        }
#else
        if (socket >= 0) {
            ::close(socket);
            socket = -1;
        }
#endif
    }
    ~Impl() { close(); }
};
GatewayUdp::GatewayUdp(const std::string &gateway, uint16_t port)
    : impl_(std::make_unique<Impl>(gateway, port)) {}
GatewayUdp::~GatewayUdp() = default;
std::string GatewayUdp::local_address() const { return impl_->source; }
GatewayPacket GatewayUdp::request(const GatewayPacket &packet,
                                  const std::function<bool(const GatewayPacket &)> &accept,
                                  const std::vector<int> &retry_intervals_ms) {
    if (packet.empty() || packet.size() > 1100)
        throw std::runtime_error("Invalid gateway UDP request length");
    if (retry_intervals_ms.empty() || retry_intervals_ms.size() > 9)
        throw std::runtime_error("Invalid gateway retransmission schedule");
    for (int milliseconds : retry_intervals_ms) {
        if (milliseconds < 1 || milliseconds > 64000)
            throw std::runtime_error("Invalid gateway retransmission interval");
        if (::send(impl_->socket, reinterpret_cast<const char *>(packet.data()),
                   static_cast<int>(packet.size()), 0) != static_cast<int>(packet.size()))
            throw std::runtime_error("Gateway UDP request could not be sent");
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        while (std::chrono::steady_clock::now() < deadline) {
            auto left = std::chrono::duration_cast<std::chrono::microseconds>(
                            deadline - std::chrono::steady_clock::now())
                            .count();
            if (left <= 0)
                break;
            timeval timeout{};
            timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(left / 1000000);
            timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>(left % 1000000);
            fd_set input;
            FD_ZERO(&input);
            FD_SET(impl_->socket, &input);
#ifdef _WIN32
            auto ready = select(0, &input, nullptr, nullptr, &timeout);
#else
            auto ready = select(impl_->socket + 1, &input, nullptr, nullptr, &timeout);
#endif
            if (ready < 0)
                throw std::runtime_error("Gateway UDP response wait failed");
            if (!ready)
                break;
            std::array<char, 1101> buffer{};
            auto received =
                ::recv(impl_->socket, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (received < 0)
                throw std::runtime_error("Gateway UDP response could not be received");
            if (received > 1100)
                throw std::runtime_error("Gateway UDP response exceeds the protocol limit");
            GatewayPacket response(buffer.begin(), buffer.begin() + received);
            if (accept(response))
                return response;
        }
    }
    throw std::runtime_error("Port-mapping gateway did not respond within the operation deadline");
}
} // namespace cysynck
