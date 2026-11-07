---
id: network
title: WireGuard and direct communication
description: Pair trusted peers, prepare the VPN and use chat or direct files.
section: Network
order: 50
revision: 1
---

# WireGuard and direct communication {#reseau-wireguard-et-communications-directes}

Each device keeps its private key locally and publishes a public profile in `.cysynck-network/peers/<peer_id>.json` under the project or Git exchange directory.
Syncthing transports profiles like other shared files; devices must already be authorized in Syncthing.
A central VPN server is unnecessary when devices can reach one another over UDP: LAN, reachable IPv6 or port forwarding.
CySynck does not implement NAT hole punching, STUN/ICE or a WireGuard packet relay.
Syncthing discovery may use discovery/relay services; this does not promise infrastructure-free Internet discovery.

## Prepare two devices {#preparer-deux-appareils}

Install official WireGuard tools. Put `wg` on PATH or select `CYSYNCK_WG`.
Windows can use `CYSYNCK_WIREGUARD` for `wireguard.exe`; Linux/macOS can select `wg-quick` with `CYSYNCK_WG_QUICK`.
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
cysynck vpn trust SHARED_PROJECT_ID OTHER_PEER_ID --key OTHER_PUBLIC_KEY
cysynck vpn preview
cysynck vpn write-config
```

Verify the public key with its owner before trusting it, on both devices. A pinned-key change is refused.
`vpn revoke PROJECT_ID PEER_ID` removes local pairing. Each peer receives only a `/32` route to its VPN address, with no default Internet route or PostUp/PostDown commands.
Private `vpn/cysynck.conf` stays in local state; never synchronize it. POSIX permissions are owner-only; Windows uses the private account state and OS protections.

## Activate the tunnel {#activer-le-tunnel}

In an administrator/root terminal, use **the same `--state` profile** prepared by the user:

```sh
cysynck --state /absolute/path/to/state vpn up
cysynck --state /absolute/path/to/state vpn status
cysynck --state /absolute/path/to/state vpn down
```

Windows uses the official WireGuard tunnel service; Linux/macOS use `wg-quick`. The interface is named `cysynck`; do not assign that name to an independent tunnel.
Changing active configuration currently requires an explicit reconnect. The desktop attempts the same operation with its current permissions and reports privilege failures.

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
