# CySynck 0.41 delivery validation

Application payload candidate: `c23ed6f9797a9c7ebe885248a9c0d8738ff6e866`.
All four [native CI jobs](https://github.com/MrMybal/CySynck/actions/runs/42482897027)
and its secret scan succeed. Later QA records and artifact-upload selectors do not
alter the shipped C++/Web/manual payload. The selector cleanup keeps CPack staging
trees out of future download wrappers; its YAML and archive/checksum patterns were
checked locally without repeating unchanged C++ tests.

| Evidence | Result and exact scope |
| --- | --- |
| [Native CI](native-ci-0.41.json) | Windows, Linux desktop/server and macOS all succeed, including package/SDK execution and relocated software-rendered desktop checks. |
| [Local Windows](windows-full-tests-0.41.json) | 53/53 Release CTests, 1070.37 s; real Syncthing, SDK, background desktop, deployment and direct TCP. Core SHA-256 `8b6dc93e1eabb05851dfd763bff73880d580aef462395ee13c39a08791f1e8f7`; binary built from 1864210, with unchanged C++ payload in c23ed6f. |
| [Real Linux bootstrap](kernel-wireguard-syncthing-linux-0.41.json) | 12 kernel checks: actual managed Syncthing profile transport, automatic LAN hints preserving WG UDP ports, real handshake/chat/16 MiB file, route/interface negatives, unprivileged TCP and revocation. |
| [Real Linux NAT](router-wireguard-linux-0.41.json) | Two independent miniupnpd routers, PCP/NAT-PMP, actual renewal, signed endpoint discovery, verified 8 MiB file and foreign forwarding preserved. Public profiles copied for this separate NAT test. |
| [Windows driver](wireguard-windows-0.41.json) | Five actual official WireGuardNT/SCM ownership, Unicode, binding and cleanup checks; 50 native CTests. |
| [macOS driver](wireguard-macos-0.41.json) | Five actual wg-quick/utun checks, ordinary-user binding, stale generation refusal, foreign key preservation and cleanup; 50 native CTests. |
| [Unreal game](unreal-packaged-windows-0.41.json) | Actual 0.41 SDK: five 5.8.3 Development Editor and five cooked/staged/archived game tests; 34 runtime/license/manual hashes. NoEngineChanges, isolated DDC/logs, NullRHI. |
| [Packages](release-packages-0.41.json) | Four local archives, outer Actions and inner CPack digests verified; Windows unchanged non-manual payload plus final 90-page export and actual extracted French help URL. |
| Local manual/Web | Ten Node tests pass; 45 topics per locale, matching anchors, fallback, keyboard/narrow navigation, stored reading state and safe translated API errors. README checks pass for 60 local links, eight fragments and 15 dependency rows. |
| Secrets | Gitleaks 8.30.1: zero findings across the source snapshot, all 35 candidate commits and analyzable content of the extracted final Windows package. |

Historical 0.40 reports remain unchanged. In particular, the separately measured
[real 2.25 GiB workload](large-workload-windows.json) is previous-binary evidence,
not a new performance measurement of the 0.41 interface guards. The earlier
[CyRevision adapter record](CYREVISION.md) and [Unreal integration records](UNREAL-VALIDATION.md) state their scopes.

## Archives

Generated local delivery: `dist/releases/0.41.0/`, with `manifest.json`,
`SHA256SUMS`, README and notices. Archives are ignored build outputs, not committed
binaries or a published GitHub release.

| Platform | Archive | SHA-256 |
| --- | --- | --- |
| Windows-x64 | `CySynck-0.41.0-win64.zip` | `61bc1030b617204d646fdfb55a0f4898008d66bfb607a9d4bf13d712fbd375a3` |
| Linux-desktop-x64 | `CySynck-0.41.0-Linux-x86_64.tar.gz` | `bba683223603aaf9b4f34cf5b7a33b4f3a5f6811a9f482c2fcb205c4b85c8b2d` |
| Linux-server-x64 | `CySynck-0.41.0-Linux-x86_64-server.tar.gz` | `110bcd63b3f0e48bc344c8641aedba422a08f7efdb77b7c49e023f02425ef117` |
| macOS-arm64 | `CySynck-0.41.0-Darwin-arm64.zip` | `b6799dcf63d22b7dcbd5bf83af2f707a2bad2057c2a28636c7b26097a440540d` |

Linux archives are Ubuntu 26.04 x64/GCC 15.2 builds; macOS is macOS 26 ARM64/AppleClang
21. Other OS/libc/CPU targets need their own compatible build and verification.

## Documentation regeneration

```sh
npm ci --prefix .CyDocBuild
npm run build --prefix .CyDocBuild
npm test --prefix .CyDocBuild
```

Normal CMake builds regenerate/copy the manual; CPack distributes it and SHA-256
sidecars. The reader uses local bundled resources through file://. No optional
image/video/gif/mesh/gallery/graph modules are selected: these pages need text,
code/API references and static assets. No Three.js is bundled. English is default,
French shares page/anchor IDs and translation tracking; aliases preserve old links.

## External verification and publishing prerequisites

- Publisher Authenticode/Developer ID credentials and notarization are not supplied.
  Windows is unsigned; macOS uses ad-hoc build signing. No binary release is published.
- Actual browser file-origin rendering/storage was refused by the available browser
  tool. DOM/export, native URL resolution and package tests pass; browser QA remains
  an explicit target check. No alternate browser/server bypass was used.
- Linux measures real paired payload/NAT traffic. Windows/macOS measure driver and
  socket/ownership lifecycle, not cross-host payload or physical router firmware.
  Reachable UDP/IPv6 or consented compatible forwarding remains necessary; CGNAT
  traversal, STUN/ICE and a packet relay are not implemented.
- Unreal Shipping, other Unreal hosts/versions and physical GPU rendering require
  target tests. NullRHI verifies the integration lifecycle and shipped resources.
