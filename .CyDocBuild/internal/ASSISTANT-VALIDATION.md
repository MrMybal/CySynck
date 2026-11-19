# CyMCPSDK integration — local validation

This report records the initial 82-operation integration. The later application-action
extension, including its current validation and package, is recorded in
[ASSISTANT-ACTIONS-VALIDATION.md](ASSISTANT-ACTIONS-VALIDATION.md).

Validation date: **2026-11-19**. Application source: **CySynck 0.41.0, development working tree**.
The earlier release validation records do not cover this addition.

## Source and modules

- SDK: **CyMCPSDK 0.7.0**, local checkout `D:/_Project/CyMCPSDK`, revision
  **`791d4d7d7680213b282ec8529d248a10f8494d77`**. The checkout was not modified.
- The SDK was compiled from its sources inside the application's existing build folders.
  A sibling checkout or `CYSYNCK_CYMCP_SOURCE_DIR` takes priority; Git fetch is a fallback
  only when the local SDK is unavailable.
- Modules: Core, Platform, Http, MCP and AI; ChatUI on desktop. Local engine downloads,
  CUDA/model management, SDK examples and SDK test targets are disabled.
- CyUI: `6ef01cfcdb197f3a89d64618ce5ef8818c704728`; Dear ImGui:
  `64944b4520b30772de8dbf0b37d0311746477a32`. Diff, Inspector and DocumentViews share
  one ImGui context/backend. Their required code/image dependencies and notices are included.
- CyToolsCore was absent from the application dependencies. No jobs/data migration was needed.
- The existing C ABI remains version 1. The Unreal bridge is unchanged; the local
  CyUEMCPSDK README and native integration guide were consulted as references.

## Delivered behavior

- **82 native business operations**, with stable names, descriptions and schemas, share one
  SDK registry between the native assistant, JSON CLI and MCP. Handlers call real Core methods.
- Project discovery has filters, pagination and an inventory hash. Existing backup, Git,
  conflict and recovery preview/receipt checks are retained. Plugin values use the active
  Core schema; mode-specific results preserve their native JSON contract.
- Reads/previews are allowed by default. Mutations need the host's explicit write grant;
  caller JSON cannot choose a principal or elevate a running instance. Legacy CLI commands
  retain their existing native-user behavior.
- Native ChatUI: streamed replies, cancellation, errors, private history/session restoration,
  file attachments, provider discovery and model selection. Only SDK factories that actually
  accept the shared registry are offered. Codex uses a read-only native sandbox; application
  writes still go through registered, authorized tools.
- MCP: stdio and authenticated loopback HTTP JSON. Full discovery remains available;
  compact discovery uses the five SDK gateway tools. Subscriptions are supported where
  negotiated; TTL/explicit refresh remain available.
- External HTTP MCP clients use an explicit tool allowlist and distinct endpoint namespaces.
  Their resources can be read, caches cleared, notifications used when available, and errors
  returned with sanitized details. Selected external schemas are connection snapshots;
  restart/reconnect the host after a remote contract change.
- Private retained results use SDK resources, quotas and expiry. Snapshot inventories are
  produced incrementally from pinned pages. Console retained exports require `--endpoint`
  so the result remains owned by a running host; `tools read-resource` retrieves its contents.
  An expired reference never causes automatic execution of its original operation.
- Independent options cover compact discovery, notifications, TTL, inline size, disk spooling,
  quota/retention, metadata-only JSONL measurements and API context bytes. Full application
  history survives context projection; no summarizer is installed.
- The CyDoc manual now has **92 generated pages across English/French**, common page/anchor
  IDs and an Assistant/F1 context. No optional CyDoc media module or Three.js was added.

## Executed locally

Environment: Windows x64, Visual Studio 2022/MSVC, C++20, Release. No GitHub workflow was run.

| Check | Result |
| --- | --- |
| Desktop/CLI/SDK source build | Passed |
| Assistant C++ integration | **41 checks passed** |
| Real CLI/MCP executable entry points | **95 checks passed** |
| Headless source build, no CyUI/ImGui/ChatUI | Passed |
| Headless assistant integration | **27 checks passed** |
| Headless real CLI/MCP entry points | **95 checks passed** |
| Installed executable CLI/MCP entry points | **95 checks passed** |
| Existing regression cases | **8 passed**: Core integration, backups, snapshots, diffs, offline help, HTTP help, desktop session and desktop background launch |
| Reader/Web documentation tests | **10 passed** |
| Installed native Assistant render | English and French, **1440×960**, captured and visually inspected |

