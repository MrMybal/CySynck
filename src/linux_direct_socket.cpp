#include "linux_direct_socket.hpp"
#ifdef __linux__
#include "vpn_tunnel.hpp"
#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <ifaddrs.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace cysynck {
namespace {
struct Descriptor {
    int value = -1;
    ~Descriptor() {
        if (value >= 0)
            close(value);
    }
};
void wireguard_kind(unsigned index, const std::string &expected) {
    Descriptor descriptor{::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE)};
    if (descriptor.value < 0)
        throw std::runtime_error("Cannot inspect the direct transport interface");
    timeval timeout{2, 0};
    if (setsockopt(descriptor.value, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)))
        throw std::runtime_error("Cannot bound the direct interface inspection deadline");
    struct Request {
        nlmsghdr header;
        ifinfomsg link;
    } request{};
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(ifinfomsg));
    request.header.nlmsg_type = RTM_GETLINK;
    request.header.nlmsg_flags = NLM_F_REQUEST;
    request.header.nlmsg_seq = 1;
    request.link.ifi_family = AF_UNSPEC;
    request.link.ifi_index = static_cast<int>(index);
    sockaddr_nl kernel{};
    kernel.nl_family = AF_NETLINK;
    if (sendto(descriptor.value, &request, request.header.nlmsg_len, 0,
               reinterpret_cast<sockaddr *>(&kernel), sizeof(kernel)) != request.header.nlmsg_len)
        throw std::runtime_error("Cannot query the direct interface type");
    alignas(nlmsghdr) std::array<unsigned char, 16384> buffer{};
    sockaddr_nl source{};
    iovec bytes{buffer.data(), buffer.size()};
    msghdr reply{};
    reply.msg_name = &source;
    reply.msg_namelen = sizeof(source);
    reply.msg_iov = &bytes;
    reply.msg_iovlen = 1;
    auto received = recvmsg(descriptor.value, &reply, 0);
    if (received < 1 || source.nl_family != AF_NETLINK || source.nl_pid != 0 ||
        (reply.msg_flags & (MSG_TRUNC | MSG_CTRUNC)))
        throw std::runtime_error("Cannot read the kernel's direct interface inventory");
    int remaining = static_cast<int>(received);
    for (auto message = reinterpret_cast<nlmsghdr *>(buffer.data()); NLMSG_OK(message, remaining);
         message = NLMSG_NEXT(message, remaining)) {
        if (message->nlmsg_seq != 1)
            continue;
        if (message->nlmsg_type != RTM_NEWLINK ||
            message->nlmsg_len < NLMSG_LENGTH(sizeof(ifinfomsg)))
            throw std::runtime_error("The direct interface is no longer available");
        auto link = reinterpret_cast<ifinfomsg *>(NLMSG_DATA(message));
        if (link->ifi_index != static_cast<int>(index) || !(link->ifi_flags & IFF_UP))
            throw std::runtime_error("The direct interface changed or is down");
        bool named = false, encrypted = false;
        int length = IFLA_PAYLOAD(message);
        for (auto attribute = IFLA_RTA(link); RTA_OK(attribute, length);
             attribute = RTA_NEXT(attribute, length)) {
            auto type = attribute->rta_type & 0x3fff;
            if (type == IFLA_IFNAME) {
                auto data = static_cast<const char *>(RTA_DATA(attribute));
                auto size = RTA_PAYLOAD(attribute);
                named = size == expected.size() + 1 && !std::memcmp(data, expected.c_str(), size);
            } else if (type == IFLA_LINKINFO) {
                int nested_length = RTA_PAYLOAD(attribute);
                for (auto nested = static_cast<rtattr *>(RTA_DATA(attribute));
                     RTA_OK(nested, nested_length); nested = RTA_NEXT(nested, nested_length))
                    if ((nested->rta_type & 0x3fff) == IFLA_INFO_KIND &&
                        RTA_PAYLOAD(nested) == 10 &&
                        !std::memcmp(RTA_DATA(nested), "wireguard", 10))
                        encrypted = true;
            }
        }
        if (named && encrypted)
            return;
        throw std::runtime_error("Direct traffic requires this profile's actual WireGuard "
                                 "interface, not an ordinary interface with the same address");
    }
    throw std::runtime_error("Kernel direct interface reply is incomplete");
}
} // namespace

LinuxDirectInterface linux_direct_interface(const std::string &local, const std::string &peer,
                                            const std::string &remote) {
    in_addr address{};
    if (local.find('\0') != std::string::npos || inet_pton(AF_INET, local.c_str(), &address) != 1)
        throw std::runtime_error("Direct traffic requires a literal local VPN IPv4 address");
    bool loopback = local == "127.0.0.1";
    if (loopback && !remote.empty()) {
        in_addr target{};
        if (remote.find('\0') != std::string::npos ||
            inet_pton(AF_INET, remote.c_str(), &target) != 1 || (ntohl(target.s_addr) >> 24) != 127)
            throw std::runtime_error(
                "Loopback direct transport cannot connect to an external peer");
    }
    ifaddrs *inventory = nullptr;
    if (getifaddrs(&inventory))
        throw std::runtime_error("Cannot inspect local direct interface addresses");
    LinuxDirectInterface found;
    bool ambiguous = false;
    for (auto item = inventory; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET || !item->ifa_name ||
            !(item->ifa_flags & IFF_UP) || (loopback && !(item->ifa_flags & IFF_LOOPBACK)))
            continue;
        auto ip = reinterpret_cast<sockaddr_in *>(item->ifa_addr);
        if (ip->sin_addr.s_addr != address.s_addr)
            continue;
        if (!found.name.empty() && found.name != item->ifa_name) {
            ambiguous = true;
            break;
        }
        found.name = item->ifa_name;
        found.index = if_nametoindex(item->ifa_name);
        found.loopback = loopback;
    }
    freeifaddrs(inventory);
    if (ambiguous || found.name.empty() || !found.index || found.name.size() >= IFNAMSIZ)
        throw std::runtime_error("The VPN address must belong to one active direct interface");
    if (!loopback) {
        if (found.name != vpn_interface_name(peer))
            throw std::runtime_error(
                "The VPN address is not on this profile's WireGuard interface");
        wireguard_kind(found.index, found.name);
    }
    return found;
}
void bind_linux_direct_socket(int socket, const LinuxDirectInterface &interface) {
    if (!interface.index || if_nametoindex(interface.name.c_str()) != interface.index)
        throw std::runtime_error("The direct transport interface changed before binding");
    if (setsockopt(socket, SOL_SOCKET, SO_BINDTODEVICE, interface.name.c_str(),
                   interface.name.size() + 1))
        throw std::runtime_error("Cannot bind direct TCP to its verified interface; this kernel "
                                 "may require CAP_NET_RAW");
    if (if_nametoindex(interface.name.c_str()) != interface.index)
        throw std::runtime_error("The direct transport interface changed during binding");
    if (!interface.loopback)
        wireguard_kind(interface.index, interface.name);
}
} // namespace cysynck
#endif
