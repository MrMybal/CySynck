# Network completion and measured evidence

The selected 0.41 network milestones are complete. This tracking record stays
outside the exported user manual. Historical 0.40 reports retain their measured
hashes and versions. Current source candidate: c23ed6f; native run 42482897027.

## Implemented

- Profile-specific WireGuard names, live public-key and Windows service ownership,
  macOS runtime mappings, safe restart configuration and foreign-tunnel preservation.
- P-256 signed endpoint records binding project, device, WG/signing keys and actual
  UDP listener. Independent signer confirmation, durable sequence/hash floors
  surviving revoke/re-pair, and project-scoped authorization/revocation.
- Direct Syncthing LAN/IPv6 hints use the pinned WG UDP port; relay/public-NAT
  transport ports are never substituted. Endpoint refresh is opt-in and does not
  create a tunnel. Failed kernel removals remain durable until completed.
- Explicit local PCP-first/NAT-PMP router consent, live-listener verification,
  private nonce reservation before UDP, exact source-address binding, renew/expiry,
  gateway epochs, re-consent after source/listener changes and conservative cleanup.
  Only explicitly published projects receive signed mapped endpoints. HTTP/remote
  CLI cannot arm these controls. Ports/nonces are never adopted from shared files.
- Exact AppArmor read-rule export and temporary, restored local CI includes.
  No enforcement downgrade or broad private-directory permission is used.

- Direct TCP interface guards on Linux, Windows and macOS, including a root-owned
  public macOS generation authorization that ordinary data hosts can read.

## Current actual evidence

- `kernel-wireguard-syncthing-linux-0.41.json`: twelve real-kernel checks. Two
  managed Syncthing engines transport public profiles without manual copies;
  connected LAN hints retain WG UDP ports. Ordinary/spoofed interfaces and
  overlapping physical routes cannot receive direct credentials. Unprivileged
  bound TCP, handshake, chat, a verified 16 MiB file and revoke/restart pass.
- `router-wireguard-linux-0.41.json`: six checks across two independent miniupnpd
  routers and two clients in isolated namespaces. PCP/NAT-PMP, real scheduled
  renewal, signed endpoints, an 8 MiB file and foreign forwarding preservation
  pass. Independent UDP payload probes verify the foreign target despite the
  legacy DNAT text renderer limitation. AppArmor overrides are restored.
- `wireguard-windows-0.41.json`: five actual official WireGuardNT/SCM tests pass,
  including Unicode profile, interface binding, foreign service/key preservation
  and own service/adapter removal. The native Windows suite passes 50 CTests.
- `wireguard-macos-0.41.json`: five actual wg-quick/utun tests pass, including an
  ordinary-user bound socket, stale boot/socket/index/key refusal, foreign-key
  preservation and own cleanup. The native macOS suite passes 50 CTests.
- `native-ci-0.41.json`: all four Windows/Linux desktop/server/macOS jobs succeed,
  with native package/SDK and relocated desktop execution. Local Windows has a
  separate full 53-test 0.41 run; `RELEASE-0.41-VALIDATION.md` identifies artifacts.

The NAT fixture copies public profiles to isolate routers. The direct-LAN fixture
proves actual Syncthing bootstrap separately. Windows/macOS driver/socket evidence
does not establish cross-host payload, physical router firmware or arbitrary CGNAT.
No STUN/ICE or packet relay is implemented. An ordinary-user Windows token remains
a target check; Linux/macOS have actual unprivileged socket probes.

## Delivery and external checks

Four archives and SHA-256 sidecars are prepared under `dist/releases/0.41.0/`.
Portable Linux/macOS update instructions and the tested Windows update/rollback
script accompany them. No application binary release has been published.

Real browser file-origin QA was denied by the available tool. No server/alternate
browser bypass is used. DOM/export/URL/package tests and actual Unreal local-help
resolution are separate evidence. Publisher signing/notarization credentials,
physical GPU/network targets, Unreal Shipping and other Unreal hosts remain the
explicit external checks in `RELEASE-0.41-VALIDATION.md`.

## Reproduction

Use the native workflow and disposable elevated driver hosts. Linux fixtures use
wireguard-tools, iproute2, iputils-ping, OpenSSL, miniupnpd-iptables and iptables.
`tests/wireguard_integration.py --help` and
`tests/router_wireguard_integration.py --help` describe fresh-work/report arguments.
Windows/macOS helpers require explicit `--disposable-host` and CI context. They
must not install or activate a driver/tunnel on a user's workstation for testing.

Protocol references: RFC 6886/6887, Syncthing system/connections REST, official
WireGuard service/quickstart contracts and miniupnpd configuration. Controller and
application code is independently implemented C++20. External programs/drivers
are not copied or linked into the MIT application core.
