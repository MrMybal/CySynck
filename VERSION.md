# CySynck — Version notes

[Back to README](README.md) · [Français](VERSION.fr.md)

**Core, CLI and desktop source version: 0.40.0**<br>
**Unreal bridge source version: 0.2.0**<br>
**Stage: IN DEVELOPMENT**<br>
**Published application binaries: none**

These notes describe the current source tree, not a published binary release. The C ABI remains at version 1. The bundled documentation uses CyDoc reader 0.1.0; that is a dependency version, separate from CySynck's version.

## Changes in 0.40.0 and the current source tree

- Isolate WireGuard tunnel names by profile, verify keys and Windows service ownership before control, resolve macOS runtime names and preserve foreign/legacy tunnels. Model tests use isolated tools/services; real driver/network validation remains a separate gate.
- Add signed, expiring VPN endpoint publications with independent signer confirmation, durable replay checkpoints, direct Syncthing LAN/IPv6 address hints, opt-in background refresh and project-scoped live revocation. Private configurations are updated to prevent service restart from restoring revoked peers. PCP/NAT-PMP UDP clients are tested but router lease integration remains pending.
- Support French/Chinese profile paths in P-256 generation, signing and verification without changing global OpenSSL environment settings.
- Add an opt-in real 2.25 GiB streamed workload: interrupted transfer/resume after source edits, CDC prefix insertion and complete restoration. The recorded Windows run reuses 99.91% of block bytes and peaks at about 36.3 MiB resident memory; timing and scope are documented, separate from WireGuard routing validation.
- Add signed CyRevision 0.1.34 Git/LFS transport with pinned publishing identities, monotonic import checkpoints, atomic review refs and interruption recovery, exposed in CLI/desktop/Web and bilingual help. Real .NET/C++ imports pass in both directions; group admission stays in CyRevision. Test tooling isolates a compatibility patch for its OS-dependent signature newlines.
- Add a legacy OpenGL fallback, GLFW error diagnostics and real framebuffer checks for packaged desktops, including GPU-less macOS contexts.
- Supply Unreal bridge 0.2.0 with FIFO worker requests, game-thread Blueprint completion, bounded queueing and deterministic close/shutdown. A staging helper preserves existing plugins; real Unreal 5.7.4 and 5.8.3 Windows Editor automation passes.
- Export preserved Git LFS assets from pointer-restoration receipts into a new separate directory. Preview/apply is available through the CLI, desktop, Web/API and existing C/C++ JSON interface, with optional path selection.
- Verify recorded asset identities and source fingerprints before export. Completed exports include a provenance manifest; interrupted exports retain an incomplete manifest and partial copies for inspection.
- Replace distributed Markdown help with a complete offline CyDoc reader: 45 topics per language, English by default and French, contextual desktop/Web help, CLI page/section links and a complete transferable ZIP.
- Generate public C++/C/plugin references from the distributed headers, keep stable help identifiers and supported legacy aliases, and include documentation generation in builds and packages.
- Handle supported Syncthing CLI variants and Linux startup executable paths. The C++ consumer example handles Unicode paths on Windows.

The application also includes project-based Syncthing modes, versioned backups, file review and recovery, explicit Git/LFS exchange, native plugins and optional direct VPN/chat/file workflows. The [offline manual](README.md#offline-manual) describes their exact operation and refusal conditions.

## Known issues

No separate itemized confirmed-bug list accompanies this source version. Consult the [actual issue tracker](https://github.com/MrMybal/CySynck/issues) for reports and [open an issue](https://github.com/MrMybal/CySynck/issues/new) with reproduction steps when needed. The limitations below do not imply the absence of bugs.

## Documented limitations

- **Distribution and platforms:** no application binary release is currently published. Windows x64 has local build/package/runtime validation. Native Linux CI validates the desktop/server, packaged SDK and real Syncthing peers. Native macOS core/SDK/package checks pass, including software-rendered desktop captures. Physical graphics, permissions, host services and tunnel activation need target validation.
- **VPN connectivity:** WireGuard needs reachable UDP endpoints or a suitable network arrangement. Automatic NAT hole punching, STUN/ICE and WireGuard packet relay are not implemented. Syncthing discovery/relays may themselves use infrastructure.
- **Direct messaging/files:** the authenticated application channel uses HTTP inside the VPN and a shared group secret. Keep it inside the tunnel. Offline store-and-forward delivery and chat read receipts are not provided.
- **Recovery and concurrency:** multi-file operations are not filesystem transactions. Application locks do not stop external editors or Git clients. Keep independent backups; general ACL, extended-attribute and original-time restoration are not guaranteed.
- **LFS asset export:** only supported pointer-restoration receipts and unchanged preserved files are accepted. Existing destinations and links are refused; limits are 4 GiB per asset and 16 GiB total. Interrupted exports require inspection and a new destination for another attempt.
- **Integration:** plugins are trusted native code. The asynchronous Unreal bridge is validated with Windows Development Editor 5.7.4 and 5.8.3; other versions, Unreal hosts and packaged games require their own tests. Close waits for active work and can block. Signed CyRevision Git/LFS transport is validated against the reference; group admission remains managed by CyRevision.
- **Installation:** Syncthing, Git, Git LFS tools, WireGuard and OpenSSL are installed separately when needed. OpenSSL provides P-256 signing for CyRevision and VPN endpoints. Signed installers, automatic update downloads and Windows Installed Apps registration are not implemented.
- **Local manual:** actual browser opening and file-origin storage still need checks on the target browsers/platforms. DOM-model and package checks do not establish browser rendering or storage policy.

## References

- [Source repository](https://github.com/MrMybal/CySynck) · [Releases](https://github.com/MrMybal/CySynck/releases) · [Issues](https://github.com/MrMybal/CySynck/issues).
- [README and build instructions](README.md) · [Offline manual entry points](README.md#offline-manual).
- [MIT license](LICENSE) · [Third-party notices](THIRD_PARTY.md).