The C++ suite exercises actual Core project creation/updates, denied calls and stale plans;
private multi-chunk recovery of a 2 MiB disk-spooled result; expiry without replay; pinned
450-file inventory export; live resource-cache invalidation before TTL on the actual HTTP
transport; private metrics; asynchronous chat streaming/cancellation/restoration; real SDK
OpenAI-compatible SSE tool exchanges; remote MCP calls and their errors from API-backed chat;
and preservation of complete history while a smaller context is sent to the provider.

The console suite launches real processes. It covers legacy commands, help, schemas,
JSON argument files and inline JSON, Unicode/space/percent/hash paths, codes 0/1/2/3/4,
unauthenticated and unauthorized requests, full paginated stdio discovery, authorized
mutations, exact-instance HTTP calls and complete retrieval of retained export content.

The chat provider/API fixtures are deterministic local test services. The application business
handlers are real; these results do not claim a live model/account compatibility test.

## Commands

```powershell
cmake -S . -B build -DCYSYNCK_CYMCP_SOURCE_DIR=D:/_Project/CyMCPSDK
cmake --build build --config Release --target cysynck cysynck-desktop cysynck-assistant-tests
ctest --test-dir build -C Release -R '^cysynck-assistant(-cli)?$' --output-on-failure
npm run build --prefix .CyDocBuild
npm test --prefix .CyDocBuild
cmake --install build --config Release --prefix dist/assistant-integration
cpack --config build/CPackConfig.cmake -C Release -B dist/assistant-packages
```

Real tested console invocation, using the installed executable and an isolated profile:

```powershell
.\dist\assistant-integration\bin\cysynck.exe --state .\temporary-profile tools call cysynck.overview '{}'
```

The actual harness supplies a unique temporary profile, verifies JSON/exit 0 and removes it.
For connection/configuration, see the [English](../manual/en/assistant.md) and
[French](../manual/fr/assistant.md) guides. `tools info` reports the compiled SDK provenance.

## Distribution and evidence

Initial Windows candidate (superseded by the application-action extension):
`dist/assistant-packages/CySynck-0.41.0-win64.zip`, **6,829,981 bytes**,
181 ZIP entries. SHA-256:
`31d0c6ebe721ee2915e408d7c76b68dd390a67e179c128a897883d7bffca1db7`.
It includes the native app, console executable, C/C++ SDK, reader, and SDK/MD4C/font notices.
Runtime profiles, tokens, conversations and temporary result stores are absent.
The previous four validated release archives were preserved.

The full archive smoke test also passed: extracted CLI/API behavior, relocated local help,
compiled C and C++ consumers, and the example plugin/SDK workflows. Its temporary
extraction, profiles and consumer builds were removed.

Gitleaks 8.30.1 scanned all **426 tracked/new source files** and the actual extracted
Windows archive: **0 findings** in both scopes. The temporary scan copies were removed.

Detailed build/test logs, rendered captures and machine-readable evidence are under the ignored
`.local/assistant-validation/` directory. Test fixtures, package extraction/consumer projects and
source-scan snapshots are isolated and removed automatically. Builds reuse the existing desktop
and headless directories; no Unreal validation projects were generated for this integration.

## Scope and remaining platform checks

- A live provider needs the user's actual installed agent/account or API/local service. No live
  generation or credential-dependent authentication was attempted during this validation.
- Actual Linux/macOS builds and native UI runs remain necessary on those systems. The headless
  Windows build proves absence of graphical dependencies, not Linux/macOS runtime compatibility.
  Their SDK HTTP transport needs system libcurl development files.
- Unreal editor/game checks were not repeated: the integration does not add Unreal object
  operations or alter the bridge ABI. Unreal scene operations belong to a separately authorized
  editor MCP connection.
- Native Core actions already executing may complete after cancellation. Durable Core receipts
  and state must be inspected before any user-authorized retry. Temporary resources are not a
  durable job store, and their host must remain alive for the advertised retention.
- Outbound peer messaging/files, trust/secret changes and process ownership retain their explicit
  existing workflows. Native agent permissions remain separate from application permissions.
