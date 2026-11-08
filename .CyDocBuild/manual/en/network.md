---
id: network
title: WireGuard and direct communication
description: Pair trusted peers, prepare the VPN and use chat or direct files.
section: Network
order: 50
revision: 8
---

# WireGuard and direct communication {#reseau-wireguard-et-communications-directes}

Each device keeps its private key locally and publishes a public profile in `.cysynck-network/peers/<peer_id>.json` under the project or Git exchange directory.
Syncthing transports profiles like other shared files; devices must already be authorized in Syncthing.
A central VPN server is unnecessary when devices can reach one another over UDP: LAN, reachable IPv6 or port forwarding.
CySynck does not implement NAT hole punching, STUN/ICE or a WireGuard packet relay.
Syncthing discovery may use discovery/relay services; this does not promise infrastructure-free Internet discovery.

## Prepare two devices {#preparer-deux-appareils}

Install official WireGuard tools and OpenSSL (P-256 endpoint signatures). Put `wg` on PATH or select `CYSYNCK_WG`; select an OpenSSL executable with `CYSYNCK_OPENSSL` when needed.
Windows can use `CYSYNCK_WIREGUARD` for `wireguard.exe`; Linux/macOS can select `wg-quick` with `CYSYNCK_WG_QUICK`.
macOS resolves logical names through `/var/run/wireguard/<name>.name`. A local tool
wrapper can select a separate absolute, unlinked directory with
`CYSYNCK_WG_RUNTIME_DIRECTORY`; this still requires the live interface public key
to match the profile before any update or removal.
Assign a **different** address in `10.77.0.0/16` to each device, avoiding existing network conflicts. This subnet is not configurable yet.

```sh
# Device A
cysynck vpn configure Alice 10.77.0.1 --endpoint alice.example.net:51820
cysynck vpn publish SHARED_PROJECT_ID

# Device B
cysynck vpn configure Bob 10.77.0.2 --endpoint bob.example.net:51820
cysynck vpn publish SHARED_PROJECT_ID
```

Endpoints are WireGuard UDP addresses, not Syncthing TCP/QUIC ports or relay addresses. Default listen port: `51820/udp`.
A NATed peer may leave its endpoint empty if the other peer is reachable, but must initiate traffic to that peer.
Peers with a known endpoint get `PersistentKeepalive` of 25 seconds.
After profiles synchronize:

```sh
cysynck vpn discover SHARED_PROJECT_ID
cysynck vpn trust SHARED_PROJECT_ID OTHER_PEER_ID --key OTHER_PUBLIC_KEY --fingerprint VERIFIED_SIGNER_SHA256
cysynck vpn preview
cysynck vpn write-config
```

Verify the VPN public key and endpoint signing fingerprint with their owner through a separate trusted channel, on both devices. `vpn status` shows the local signing fingerprint; `vpn discover` shows each candidate's fingerprint. The native pairing form requires the independently checked values. Changing a pinned key, address, device, listener or signing identity requires revocation and pairing again. Legacy profiles without a signing key remain usable for static pairing.

`vpn revoke PROJECT_ID PEER_ID` removes authorization for that project. A peer shared with another authorized project stays in the tunnel. Removing its last authorization also removes the live WireGuard peer and updates an existing private configuration so a service restart cannot restore it. Permission/tool failures preserve a private removal intent and return `requires_restart: true`; application trust is already revoked. Use `vpn refresh` with the required permissions to retry, or stop and restart the tunnel. Each peer receives only a `/32` route to its VPN address, with no default Internet route or PostUp/PostDown commands.
Private `vpn/cs-PROFILE_HASH.conf` stays in local state; never synchronize it. Its stable
15-character tunnel name is derived from this profile's peer identity and is shown by
`vpn preview`, `vpn write-config` and `vpn status`. Separate profiles use separate names.
POSIX permissions are owner-only; Windows uses the private account state and OS protections.

## Activate the tunnel {#activer-le-tunnel}

CySynck checks the existing interface's public key before controlling it. A foreign key
or foreign Windows service configuration causes a refusal. Windows service ownership
also requires the expected executable, service command and exact private configuration
path. Inspection failures are not treated as an absent tunnel. macOS resolves the logical
configuration name to its `utun` interface through wg-quick's runtime mapping, then checks
the key. These checks do not replace operating-system administrator/root permissions.

