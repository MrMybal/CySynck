#include "macos_direct_socket.hpp"
#ifdef __APPLE__
#include "vpn_tunnel.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <regex>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
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
fs::path runtime_directory() {
    auto path = fs::u8path(env("CYSYNCK_WG_RUNTIME_DIRECTORY", "/var/run/wireguard"));
    if (!path.is_absolute() || path_is_link(path))
        throw std::runtime_error("WireGuard runtime directory must be absolute and unlinked");
    return path;
}
void trusted_directory(int descriptor) {
    struct stat info{};
    if (descriptor < 0 || fstat(descriptor, &info) || !S_ISDIR(info.st_mode) || info.st_uid != 0 ||
        (info.st_mode & 0022))
        throw std::runtime_error(
            "Direct macOS traffic requires the protected root-owned WireGuard runtime directory");
}
std::string receipt_name(const std::string &peer) {
    return vpn_interface_name(peer) + ".cysynck-direct.json";
}
Json read_receipt(int directory, const std::string &name) {
    Descriptor file{openat(directory, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)};
    struct stat info{};
    if (file.value < 0 || fstat(file.value, &info) || !S_ISREG(info.st_mode) || info.st_uid != 0 ||
        (info.st_mode & 0022) || info.st_nlink != 1 || info.st_size < 2 || info.st_size > 4096)
        throw std::runtime_error(
            "Run local vpn up as administrator to authorize this macOS direct interface");
    std::string bytes(static_cast<size_t>(info.st_size), '\0');
    size_t offset = 0;
    while (offset < bytes.size()) {
        auto count = read(file.value, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            throw std::runtime_error("Cannot read the macOS direct interface authorization");
        offset += static_cast<size_t>(count);
    }
    return Json::parse(bytes);
}
unsigned address_interface(const std::string &local, const std::string &expected, bool loopback) {
    in_addr address{};
    if (local.find('\0') != std::string::npos || inet_pton(AF_INET, local.c_str(), &address) != 1)
        throw std::runtime_error("Direct traffic requires a literal local VPN IPv4 address");
    ifaddrs *raw = nullptr;
    if (getifaddrs(&raw))
        throw std::runtime_error("Cannot inspect macOS direct interface addresses");
    std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> addresses(raw, freeifaddrs);
    unsigned found = 0;
    for (auto ip = raw; ip; ip = ip->ifa_next) {
        if (!ip->ifa_addr || ip->ifa_addr->sa_family != AF_INET || !(ip->ifa_flags & IFF_UP))
            continue;
        auto value = reinterpret_cast<sockaddr_in *>(ip->ifa_addr);
        if (value->sin_addr.s_addr != address.s_addr)
            continue;
        auto index = if_nametoindex(ip->ifa_name);
        if (!index || (found && found != index) ||
            (loopback ? !(ip->ifa_flags & IFF_LOOPBACK)
                      : expected != ip->ifa_name || (ip->ifa_flags & IFF_LOOPBACK)))
            throw std::runtime_error(
                "The VPN address is not unique on its authorized macOS tunnel");
        found = index;
    }
    if (!found)
        throw std::runtime_error("The VPN address is unavailable on the authorized macOS tunnel");
    return found;
}
Json snapshot(int directory, const std::string &local, const std::string &peer,
              const std::string &key, const std::string &runtime) {
    if (!std::regex_match(runtime, std::regex("utun[0-9]{1,5}")))
        throw std::runtime_error("Invalid macOS direct runtime interface");
    auto index = address_interface(local, runtime, false);
    struct stat socket{};
    auto name = runtime + ".sock";
    if (fstatat(directory, name.c_str(), &socket, AT_SYMLINK_NOFOLLOW) ||
        !S_ISSOCK(socket.st_mode) || socket.st_uid != 0 || (socket.st_mode & 0077))
        throw std::runtime_error(
            "The authorized macOS WireGuard control socket is unavailable or unprotected");
    char boot[64]{};
    size_t size = sizeof(boot);
    if (sysctlbyname("kern.bootsessionuuid", boot, &size, nullptr, 0) || size != 37 ||
        boot[36] != '\0' ||
        !std::regex_match(boot, std::regex("[A-Fa-f0-9]{8}(-[A-Fa-f0-9]{4}){3}-[A-Fa-f0-9]{12}")))
        throw std::runtime_error("Cannot verify the macOS tunnel boot identity");
    return {{"version", 1},
            {"local_address", local},
            {"peer_id", peer},
            {"public_key", key},
            {"runtime", runtime},
            {"index", index},
            {"boot_uuid", std::string(boot)},
            {"socket_device", socket.st_dev},
            {"socket_inode", socket.st_ino},
            {"socket_ctime_seconds", socket.st_ctimespec.tv_sec},
            {"socket_ctime_nanoseconds", socket.st_ctimespec.tv_nsec}};
}
void verify(const MacosDirectInterface &binding) {
    if (binding.loopback) {
        if (address_interface(binding.local_address, "", true) != binding.index)
            throw std::runtime_error("The direct loopback interface changed");
        return;
    }
    auto directory_path = runtime_directory();
    Descriptor directory{
        open(directory_path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    trusted_directory(directory.value);
    auto current = snapshot(directory.value, binding.local_address, binding.peer_id,
                            binding.public_key, binding.runtime_name);
    if (current != binding.attestation ||
        read_receipt(directory.value, receipt_name(binding.peer_id)) != current)
        throw std::runtime_error(
            "The authorized macOS WireGuard generation changed; run local vpn up again");
}
} // namespace

void authorize_macos_direct_interface(const std::string &local, const std::string &peer,
                                      const std::string &key, const std::string &tool,
                                      const fs::path &configuration) {
    if (geteuid() != 0)
        throw std::runtime_error(
            "macOS direct interface authorization requires local administrator privileges");
    auto directory_path = runtime_directory();
    Descriptor directory{
        open(directory_path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    trusted_directory(directory.value);
    auto status =
        vpn_tunnel_status(tool, vpn_interface_name(peer), key, configuration, directory_path);
    if (status.at("present") != true || status.at("owned") != true)
        throw std::runtime_error("Cannot authorize a foreign or inactive macOS WireGuard tunnel");
    auto runtime = status.at("runtime_interface").get<std::string>();
    auto proof = snapshot(directory.value, local, peer, key, runtime);
    auto name = receipt_name(peer), temporary = name + ".part-" + random_id();
    Descriptor file{openat(directory.value, temporary.c_str(),
                           O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
    if (file.value < 0)
        throw std::runtime_error("Cannot reserve macOS public interface authorization");
    try {
        auto bytes = proof.dump() + "\n";
        size_t offset = 0;
        while (offset < bytes.size()) {
            auto count = write(file.value, bytes.data() + offset, bytes.size() - offset);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                throw std::runtime_error("Cannot write macOS public interface authorization");
            offset += static_cast<size_t>(count);
        }
        if (fchmod(file.value, 0644) || fsync(file.value) ||
            renameat(directory.value, temporary.c_str(), directory.value, name.c_str()))
            throw std::runtime_error("Cannot publish macOS public interface authorization");
        fsync(directory.value);
    } catch (...) {
        unlinkat(directory.value, temporary.c_str(), 0);
        throw;
    }
}
void forget_macos_direct_interface(const std::string &peer) {
    auto path = runtime_directory();
    if (!fs::exists(path))
        return;
    Descriptor directory{open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    trusted_directory(directory.value);
    if (geteuid() != 0 ||
        (unlinkat(directory.value, receipt_name(peer).c_str(), 0) && errno != ENOENT))
        throw std::runtime_error("Cannot remove macOS public interface authorization");
}
MacosDirectInterface macos_direct_interface(const std::string &local, const std::string &peer,
                                            const std::string &key, const std::string &remote) {
    MacosDirectInterface binding{local, peer, key, "", 0, local == "127.0.0.1", Json()};
    if (binding.loopback) {
        in_addr target{};
        if (!remote.empty() && (remote.find('\0') != std::string::npos ||
                                inet_pton(AF_INET, remote.c_str(), &target) != 1 ||
                                (ntohl(target.s_addr) >> 24) != 127))
            throw std::runtime_error(
                "Loopback direct transport cannot connect to an external peer");
        binding.index = address_interface(local, "", true);
    } else {
        auto path = runtime_directory();
        Descriptor directory{open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
        trusted_directory(directory.value);
        binding.attestation = read_receipt(directory.value, receipt_name(peer));
        binding.runtime_name = binding.attestation.at("runtime").get<std::string>();
        binding.index = binding.attestation.at("index").get<unsigned>();
    }
    verify(binding);
    return binding;
}
void bind_macos_direct_socket(int socket, const MacosDirectInterface &binding) {
    verify(binding);
    auto index = binding.index;
    if (setsockopt(socket, IPPROTO_IP, IP_BOUND_IF, &index, sizeof(index)))
        throw std::runtime_error(
            "Cannot bind direct TCP to its authorized macOS WireGuard interface");
    unsigned actual = 0;
    socklen_t size = sizeof(actual);
    if (getsockopt(socket, IPPROTO_IP, IP_BOUND_IF, &actual, &size) || size != sizeof(actual) ||
        actual != index)
        throw std::runtime_error("macOS did not preserve the direct TCP interface binding");
    verify(binding);
}
} // namespace cysynck
#endif
