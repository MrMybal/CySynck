# Completion work

This is a developer tracking file, outside the exported human manual. The user asked
to complete the remaining application on 2026-11-08. Each item needs implementation
and evidence; a passing compile is not a target-device or Internet-network test.

## Baseline

- Source 0.40.0, C ABI 1, main `5b2583b`; documentation/readme migration published.
- GitHub run 42382540140: Windows, Linux desktop and Linux server passed; macOS CLI,
  SDK and signature checks passed but relocated desktop window creation failed.
- GitHub secret scan passed. Existing local Windows/manual/package checks are recorded
  separately; do not republish private audit histories in the human README.

## Acceptance work

- [x] macOS renderer compatibility, verified by native CI 42440677786 at `3b0b904`;
  both OpenGL 3/2 captures use Apple's software renderer. Packaged tests stay enabled.
- [ ] Native Windows/Linux/macOS core, SDK, plugin and package CI on the final revision.
- [x] Unreal asynchronous Blueprint requests, deterministic shutdown and actual Windows
  Development Editor build/test in UE 5.7.4 and 5.8.3 (five tests each).
- [ ] Unreal packaged-game and other target-host validation.
- [x] CyRevision Git/LFS transport interoperation against its real formats, signatures and explicit publishing-device authorization.
  Signed Git/LFS transport and actual .NET/C++ roundtrips now pass locally; native
  Windows/Unix canonical signatures are verified. Group admission UI is separate.
- [ ] WireGuard endpoint discovery/rendezvous and network tests; distinguish direct connectivity,
  NAT capabilities and required discovery/relay infrastructure without promising universal traversal.
  Profile-specific names, public-key/Windows-service ownership checks, legacy preservation,
  macOS logical/runtime mapping and bounded handshake status are implemented. Cross-platform
  model tests never activate a workstation tunnel. Real network/driver validation remains pending.
- [ ] Large transfer/recovery and content-defined storage workloads with reproducible measurements.
- [ ] Real local reader and contextual help checks on available target browsers.
- [ ] Reproducible release archives/checksums, verified distribution/update workflow.
- [ ] Signing/notarization when valid external publisher credentials are available; do not invent
  identities or silently replace publisher signing with development/ad-hoc signatures.
- [ ] Update the bilingual human documentation and versions to the final verified scope.

The development branch is `codex/finalize-release`. Local commits remain the default;
push the branch when remote target CI is necessary to advance the requested completion.

## Evidence added

- `3b0b904`: GLFW software pixel format fallback, OpenGL 2 backend, graphics diagnostics;
  package smoke renders both backends and rejects flat/truncated framebuffers.
- Windows complete extracted-package CLI/C++/C ABI/plugin/Git/LFS checks pass, including
  Unreal staging/refusal of an existing plugin and both French desktop captures.
- The real Unreal tests cover Unicode ABI profiles, FIFO/game-thread callbacks and proxy
  lifetime, Close with active/queued work, queue saturation and shutdown/join/reload.
  The worker is owned and joined before unloading native code; a ready result alone is
  not used as an unload barrier.
- The initial full Windows 46-test run passed all 45 non-deployment tests (1,615 s total).
  Its optional deployment cache still referenced `.local/installer-source-0.32.0` and correctly
  failed the current `docs --print` contract. After installing the current tree into
  `dist/finalize-sdk-current` and correcting only that ignored CMake cache value, the
  deployment test passes. The latest six targeted native codec/transport/VPN/diagnostic/core
  checks are run separately after final hardening; final-revision native CI remains required.
- Reproduction: `scripts/test-unreal.ps1` with an installed engine, complete installed SDK
  and a new isolated absolute work directory. Build/runtime logs and JSON reports remain
  in that test directory. Native CI stages the integration but does not install Unreal.