An old owned global `cysynck` tunnel must be stopped with `vpn down` before the new
profile-specific tunnel starts. The old configuration must belong to this profile;
foreign global tunnels are preserved. Shutdown regenerates a minimal configuration
without hooks rather than executing commands from an edited old file. Repeated up/down
does not overwrite or reactivate an already active/absent tunnel. If an owned Windows
service exists without a running interface, `already_up` is false; stop it before retrying.
Stop/start is needed to apply a changed local address or listener. `vpn refresh` can apply peer endpoints and revocations to an owned active tunnel.

In an administrator/root terminal, use **the same `--state` profile** prepared by the user:

```sh
cysynck --state /absolute/path/to/state vpn up
cysynck --state /absolute/path/to/state vpn status
cysynck --state /absolute/path/to/state vpn down
```

Windows uses the official WireGuard tunnel service; Linux/macOS use `wg-quick`. Use the profile-specific interface name shown by `vpn status`. The desktop attempts the same operations with its current permissions and reports privilege failures.

Some Linux distributions confine `wg-quick` with AppArmor and initially permit configuration files only under `/etc/wireguard`. Root permissions alone do not authorize another private state path. `cysynck vpn apparmor` exports read-only rules for this profile's exact configuration file and directory; it does not modify policy. An administrator can add those rules to the installed `wg-quick` profile's supported local include, preserving existing rules, then reload that profile with `apparmor_parser -r /etc/apparmor.d/wg-quick` (use the actual installed profile filename). The export refuses path characters that could become policy wildcards or variables. Keep AppArmor enforcement enabled and keep the private file permissions unchanged.

## Automatic endpoint refresh {#automatic-endpoints}

Leave a peer's published endpoint empty to allow automatic selection. An explicitly pinned endpoint always takes precedence. `vpn refresh [PROJECT_ID]` reads signed `<peer_id>.endpoint.json` publications only for already authorized projects and identities. Each publication binds the project, peer, WireGuard key, signing key, Syncthing device and UDP listener. Signatures expire within one hour; private sequence/hash checkpoints reject rollback and conflicting reuse after a restart. A valid cached publication stays usable until expiry if an older or forged shared file replaces it. Revoking a project's authorization discards its eligible endpoint.

When there is no valid signed endpoint, the local Syncthing connection may supply a literal LAN IPv4 or direct global/ULA IPv6 address. Only connected TCP/QUIC peers with a pinned Syncthing device ID qualify. The UDP port comes from the pinned **WireGuard listener**, not the observed sync port. Relay, disconnected, public IPv4 NAT hints, loopback, link-local and hostname candidates are excluded. A candidate does not prove UDP reachability: check a recent WireGuard handshake in `vpn status`.

```sh
cysynck vpn refresh SHARED_PROJECT_ID
cysynck vpn automatic true
# Keep the desktop or `cysynck serve` running to call Core::tick().
cysynck vpn automatic false
```

Automatic refresh is off by default and checks about every 45 seconds while the host runs. It never creates a tunnel; updating an active tunnel requires administrator/root permissions and passes ownership checks. An expired dynamic endpoint is removed from the generated configuration and cleared from the live peer before re-adding its host route. Errors are available under `endpoint_refresh` in `vpn status`. HTTP and remote CLI cannot configure identities, pair/revoke peers, write private tunnel configurations, activate tunnels or arm/perform automatic refresh; use the local desktop or CLI.

New local identities keep their separate endpoint signing key in `vpn/endpoint-signing.pem`, never in the exchange. Publishing a manual endpoint also writes a signed publication valid for 15 minutes.

Compact sequence/hash floors also survive revocation and re-pairing of the same
project/device/key tuple. They do not preserve authorization or restore a revoked
endpoint. This private history admits at most 4096 new identity tuples; reaching
the limit refuses new records without silently discarding replay protection.

## Opt-in router mapping {#router-mapping}

A compatible local IPv4 router can open the **owned live WireGuard UDP listener** using
PCP or NAT-PMP. This is off by default. Choose the router explicitly in the native
Network view or local CLI, after starting the VPN. Keep the public profile's manual
endpoint empty for this mode. A new identity can omit `--endpoint` when configuring;
changing an already pinned profile requires revocation and pairing again on its peers.

