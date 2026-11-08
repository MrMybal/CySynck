# Direct transport interface binding audit

Current production chat/legacy-file and resumable transfer clients call
`httplib::detail::bind_ip_address` with the configured local VPN address. The direct
receiver checks that its listener address equals the profile address. These checks
do not by themselves establish that a TCP socket uses WireGuard when an ordinary
interface also owns an overlapping address/route. A Linux implementation now checks
the address/interface inventory and Netlink WireGuard type before binding every
TCP socket with SO_BINDTODEVICE. Actual plain-veth/spoofed-name, overlapping-route
and unprivileged kernel probes passed in native run 42477753274 (ten real-driver
checks; see `kernel-wireguard-linux.json`). Windows now validates the official
WireGuardNT adapter, LUID and address before setting IP_UNICAST_IF on every direct
client/listener socket. Actual Windows loopback TCP, accepted-socket inheritance,
replies and ordinary-adapter refusal pass locally; a disposable official-driver
gate is implemented, with native CI execution still pending. macOS now binds TCP
with IP_BOUND_IF using a public, root-owned authorization published by local
`vpn up` after live public-key verification. It binds the profile/key/address to
the actual utun index, boot-session UUID and protected UAPI socket inode/ctime;
every socket revalidates those values. Ordinary hosts read no private socket or
mapping file. The authorization disappears on successful `vpn down`; a stopped,
recycled or re-keyed profile fails the stored identity/generation comparison.
Actual macOS driver/unprivileged probes are implemented but native execution is
pending. This is not yet measured macOS driver evidence.

## Requirements for the implementation

- Identify the unique active interface owning the profile's local IPv4 address and
  verify its expected profile-specific name and actual transport type/ownership.
- Bind the outgoing socket to that interface before connecting or sending the mesh
  token/body. Bind the direct listener/replies appropriately too. Refuse ambiguity,
  missing interfaces and OS binding errors; never fall back to ordinary routing.
- Preserve ordinary user data hosts after an administrator prepares the tunnel.
  Requiring a privileged `wg show` for every chunk would regress embedded games.
- Keep protocol loopback fixtures explicitly local; production VPN configuration
  already rejects loopback. Do not introduce a production environment skip switch.
- Add actual namespace negatives with the VPN address on a plain veth, including a
  deliberately overlapping route. Assert that no HTTP credentials/payload arrive.
  Keep real WireGuard positives and add an unprivileged Linux positive where possible.

## Primary implementation findings

Linux `IP_UNICAST_IF` writes `inet->uc_index`, while the inspected IPv4 TCP connect
path uses `sk_bound_dev_if`. It is not a substitute for binding TCP to a device.
`SO_BINDTODEVICE`/the interface-index counterpart should be used with a validated
Netlink `RTM_GETLINK`/`IFLA_INFO_KIND=wireguard` interface and address inventory. The
inspected Linux 5.10/current socket code permits the initial device binding without
CAP_NET_RAW; rebinding an already bound socket requires it. Linux 5.4 requires the
capability even initially. Runtime errors must remain visible on such older kernels.

Windows officially supports `IP_UNICAST_IF` with an interface index in network byte
order, for outgoing traffic. `IP_IFLIST` restricts datagrams and must not be claimed
as an incoming TCP guard. WireGuard's Windows service creates the adapter with the
profile name and tunnel type `WireGuard`; WireGuardNT uses IF_TYPE_PROP_VIRTUAL and
the adapter API builds the description `WireGuard Tunnel`. Validate the actual
adapter/address inventory and review listener/reply behavior separately.

Darwin exposes `IP_BOUND_IF`. A generic `utun` name alone does not establish
WireGuard ownership: use the logical wg-quick mapping plus a current ownership
check. The implemented public attestation lives in the protected root-owned
WireGuard runtime directory with mode 0644, avoiding new root-owned private state
files that an ordinary preparing user cannot read. The UAPI socket retains its
original owner-only mode. The boot-session UUID is independent of wall-clock
adjustments. Actual platform evidence remains pending as stated above.

Sources inspected (interfaces/constants only; no external implementation copied):

- <https://github.com/torvalds/linux/blob/master/net/ipv4/tcp_ipv4.c>
- <https://github.com/torvalds/linux/blob/master/net/ipv4/ip_sockglue.c>
- <https://github.com/torvalds/linux/blob/v5.10/net/core/sock.c>
- <https://github.com/torvalds/linux/blob/v5.4/net/core/sock.c>
- <https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ip-socket-options>
- <https://github.com/WireGuard/wireguard-windows/blob/master/tunnel/service.go>
- <https://github.com/WireGuard/wireguard-nt/blob/master/driver/device.c>
- <https://github.com/WireGuard/wireguard-nt/blob/master/api/adapter.c>
- <https://github.com/apple-oss-distributions/xnu/blob/main/bsd/netinet/in.h>
- <https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/kern_sysctl.c>
