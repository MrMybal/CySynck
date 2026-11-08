---
id: availability
title: Availability and limits
description: Understand the implemented scope and workflows requiring external tools.
section: Start
order: 6
parent: overview
revision: 10
---

# Availability and limits {#availability-and-limits}

## Implemented workflows {#implemented-workflows}

CySynck 0.41 includes project modes, supervised or connected Syncthing, whole-file/fixed-block/CDC backups,
selective restoration, bounded diffs, reviewed conflict resolution with preserved copies, explicit Git/LFS exchange,
history review, separate extraction, reviewed fast-forward/divergent merges, offline LFS cache refill,
materialization/resume, pointer restoration and export of preserved assets. Their guides state exact limits and refusal conditions.
Native plugins use a versioned C contract. The C++ core and C ABI can be used without the desktop.
The [CyRevision transport adapter](doc:cyrevision) adds signed Git/LFS publications and pinned
publishing devices, with actual bidirectional reference tests. CyRevision group membership and roles
remain managed by CyRevision; transport authorization does not grant group admission.

## Network requirements {#network-requirements}

WireGuard needs reachable UDP endpoints or a suitable network arrangement. No STUN/ICE, automatic NAT hole punching or WireGuard packet relay is implemented.
Signed expiring endpoint discovery, private replay checkpoints, direct Syncthing LAN/IPv6 hints and opt-in background refresh are implemented. Live peer updates and project-scoped revocation pass isolated ownership/tool models. A real Linux kernel namespace test also passes handshake, chat, verified files, live revoke/restart and foreign-interface preservation, with exact AppArmor read rules restored afterward. Disposable native CI also exercises official Windows WireGuardNT service/adapter ownership and macOS wg-quick/utun authorization, including ordinary macOS data hosts and stale-generation refusal. Each recorded run states its exact scope; physical host/network permissions still require target configuration. A second real Linux gate passes two independent miniupnpd NAT routers, PCP/NAT-PMP grants, scheduled renewal, signed discovery, verified chat/files and preservation of a foreign forwarded port. Explicit PCP/NAT-PMP router consent, private lease persistence, renewal/expiry, source-address binding, owned cleanup and signed mapped-endpoint publication pass protocol/controller/Core tests. See [network setup](doc:network#automatic-endpoints) and [router mapping](doc:network#router-mapping).
Syncthing discovery and relays may use infrastructure. Direct chat/files use an authenticated HTTP channel inside the VPN and a shared group secret.
There is no offline store-and-forward delivery or read receipt. Do not expose that unencrypted application channel outside the tunnel.

## Platform and integration scope {#platform-and-integration-scope}

Windows is the locally available runtime validation platform. Native Linux CI also passes desktop/server, package/SDK and real Syncthing peer tests.
Native macOS core/SDK/package checks pass, including OpenGL 3 and OpenGL 2 software-renderer captures. CI and cross-compilation do not prove physical graphics, host service activation, tunnel permissions or power-loss behavior.
The Unreal bridge provides asynchronous FIFO Blueprint requests. Windows Development Editor 5.7.4 and 5.8.3 build/automation passes. A real 5.8.3 Win64 Development packaged game passes five tests with 34 runtime/license/manual files verified. NullRHI does not prove graphics; other Unreal versions/hosts and Shipping builds need their own validation. Close waits for active work and can block.
Git, Git LFS tracking tools, Syncthing, WireGuard and OpenSSL remain separately installed where required.

## Preserve independent recovery copies {#preserve-independent-recovery-copies}

Multi-file backup/restore, Git updates, receipt updates and recovery publication are not one filesystem transaction.
CySynck locks coordinate its own processes; external editors and Git clients can still write files. Pause external writers during reviewed operations.
Simple permissions are preserved where documented; general ACL, extended-attribute and original-time restoration is not guaranteed.
Checksums check integrity, not authenticity against a writer who can alter both data and metadata.
Keep independent backups before changing valuable repositories or applying recovery operations.
