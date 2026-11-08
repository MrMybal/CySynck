#include "nat_pmp.hpp"
#include "gateway_mapping.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
using Length = int;
constexpr Socket invalid_socket = INVALID_SOCKET;
void close_socket(Socket value) { closesocket(value); }
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
using Length = socklen_t;
constexpr Socket invalid_socket = -1;
void close_socket(Socket value) { ::close(value); }
#endif
using namespace cysynck;
using Bytes = std::vector<unsigned char>;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void refuses(const std::function<void()> &action, const char *message) {
    bool refused = false;
    try {
        action();
    } catch (const std::exception &) {
        refused = true;
    }
    check(refused, message);
}
uint16_t number16(const Bytes &value, size_t at) {
    return (uint16_t(value.at(at)) << 8) | value.at(at + 1);
}
uint32_t number32(const Bytes &value, size_t at) {
    return (uint32_t(number16(value, at)) << 16) | number16(value, at + 2);
}
void append16(Bytes &value, uint16_t number) {
    value.push_back(number >> 8);
    value.push_back(number & 255);
}
void append32(Bytes &value, uint32_t number) {
    append16(value, number >> 16);
    append16(value, number & 65535);
}
class Gateway {
    Socket socket_ = invalid_socket;
    std::thread worker_;
    std::atomic<bool> stopped_{false}, fault_{false};
    std::mutex mutex_;
    std::vector<Bytes> packets_;