```sh
cysynck vpn up
cysynck vpn publish SHARED_PROJECT_ID
cysynck vpn router enable 192.168.1.1 --protocol auto --lifetime 600 --external-port 0
cysynck vpn status
# Keep this profile's desktop or normal serve process running for renewal.
cysynck vpn router renew
cysynck vpn router disable
```

`auto` tries PCP and falls back to NAT-PMP only on an explicit Unsupported Version
response. Forced `pcp` and `nat-pmp` modes are available. Requested lifetimes are
60–3600 seconds; external port `0` lets the router choose. The actual grant supplies
the published external address/port, deadline and renewal schedule. The live tunnel's
public key and UDP port must match this profile; merely editing the desired port
does not authorize mapping a different listener. No tunnel is created by mapping.

The private registry saves the PCP nonce before sending a request and reuses it
after restart. Renewal occurs around half the granted lifetime while `Core::tick()`
runs. Each request binds to the consented gateway-facing local IPv4 address. A
changed source address or listener stops renewal and requires a new local enable
action. Gateway epoch changes are detected on a successful renewal; a newly assigned
external port replaces the signed publication. Only projects already explicitly
published locally receive these automatic records. They expire with the grant and
in at most one hour. Peers must run `vpn refresh` or opt into automatic refresh.

Disabling is saved before attempting deletion and withdraws this device's local
endpoint sidecars. Copied signed records retain their original expiry. Cleanup
uses only the recorded protocol, source address, owned listener and PCP nonce;
it never sends a delete-all operation. Failed deletion remains pending for a
bounded retry. An expired mapping, changed network/listener or uncertain legacy
request is left to router expiry instead of risking deletion of another mapping.
`vpn down` attempts cleanup before stopping its owned tunnel. `router_mapping` in
`vpn status` shows grants, errors and pending expiry, without the private nonce.
HTTP and remote CLI cannot enable, renew or disable mappings.

The UDP protocol fixtures, persistent lease lifecycle and signed Core publication
tests pass. A real Linux kernel test with two independent miniupnpd NAT routers also
passes PCP/NAT-PMP grants, actual scheduled renewal, signed discovery, forged/stale
record rejection, chat, a verified 8 MiB file and preservation of an unrelated
forwarded UDP listener during cleanup. The harness copies public profiles; it does
not test Syncthing transport across those routers or physical router firmware.
This feature does not provide STUN/ICE, arbitrary NAT hole punching, a packet relay
or guaranteed CGNAT traversal.

## Chat and direct files {#tchat-et-fichiers-directs}

Set the same random secret of at least 24 characters as `CYSYNCK_MESH_TOKEN` on authorized devices.
Share it outside public-profile folders. This version uses a group secret rather than per-pair secrets.

```sh
cysynck chat serve
cysynck chat send PROJECT_ID OTHER_PEER_ID "Hello"
cysynck send-file PROJECT_ID OTHER_PEER_ID assets/example.bin
cysynck chat history PROJECT_ID
```

The receiver listens on the configured VPN address, default `8485/tcp`. The desktop Network view can start it too; only one CLI/desktop receiver may use that address/port.
The sent filename is relative to the project. Received bytes stay under a generated `inbox/<project>/<id>.received` identity, with original name in history.
They are not opened, executed or inserted into the project automatically.
The application channel uses HTTP **inside WireGuard** without its own TLS. Outgoing connections bind to the configured VPN address.
The receiver checks VPN source address, pinned peer, project authorization and group secret. Do not configure these addresses on a public/plaintext interface.
Defaults: 16 KiB UTF-8 per message, 1 GiB per file, 4 GiB incoming/outgoing quota, 2,000 local history entries.
Files use verified 1 MiB blocks, a frozen source and explicit interruption recovery: `transfers list PROJECT_ID`, then `transfers resume PROJECT_ID TRANSFER_ID`.
`transfers configure` and interfaces adjust limits up to 4 GiB per file and 64 GiB per quota across the profile's projects.
Incoming reservations count against the quota. For old receivers choose `send-file ... --legacy true`.
See [the transfer protocol and limits](doc:transfers). Read receipts and deferred/offline delivery are not implemented.
