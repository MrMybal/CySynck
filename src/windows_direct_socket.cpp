#include "windows_direct_socket.hpp"
#ifdef _WIN32
#include "vpn_tunnel.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace cysynck {
namespace {
bool equal(const wchar_t *value, const std::wstring &expected) {
    return value && CompareStringOrdinal(value, -1, expected.c_str(), -1, TRUE) == CSTR_EQUAL;
}
bool wireguard_description(const wchar_t *description) {
    if (!description)
        return false;
    std::wstring text(description), prefix = L"WireGuard Tunnel";
    if (text.size() < prefix.size() ||
        CompareStringOrdinal(text.data(), static_cast<int>(prefix.size()), prefix.data(),
                             static_cast<int>(prefix.size()), TRUE) != CSTR_EQUAL)
        return false;
    auto suffix = text.substr(prefix.size());
    return suffix.empty() ||
           (suffix.size() >= 3 && suffix.size() <= 8 && suffix.starts_with(L" #") &&
            std::all_of(suffix.begin() + 2, suffix.end(),
                        [](wchar_t c) { return c >= L'0' && c <= L'9'; }));
}
void verify_row(const WindowsDirectInterface &binding) {
    MIB_IF_ROW2 row{};
    row.InterfaceLuid.Value = binding.luid;
    std::wstring name(binding.logical_name.begin(), binding.logical_name.end());
    if (GetIfEntry2(&row) != NO_ERROR || row.InterfaceIndex != binding.index ||
        row.InterfaceLuid.Value != binding.luid || row.OperStatus != IfOperStatusUp ||
        (binding.loopback ? row.Type != IF_TYPE_SOFTWARE_LOOPBACK
                            : row.Type != IF_TYPE_PROP_VIRTUAL || !equal(row.Alias, name) ||
                                  !wireguard_description(row.Description)))
        throw std::runtime_error("The direct transport adapter changed, is down or is not the "
                                 "expected WireGuard adapter");
}
} // namespace

WindowsDirectInterface windows_direct_interface(const std::string &local, const std::string &peer,
                                                const std::string &remote) {
    IN_ADDR address{};
    if (local.find('\0') != std::string::npos || InetPtonA(AF_INET, local.c_str(), &address) != 1)
        throw std::runtime_error("Direct traffic requires a literal local VPN IPv4 address");
    bool loopback = local == "127.0.0.1";
    if (loopback && !remote.empty()) {
        IN_ADDR target{};
        if (remote.find('\0') != std::string::npos ||
            InetPtonA(AF_INET, remote.c_str(), &target) != 1 ||
            (ntohl(target.S_un.S_addr) >> 24) != 127)
            throw std::runtime_error(
                "Loopback direct transport cannot connect to an external peer");
    }
    auto name = loopback ? std::string{} : vpn_interface_name(peer);
    std::wstring expected(name.begin(), name.end());
    ULONG size = 16384;
    std::vector<unsigned char> buffer;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
        if (size < sizeof(IP_ADAPTER_ADDRESSES) || size > 1024 * 1024)
            throw std::runtime_error("Invalid direct adapter inventory size");
        buffer.resize(size);
        result = GetAdaptersAddresses(
            AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER |
                GAA_FLAG_INCLUDE_ALL_INTERFACES,
            nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()), &size);
    }
    if (result != NO_ERROR)
        throw std::runtime_error("Cannot inspect local direct adapter addresses");
    WindowsDirectInterface found;
    for (auto adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()); adapter;
         adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp)
            continue;
        for (auto ip = adapter->FirstUnicastAddress; ip; ip = ip->Next) {
            if (!ip->Address.lpSockaddr || ip->Address.lpSockaddr->sa_family != AF_INET ||
                ip->Address.iSockaddrLength < sizeof(sockaddr_in))
                continue;
            auto value = reinterpret_cast<sockaddr_in *>(ip->Address.lpSockaddr);
            if (value->sin_addr.S_un.S_addr != address.S_un.S_addr)
                continue;
            if (found.index && found.index != adapter->IfIndex)
                throw std::runtime_error(
                    "The VPN address must belong to one active direct interface");
            if (loopback ? adapter->IfType != IF_TYPE_SOFTWARE_LOOPBACK
                         : adapter->IfType != IF_TYPE_PROP_VIRTUAL ||
                               !equal(adapter->FriendlyName, expected) ||
                               !wireguard_description(adapter->Description))
                throw std::runtime_error(
                    "The VPN address is not on this profile's official WireGuardNT adapter");
            found = {local, name, adapter->IfIndex, adapter->Luid.Value, loopback};
        }
    }
    if (!found.index || !found.luid)
        throw std::runtime_error("The VPN address is unavailable on the expected active adapter");
    verify_row(found);
    return found;
}
void bind_windows_direct_socket(uintptr_t handle, const WindowsDirectInterface &binding) {
    verify_row(binding);
    DWORD index = htonl(binding.index);
    auto socket = static_cast<SOCKET>(handle);
    if (setsockopt(socket, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char *>(&index),
                   sizeof(index)))
        throw std::runtime_error("Cannot bind direct TCP to its verified WireGuard adapter");
    DWORD actual = 0;
    int size = sizeof(actual);
    if (getsockopt(socket, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<char *>(&actual), &size) ||
        size != sizeof(actual) || actual != binding.index)
        throw std::runtime_error("Windows did not preserve the direct TCP adapter binding");
    verify_row(binding);
}
} // namespace cysynck
#endif