  public:
    uint16_t port = 0;
    explicit Gateway(std::string mode = "normal") {
        socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        check(socket_ != invalid_socket, "Cannot create loopback gateway fixture");
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_port = 0;
        inet_pton(AF_INET, "127.0.0.1", &local.sin_addr);
        check(::bind(socket_, reinterpret_cast<sockaddr *>(&local), sizeof(local)) == 0,
              "Cannot bind isolated loopback gateway");
        Length length = sizeof(local);
        check(::getsockname(socket_, reinterpret_cast<sockaddr *>(&local), &length) == 0,
              "Cannot locate loopback gateway port");
        port = ntohs(local.sin_port);
        worker_ = std::thread([this, mode] {
            try {
                size_t address_attempts = 0;
                while (!stopped_) {
                    fd_set input;
                    FD_ZERO(&input);
                    FD_SET(socket_, &input);
                    timeval timeout{0, 50000};
#ifdef _WIN32
                    auto ready = select(0, &input, nullptr, nullptr, &timeout);
#else
                    auto ready = select(socket_ + 1, &input, nullptr, nullptr, &timeout);
#endif
                    if (!ready)
                        continue;
                    if (ready < 0)
                        throw std::runtime_error("Gateway fixture wait failed");
                    std::array<char, 64> buffer{};
                    sockaddr_in remote{};
                    Length length = sizeof(remote);
                    auto size = recvfrom(socket_, buffer.data(), static_cast<int>(buffer.size()), 0,
                                         reinterpret_cast<sockaddr *>(&remote), &length);
                    if (size < 0)
                        throw std::runtime_error("Gateway fixture receive failed");
                    Bytes request(buffer.begin(), buffer.begin() + size);
                    {
                        std::lock_guard lock(mutex_);
                        packets_.push_back(request);
                    }
                    if (!request.empty() && request[0] == 2) {
                        auto send_pcp = [&](const Bytes &packet) {
                            check(sendto(socket_, reinterpret_cast<const char *>(packet.data()),
                                         static_cast<int>(packet.size()), 0,
                                         reinterpret_cast<sockaddr *>(&remote),
                                         sizeof(remote)) == static_cast<int>(packet.size()),
                                  "PCP fixture response failed");
                        };
                        check(request.size() == 60 && request[1] == 1 && request[36] == 17 &&
                                  number16(request, 40) == 51820 && request[18] == 255 &&
                                  request[19] == 255 && request[20] == 127 && request[23] == 1,
                              "Invalid PCP UDP MAP request or client IPv4-mapped address");
                        if (mode == "legacy") {
                            send_pcp({0, 129, 0, 1, 0, 0, 0, 100});
                            continue;
                        }
                        if (mode == "pcp-silent")
                            continue;
                        check(mode.starts_with("pcp-"),
                              "Client tried an unrequested PCP negotiation");
                        auto lifetime = number32(request, 4);
                        Bytes response{2, 129, 0,
                                       static_cast<unsigned char>(mode == "pcp-denied" ? 2 : 0)};
                        append32(response, lifetime ? 300 : 0);
                        append32(response, 100);
                        response.resize(24, 0);
                        response.insert(response.end(), request.begin() + 24, request.begin() + 36);
                        response.insert(response.end(), {17, 0, 0, 0});
                        append16(response, 51820);
                        append16(response, lifetime ? 45001 : 0);
                        response.insert(response.end(), 10, 0);
                        response.insert(response.end(), {255, 255, 198, 18, 0, 11});
                        if (!lifetime)
                            std::fill(response.begin() + 44, response.end(), 0);
                        if (mode == "pcp-short")
                            response.resize(40);
                        if (mode == "pcp-options")
                            response.insert(response.end(), {128, 0, 0, 1, 42, 0, 0, 0});
                        if (mode == "pcp-mandatory")
                            response.insert(response.end(), {1, 0, 0, 0});
                        if (mode == "pcp-bad-option")
                            response.insert(response.end(), {128, 0, 255, 255});
                        if (mode == "pcp-wrong-nonce" || mode == "pcp-only-wrong") {
                            auto wrong = response;
                            wrong[24] ^= 1;
                            send_pcp(wrong);
                            if (mode == "pcp-only-wrong")
                                continue;
                        }
                        if (mode == "pcp-wrong-fields") {
                            auto wrong = response;
                            wrong[36] = 6;
                            send_pcp(wrong);
                        }
                        send_pcp(response);
                        continue;
                    }
                    check(request.size() == 2 || request.size() == 12,
                          "Client sent an invalid request length");
                    check(request[0] == 0, "Client changed protocol without consent");
                    bool address = request[1] == 0;
                    check(address || request[1] == 1,
                          "Client requested a TCP mapping instead of UDP");
                    if (mode == "silent")
                        continue;
                    if (address && mode == "drop-first" && address_attempts++ == 0)
                        continue;
                    Bytes response{static_cast<unsigned char>(mode == "version" ? 1 : 0),
                                   static_cast<unsigned char>(128 + request[1])};
                    append16(response, mode == "denied" ? 2 : 0);
                    append32(response, !address && mode == "reboot" ? 50 : 100);
                    if (address)
                        response.insert(response.end(), {static_cast<unsigned char>(
                                                             mode == "bad-address" ? 127 : 198),
                                                         18, 0, 10});
                    else {
                        check(request[2] == 0 && request[3] == 0, "Reserved bytes were not zero");
                        auto lifetime = number32(request, 8);
                        check(number16(request, 4) == 51820,
                              "Client requested a different internal port");
                        if (!lifetime)
                            check(number16(request, 6) == 0,
                                  "Deletion did not clear the suggested external port");
                        append16(response, mode == "wrong-port" ? 51821 : 51820);
                        append16(response, lifetime ? 45000 : 0);
                        append32(response, lifetime ? 300 : 0);
                    }
                    if (mode == "short" && !address)
                        response.pop_back();
                    auto send = [&](Socket socket, const Bytes &packet) {
                        check(sendto(socket, reinterpret_cast<const char *>(packet.data()),
                                     static_cast<int>(packet.size()), 0,
                                     reinterpret_cast<sockaddr *>(&remote),
                                     sizeof(remote)) == static_cast<int>(packet.size()),
                              "Gateway fixture response failed");
                    };
                    if (mode == "wrong-opcode") {
                        auto wrong = response;
                        wrong[1] ^= 1;
                        send(socket_, wrong);
                    }
                    if (mode == "wrong-source") {
                        auto other = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                        check(other != invalid_socket, "Cannot create wrong-source fixture");
                        send(other, response);
                        close_socket(other);
                    }
                    send(socket_, response);
                }
            } catch (...) {
                fault_ = true;
            }
        });
    }
    ~Gateway() {
        stopped_ = true;
        if (worker_.joinable())
            worker_.join();
        if (socket_ != invalid_socket)
            close_socket(socket_);
    }
    std::vector<Bytes> packets() {
        check(!fault_, "Gateway fixture rejected the client wire format");
        std::lock_guard lock(mutex_);
        return packets_;
    }
};
int main() {
#ifdef _WIN32
    WSADATA sockets{};
    if (WSAStartup(MAKEWORD(2, 2), &sockets))
        return 1;
#endif
    int result = 0;
    try {
        for (const auto &mode : {"normal", "drop-first", "wrong-source", "wrong-opcode"}) {
            Gateway gateway(mode);
            auto lease = nat_pmp_map("127.0.0.1", 51820, 40000, 600, gateway.port);
            check(lease.external_address == "198.18.0.10" && lease.local_address == "127.0.0.1" &&
                      lease.internal_port == 51820 && lease.external_port == 45000 &&
                      lease.lifetime_seconds == 300 && lease.gateway_epoch == 100,
                  "Client did not use the router-assigned UDP port, lifetime and epoch");
            auto packets = gateway.packets();
            check(packets.back().size() == 12 && number16(packets.back(), 6) == 40000,
                  "Preferred external port was not encoded in network byte order");
        }
        {
            Gateway gateway;
            auto initial = nat_pmp_map("127.0.0.1", 51820, 0, 600, gateway.port);
            auto renewed =
                nat_pmp_map("127.0.0.1", 51820, initial.external_port, 600, gateway.port);
            auto packets = gateway.packets();
            check(renewed.external_port == initial.external_port &&
                      number16(packets.back(), 6) == initial.external_port,
                  "Renewal did not request the router-assigned external port");
        }
        {
            Gateway gateway;
            auto lease = nat_pmp_map("127.0.0.1", 51820, 45000, 0, gateway.port);
            auto packets = gateway.packets();
            check(
                packets.size() == 1 && packets[0][1] == 1 && lease.lifetime_seconds == 0 &&
                    lease.external_port == 0,
                "Deletion queried the external address or deleted more than the selected UDP port");
        }
        for (const auto &mode :
             {"version", "denied", "short", "wrong-port", "reboot", "bad-address", "silent"}) {
            Gateway gateway(mode);
            refuses([&] { nat_pmp_map("127.0.0.1", 51820, 0, 600, gateway.port); },
                    "Invalid, denied or absent NAT-PMP gateway was accepted");
            (void)gateway.packets();
        }
        refuses([] { nat_pmp_map("127.0.0.1", 0, 0, 0); },
                "Delete-all mapping request was permitted");
        refuses([] { nat_pmp_map("127.0.0.1", 51820, 0, 3601); },
                "Unbounded mapping lifetime was accepted");
        refuses([] { nat_pmp_map("not-a-gateway", 51820, 0, 600); },
                "Unrequested hostname lookup was permitted");
        for (const auto &mode :
             {"pcp-normal", "pcp-options", "pcp-wrong-nonce", "pcp-wrong-fields"}) {
            Gateway gateway(mode);
            auto nonce = new_gateway_nonce();
            auto lease =
                map_gateway("127.0.0.1", 51820, 40000, 600, "auto", nonce, "", gateway.port);
            check(
                lease.protocol == "pcp" && lease.private_nonce == nonce &&
                    lease.external_address == "198.18.0.11" && lease.external_port == 45001 &&
                    lease.gateway_epoch == 100 && lease.local_address == "127.0.0.1",
                "PCP did not validate the private nonce or use the assigned external UDP endpoint");
            auto renewed = map_gateway("127.0.0.1", 51820, lease.external_port, 600, "pcp", nonce,
                                       lease.external_address, gateway.port);
            check(renewed.private_nonce == nonce && renewed.external_port == lease.external_port,
                  "PCP renewal lost its reserved ownership token");
            auto removed = map_gateway("127.0.0.1", 51820, lease.external_port, 0, "pcp", nonce,
                                       lease.external_address, gateway.port);
            check(removed.lifetime_seconds == 0, "PCP did not confirm targeted deletion");
            auto packets = gateway.packets();
            check(packets.size() == 3 && number16(packets[1], 42) == lease.external_port,
                  "PCP renewal used the internal port as the external endpoint");
        }
        {
            Gateway gateway("legacy");
            auto lease = map_gateway("127.0.0.1", 51820, 0, 600, "auto", new_gateway_nonce(), "",
                                     gateway.port);
            auto packets = gateway.packets();
            check(lease.protocol == "nat-pmp" && lease.private_nonce.empty() &&
                      packets.size() == 3 && packets[0][0] == 2 && packets[1][0] == 0,
                  "PCP did not negotiate NAT-PMP only after explicit legacy Unsupported Version");
        }
        for (const auto &mode : {"pcp-denied", "pcp-short", "pcp-mandatory", "pcp-bad-option",
                                 "pcp-only-wrong", "pcp-silent"}) {
            Gateway gateway(mode);
            refuses(
                [&] {
                    map_gateway("127.0.0.1", 51820, 0, 600, "auto", new_gateway_nonce(), "",
                                gateway.port);
                },
                "Invalid or refused PCP response enabled a mapping");
            auto packets = gateway.packets();
            check(packets.size() == 1 && packets[0][0] == 2,
                  "PCP failure silently downgraded to another protocol");
        }
        refuses([] { map_gateway("127.0.0.1", 51820, 0, 0, "auto", new_gateway_nonce()); },
                "Deletion without its recorded protocol was allowed");
        refuses([] { map_gateway("127.0.0.1", 51820, 0, 600, "pcp", "not-reserved"); },
                "PCP created a mapping before reserving its ownership nonce");
        std::cout << "PASS loopback UDP NAT-PMP packets, assigned ports, renewal/deletion, "
                     "PCP negotiation/nonce ownership, wrong-source filtering and failure cases\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return result;
}
