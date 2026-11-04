# Development milestones

## Delivered in 0.9

| Milestone | Result | Validation |
| --- | --- | --- |
| Profile diagnostics | One report for CLI, API, desktop and Web; required/optional dependencies, directory health, recovery flags and explicit probes | Isolated tools and loopback engine fixture; English/French UI checks |
| User service preparation | Per-profile Linux systemd/macOS LaunchAgent preview, checked export token, private output and hashed receipt | Export preservation, argument escaping, XML parsing and CLI/API parity; activation remains pending on native hosts |
| C/C++ SDK packaging | Buildable examples shipped inside the archive; pure C entry point with Windows UTF-8 argument conversion | Fresh extraction, C and C++ compilation/execution, C ABI diagnostic/service calls and JSON errors |

These milestones extend the C++ core and its interfaces. Syncthing and WireGuard continue
to run as external engines. No GitHub push is required for local compilation, tests or
commits; the native GitHub jobs only run after an authorized push or workflow dispatch.

## Platform validation still required

Windows x64 is exercised locally, including real Syncthing synchronization and deployment.
The headless Linux core, CLI and C++ test binaries also cross-compile with Zig from Windows.
This validates compilation and linking, rather than Linux runtime behavior.

Native Linux desktop/server and macOS builds must pass their CI jobs, extracted-package
checks and actual service activation tests before release claims cover those platforms.
The Unreal bridge still needs compilation and testing inside Unreal. Developer ID signing,
macOS notarization, signed Windows installers and automatic updates remain separate work.

## Next functional milestones

- Resume interrupted direct transfers and support larger payloads with chunk verification.
- Extend backup storage with block-level deduplication and test larger project trees.
- Expand Git collaboration with LFS and verify CyRevision interoperability.
- Design and test WireGuard rendezvous/NAT traversal against real network configurations;
  Syncthing discovery and relays do not themselves provide that transport.

Implementation guides: [diagnostics](diagnostics.md), [services](services.md),
[integration](integration.md) and [platform packaging](packaging.md).
