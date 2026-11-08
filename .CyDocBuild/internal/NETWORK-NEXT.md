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
denials and malformed options. The client is not yet connected to application configuration, automatic publication
or maintenance; the user manual correctly still labels NAT traversal as pending.

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
  afterward. Enforcement and DAC permissions stay enabled. This configuration must
  pass native CI before recording real tunnel acceptance as successful.
  Reproduce on a disposable Linux machine with the required tools:
  `sudo python3 tests/wireguard_integration.py --prepare-apparmor --cli /absolute/build/cysynck --work /tmp/cysynck-wg-UNIQUE --report /absolute/reports/wireguard.json`.

- Support explicit router-mapping consent and gateway selection, mapping renewal,
  expiry, gateway reboot/network change and ownership-bound cleanup. Prefer modern
  PCP for automatic negotiation; RFC 6886 recommends PCP first with NAT-PMP fallback
  on Unsupported Version, not on arbitrary silence. A forced legacy mode can be explicit.
- Publish mapped endpoints using the integrated signed-record/replay machinery. Reserve
  monotonic counters before publication; keep private keys and mapping nonces local.
- Keep mapping leases private and bind them to the local identity, gateway, local source
  address and selected WG port. Never delete another application's mapping or all ports.
- Run actual Linux WireGuard handshakes/data through two isolated router/NAT namespaces
  in CI, coupled to the mapping fixture and real endpoint publication/discovery. Test
  remapped ports, lease renewal, restart, forged/stale records, chat and direct files.
  Workstation WSL is unavailable; do not install/enable it or activate live user tunnels.
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

All protocol code is independently implemented C++20. No WireGuard/GPL implementation
is copied into the MIT core. Installed tools/drivers remain external dependencies.
