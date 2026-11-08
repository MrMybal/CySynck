# Network completion and measured evidence

This tracking document is outside the exported user manual. Source implementations,
tool models and actual driver/router evidence have separate scope.

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

## Actual evidence

- `kernel-wireguard-linux.json`: revision 641f89b, run 42477753274, ten real Linux
  kernel checks including sudo owner preservation, ordinary/spoofed interface
  refusal, overlapping physical routes, unprivileged bound TCP, handshake, chat,
  16 MiB verified/encrypted file traffic, revoke/restart and foreign-interface refusal.
- `kernel-wireguard-syncthing-linux.json`: revision dc630a5, run 42480683084,
  twelve successful real-kernel checks. Two actual managed Syncthing engines carry
  public profiles without a manual copy; connected LAN hints retain the pinned
  WireGuard UDP listener rather than adopting the observed TCP sync port. The
  complete guarded handshake/chat/file/revoke workflow then passes.
- `router-wireguard-linux.json`: revision af43bc6, run 42475158981, two independent
  miniupnpd 2.3.9 routers and two clients in four owned namespaces. Real PCP/NAT-PMP
  grants, occupied-port preservation, scheduled Core::tick renewal, signed records,
  forged/stale refusal, chat, an 8 MiB file and owned cleanup all pass. The legacy
  DNAT text renderer cannot decode one target revision; independent UDP payload
  probes verify that the foreign forwarding destination survives each operation.
  Router runtime/control sockets have private mount namespaces. AppArmor is restored.
- The same server job passes all 49 CTests, real Syncthing, SDK/package checks.
- `windows-full-tests.json`: 52 configured Windows Release tests pass in 987.98 s
  with parallelism 2, including real Syncthing, background desktop and deployment.
  The separately measured 2.25 GiB workload is in `large-workload-windows.json`.
- Revision ed1545d passes all four native CI jobs (Windows, Linux desktop/server,
  macOS), including relocated SDK/app execution and software OpenGL captures.

The dual-router fixture copies public profiles to isolate NAT forwarding. The
new direct-LAN kernel fixture establishes actual Syncthing profile transport and
automatic LAN endpoint discovery separately. These do not prove physical router
firmware, arbitrary CGNAT or Windows/macOS driver activation.

## Required work before the final candidate

- Record the pending native Windows/macOS driver gates and final CTests. Their
  socket-interface ownership/lifecycle implementations and disposable driver
  fixtures are present. See `DIRECT-TRANSPORT-AUDIT.md` for measured scope.
- Produce the coherent final version, fresh packages/SDK/checksums, portable update
  validation and final native checks. Keep historical report hashes unchanged.

Real browser file-origin QA was denied by the available browser tool. Do not bypass
that denial with a server or another launch mechanism. DOM/export/URL/package tests
and actual Unreal local-help resolution are separate evidence. Shipping/GPU/other
Unreal hosts and publisher certificate/notarization prerequisites remain target
checks; no signing credential or completed check should be invented.

## Reproduction

Use a disposable Linux runner with wireguard-tools, iproute2, iputils-ping,
OpenSSL, miniupnpd-iptables and iptables. The native workflow builds the headless
CLI first for early network feedback, then all remaining test/SDK targets.
`tests/wireguard_integration.py --help` and
`tests/router_wireguard_integration.py --help` describe their fresh-work/report
arguments. Never activate a workstation tunnel to run these namespace tests.

Protocol and tool references: RFC 6886, RFC 6887, Syncthing system/connections REST,
official WireGuard quickstart/Windows service contract and miniupnpd configuration.
All protocol/controller code is independently implemented C++20. External drivers,
router daemons and Syncthing are not copied or linked into the MIT core.
