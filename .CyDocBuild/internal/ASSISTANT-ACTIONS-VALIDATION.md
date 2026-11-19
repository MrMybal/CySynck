# Assistant application actions — local validation

Date: 2026-11-19. This extends the [initial integration](ASSISTANT-VALIDATION.md).

This records the local validation stage before preview publication. Its candidate
hash and local-only statements describe that stage. The regenerated distributed
package and checksums are on the [Windows preview release](https://github.com/MrMybal/CySynck/releases/tag/v0.41.0-preview.20261119).

## Changes

- The shared CLI/chat/MCP registry now exposes **130 operations** (48 added).
  New operations call existing native Core implementations for engine configuration,
  owned process lifecycle, WireGuard, router leases, pairing, direct chat/files,
  transfer quotas/resume/cancel, CyRevision identities/trust/transactions/recovery,
  service export, maintenance and saved preferences. No new synchronization protocol
  or VPN implementation was invented for the adapter.
- Engine API keys are consumed through an environment-variable reference, never raw
  tool arguments. Responses filter private credentials. Native plugin loading requires
  the reviewed library's SHA-256 and a persistent application instance.
- `cysynck.ui.*` and `cysynck.startup.*` use a bounded desktop action bridge. Workers
  cannot manipulate ImGui. The bridge runs on the application's UI thread even when
  another view is open. Cancellation removes unstarted actions; closing rejects pending
  requests before joining workers. Started/committed actions retain their receipts.
- Desktop and assistant share the existing direct receiver and login controller.
  A headless MCP host owns its own direct receiver. Ephemeral CLI process/receiver/plugin
  lifecycle requests are refused with instructions to use a persistent endpoint.
- `--mcp-port PORT` and optional explicit `--mcp-allow-write` start the desktop endpoint.
  Port zero selects a free loopback port; URL/token-file location go to stderr.
- Read-only defaults remain. Enabling writes now explicitly authorizes application
  actions, including processes and networking. Native plans, independently confirmed
  public keys/fingerprints, tunnel ownership, quotas and path checks remain enforced.
  The assistant prompt requires an explicit user request before sending messages/files.
- English/French guides, matching revisions/anchors, canonical CLI help, READMEs and
  release notes describe the new scope. CyDoc retains 92 pages and needs no media module.

## Dependency isolation

The sibling `D:/_Project/CyMCPSDK` contained unrelated uncommitted SDK changes at the
start of compilation. Its changed `Operation` layout did not compile with its own
existing aggregate initializers. Those edits were preserved. Validation uses a local
`git archive` of the same previously validated **0.7.0** revision
`791d4d7d7680213b282ec8529d248a10f8494d77`, extracted under the existing build tree.
There was no SDK download or modification. `tools info` correctly reports
`source-archive-791d4d7d7680213b282ec8529d248a10f8494d77`; CMake no longer accidentally
attributes a nested source archive to the parent CySynck Git revision.

## Local checks

Windows x64, MSVC/Visual Studio 2022, C++20, Release; desktop and headless builds.
Tests create isolated temporary profiles and remove them. No real peer received
messages/files, VPN interfaces/router mappings were not changed, and no real startup
entry or system service was installed. Engine lifecycle tests use an actual child
process implementing a deterministic loopback-only Syncthing fixture. WireGuard key
generation/inspection uses a deterministic test executable; no activation helper runs.

| Check | Result |
| --- | --- |
| Existing assistant/provider/SDK transport integration | 41 desktop checks |
| Headless assistant/SDK integration | 27 checks |
| Real CLI, stdio and authenticated HTTP entry points | 123 checks per build |
| New native application actions | 106 checks per build |
| Actual hidden desktop driven through MCP | 17 checks passed, including language/theme/project/view changes, settings persistence, receiver/startup inspection and native rendering |
| CyDoc | 92 pages, two locales and common anchors validated; 10 reader tests passed |
| Existing help, snapshot, backup, diff, core and desktop-session regressions | 8 cases passed in temporary directories |
| Installed Windows CLI and native desktop | 123 CLI checks and 17 native-window checks passed |
| Gitleaks 8.30.1 | 0 findings in 431 source files and the extracted candidate ZIP |

The new action tests cover read-only annotations/refusals, native quota invariants,
service export plans, CyRevision configuration/trust/revoke/recovery preview, public
identity persistence, redacted VPN previews, native peer-key confirmation, refusal to
map routers/send chat without an owned tunnel, persistent owned-engine start/stop,
environment-key handling, a reviewed native test plugin, UI-thread dispatch,
cancellation without later execution and closing without deadlock.

## Candidate package

Local candidate: `dist/assistant-packages/CySynck-0.41.0-win64.zip`, **6,907,801 bytes**,
181 entries. SHA-256:
`fdf13f1a68695f3af88c4b524f4cc7a961e7b3017212e3d9cb22449329d5399e`.
The corresponding installation is `dist/assistant-integration`. Both use the new
130-operation integration and regenerated offline manual. The ZIP contains no
runtime profile/API-token files. It is not a published release. Existing archives
under `dist/releases/0.41.0` were preserved. Package staging, extraction/scanner
fixtures, engine/GUI test profiles and intermediate BMP captures were removed;
only the small test reports and French PNG render were retained.

## Reproduction

```sh
cmake -S . -B build -DCYSYNCK_CYMCP_SOURCE_DIR=/sources/stable-CyMCPSDK
cmake --build build --config Release --target cysynck cysynck-desktop cysynck-assistant-tests cysynck-assistant-actions-tests
ctest --test-dir build -C Release -R '^cysynck-assistant(-actions|-cli|-desktop)?$' --output-on-failure
node .CyDocBuild/update-cli.mjs
npm run build --prefix .CyDocBuild
npm test --prefix .CyDocBuild
```

For a server configure another existing build with `-DCYSYNCK_DESKTOP=OFF
-DCYSYNCK_TESTS=ON`, build the CLI and two assistant test targets, then run the same
test expression (the desktop test is absent). The native-window CTest is enabled on
Windows; other platforms still need their own display-session validation.

## Limits

- No GitHub workflow, push or native Linux/macOS validation was performed for this extension.
  Earlier platform/Unreal results do not validate these new assistant actions.
- Actual privileged VPN activation, router changes, peer deliveries, real startup
  registration and external provider billing were deliberately not exercised locally.
  They use existing implementations; this report does not claim new end-to-end coverage.
- A headless MCP host does not automatically enable periodic maintenance. Use the
  existing service/controller or invoke `cysynck.maintenance.tick` explicitly.
- Native plugins stay loaded until their Core instance exits. Unreal scene/editor
  control remains a separate explicitly selected external MCP connection.

Private detailed logs and the native French render are under
`.local/assistant-validation/` and `.local/assistant-expansion-*.log`.
