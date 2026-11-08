# Automatic peer endpoints — implementation work

This is developer work tracking, outside the exported user manual. Do not mark the
VPN goal complete on the basis of ownership models or a UDP protocol fixture alone.

## Already implemented

`ab1f418` isolates tunnel names per profile, verifies live interface public keys and
Windows service executable/command/configuration ownership, preserves foreign and
legacy tunnels, resolves macOS wg-quick logical names and bounds handshake status.
The tests inject tools, services and runtime mappings, without activating a workstation
tunnel. Native French captures are inspected in an isolated synthetic profile.

The PCP/NAT-PMP client foundation sends actual UDP datagrams to an explicitly selected
gateway. It uses the **WireGuard internal UDP port** and the router-assigned external
port; it never copies a Syncthing TCP/QUIC port or a STUN mapping from an unrelated
socket. Internal port zero/delete-all is forbidden. Lifetime-zero deletion omits
the address query and clears the suggested external port, as required by RFC 6886.
PCP keeps a private 96-bit nonce for ownership, renewal and deletion. Automatic mode
falls back only on an explicit legacy Unsupported Version response. Loopback tests
cover assigned ports, renew/delete, epochs, wrong sources/nonces/fields, protocol
denials and malformed options. The application now exposes explicit local router
consent, owned live-listener inspection, durable PCP reservations, source-bound UDP,
renewal/expiry and conservative cleanup. Successful grants publish signed endpoints
only for projects explicitly published from this profile. UDP, controller lifecycle
and Core publication tests pass; actual two-router NAT acceptance remains pending.

## Required implementation and evidence

Signed endpoint discovery now stores durable private replay checkpoints and resolves
only pinned, authorized project/device/WG/signing identities. Local CLI/desktop controls
support direct LAN/IPv6 candidates, explicit endpoint priority, opt-in background refresh
through Core::tick and owned live wg peer updates. Revocation removes only the chosen
project; the last authorization removes the kernel peer, updates an existing private
configuration and keeps a durable pending removal on failure. HTTP and remote CLI cannot
arm these operations; HTTP policy normalizes flags before checking positional commands.

Local tests pass for restart/replay, changed pinned metadata, independent fingerprints,
foreign tunnel preservation, persisted service configuration, project-scoped revoke,
failed/retried removal and default-off/tick opt-in. These are **isolated tool models**,
not actual driver or router acceptance evidence. Real P-256 tests use Unicode paths.

Remaining work:

- `tests/wireguard_integration.py` is now wired into the Linux server CI job. It
  creates two private network namespaces, a veth pair wholly inside them, real
  `wg-quick` tunnels and the production direct receiver. It checks recent kernel
  handshakes, ping, chat, a 16 MiB nonrepeating file with exact checksum/encrypted
  byte counters, live revoke, restart preservation and foreign-interface refusal.
  The harness copies only public profiles; it does not claim real Syncthing/NAT
  transport coverage. Only its sanitized report is uploaded; owned namespaces,
  data and ephemeral private keys are removed. Initial native runs fail when the
  enforcing wg-quick AppArmor profile refuses configuration under the private test
  state path: euid/uid/owner are root, mode is 0600 and the wrapper can read it before
  exec. The installed-tool boundary is now reported without printing keys.
  `vpn apparmor` exports exact read-only rules and refuses policy metacharacters.
  Explicit CI `--prepare-apparmor` adds only the two known test configs through the
  distro-supported local include, reloads only wg-quick and restores the include
  afterward. Enforcement and DAC permissions stay enabled. This configuration
  passed native CI at `ed1545d`: all six driver checks, CTest, SDK/package and real
  Syncthing server tests pass in run 42471087011. `kernel-wireguard-linux.json` records
  the sanitized proof and its precise scope. The extra fixture-owner check added later
  checks that sudo updates preserve an existing user's private registry ownership;
  its actual POSIX run passes in the same six-check native job.
  Reproduce on a disposable Linux machine with the required tools:
  `sudo python3 tests/wireguard_integration.py --prepare-apparmor --cli /absolute/build/cysynck --work /tmp/cysynck-wg-UNIQUE --report /absolute/reports/wireguard.json`.

- Exercise the integrated router controls against an independently implemented NAT
  gateway. Current automated evidence separates actual UDP protocol fixtures, the
  deterministic persisted controller and real Core publication/listener models;
  none establishes actual NAT forwarding. PCP-first fallback, private nonces,
  renewal/reboot/remapping, source change and deletion boundaries are implemented.
- Extend the actual Linux WireGuard test from direct veth peers to two isolated
  router/NAT namespaces, coupled to real mapped endpoint publication/discovery. Test
  remapped ports, lease renewal, restart and forged/stale records in addition to the
  existing real handshake/chat/file/revoke/restart/foreign-interface checks.
  Workstation WSL is unavailable; do not install/enable it or activate live user tunnels.
  `tests/router_wireguard_integration.py` now defines that separate native CI gate:
  two clients/two independent miniupnpd routers, private runtime mounts, real PCP
  and NAT-PMP grants, collision with a foreign mapping, signed discovery, actual
  Core::tick renewal, forged/stale shared records, encrypted chat/file counters and
  owned cleanup. Its Python syntax and secret scan pass on Windows; actual Linux
  execution is still pending. The first runner detected a fixture backend mismatch:
  miniupnpd-iptables uses libiptc while the system iptables alternative uses nftables.
  The fixture now explicitly uses iptables-legacy, without changing host alternatives.
  No forwarding rule or temporary interface is placed
  in the host network namespace. Router debug logs are private and discarded.
  Reproduce with miniupnpd-iptables, iptables and the previous kernel-test tools:
  `sudo python3 tests/router_wireguard_integration.py --prepare-apparmor --cli /absolute/build/cysynck --work /tmp/cysynck-wg-nat-UNIQUE --report /absolute/reports/router-wireguard.json`.
- Review outbound direct HTTP route binding and live peer revocation while implementing
  the real driver tests; do not rely solely on the chosen private IP range as evidence
  that a connection uses the owned WireGuard interface.
- Document which routes need reachable IPv6, manual forwarding or a compatible local
  gateway, and when a relay/reachable third party remains necessary. Do not promise
  universal serverless traversal through arbitrary NAT/CGNAT/firewalls.

## Primary protocol sources inspected

- <https://www.rfc-editor.org/rfc/rfc6886> (wire format, 5351, assigned ports,
  deletion, renewal, epochs and PCP transition)
- <https://datatracker.ietf.org/doc/html/rfc6887> (PCP MAP, private nonce, options and response validation)
- <https://docs.syncthing.net/rest/system-connections-get.html> (direct vs relay types)
- <https://www.wireguard.com/quickstart/> (UDP endpoints and keepalive)
- <https://git.zx2c4.com/wireguard-windows/about/docs/enterprise.md> (service contract)
- <https://git.zx2c4.com/wireguard-tools/tree/src/wg-quick/darwin.bash> (runtime mapping)
- <https://github.com/miniupnp/miniupnp/blob/master/miniupnpd/miniupnpd.conf> (independent router configuration)
- <https://packages.ubuntu.com/resolute/miniupnpd-iptables> (native test-only gateway package)

All protocol code is independently implemented C++20. No WireGuard/GPL implementation
is copied into the MIT core. Installed tools/drivers remain external dependencies.
