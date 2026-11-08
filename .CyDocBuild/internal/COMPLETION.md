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

- [ ] macOS renderer compatibility; packaged rendering tests must remain enabled.
- [ ] Native Windows/Linux/macOS core, SDK, plugin and package CI on the final revision.
- [ ] Unreal asynchronous Blueprint requests, deterministic shutdown and actual engine build/test.
- [ ] CyRevision interoperation against its real formats, signatures and trust model.
- [ ] WireGuard endpoint discovery/rendezvous and network tests; distinguish direct connectivity,
  NAT capabilities and required discovery/relay infrastructure without promising universal traversal.
- [ ] Large transfer/recovery and content-defined storage workloads with reproducible measurements.
- [ ] Real local reader and contextual help checks on available target browsers.
- [ ] Reproducible release archives/checksums, verified distribution/update workflow.
- [ ] Signing/notarization when valid external publisher credentials are available; do not invent
  identities or silently replace publisher signing with development/ad-hoc signatures.
- [ ] Update the bilingual human documentation and versions to the final verified scope.

The development branch is `codex/finalize-release`. Local commits remain the default;
push the branch when remote target CI is necessary to advance the requested completion.
