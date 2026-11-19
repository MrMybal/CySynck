---
id: assistant
title: Assistant, automation and MCP
description: Use the native assistant, script application operations and connect an external MCP client.
section: Developers
order: 72
revision: 2
---

# Assistant and MCP {#assistant}

CySynck builds CyMCPSDK 0.7.0 from source. The integration reference is revision
`791d4d7d7680213b282ec8529d248a10f8494d77`; `cysynck tools info` reports the actual
checkout revision used by your build. A sibling `CyMCPSDK` checkout is preferred;
set `CYSYNCK_CYMCP_SOURCE_DIR` to choose another source tree. Only when no local
checkout exists does CMake fetch the pinned repository revision.

Modules: Core (schemas, policy, resources), Platform (provider processes), Http,
MCP, AI and, on desktop, ChatUI. Local engine supervision/downloads are disabled:
CySynck connects to user-installed agents or existing model services and does not
download model weights. CyUI supplies Diff, Inspector and DocumentViews with their
required text/image dependencies. CyToolsCore was not a dependency of this app.
The existing C/C++ ABI and Unreal bridge remain independent of the assistant.

## Operations and permissions {#operations}

`tools list` is the complete operation catalogue. `tools describe NAME` returns
the actual input/output schemas, descriptions and read-only annotations. Local CLI,
native chat and MCP call this same registry and the real `Core::request` handlers.
Inputs use `project_id` for project actions and `parameters` for their native JSON
arguments. Results use a `result` envelope, except retained exports which return
`resource`. The schemas describe native fields, required inputs and preview/apply
conditions. Core additionally validates current hashes, paths and plugin values;
project/plugin metadata and mode-specific result shapes remain extensible.
Read the [CLI reference](doc:cli) for the native domain contracts.

The catalogue covers projects, backups, snapshots, restores, differences, conflicts,
Git exchange/recovery, storage and plugin options. It also exposes engine configuration
and owned-process lifecycle, VPN configuration/activation, signed endpoint discovery,
peer pairing/revocation, direct chat/files, transfer quotas/resume/cancel, CyRevision
identity/trust/transactions, service export, maintenance, native plugin loading and
desktop preferences/navigation/help/startup. `tools list` is the authoritative inventory.

The interface is read-only by default. Local `--allow-write`, or **Allow application
actions** in the Assistant, authorizes mutations for that host, including processes
and network actions. A remote caller cannot elevate it with a JSON field or CLI flag.
Native preview plans, fingerprints, tunnel ownership checks, destination protections,
quotas and durable recovery receipts still apply. Sending messages/files requires an
explicit user request. Public keys/fingerprints must be confirmed independently by
the user before pairing; discovery alone is insufficient confirmation.

| Operations | Execution requirements |
| --- | --- |
| `cysynck.engine.configure`, `.connect`, `.add-device`, `.share` | Native executable/API validation. `.connect` takes `api_key_env`, the environment variable name holding the API key; the secret is never an argument/result. |
| `cysynck.engine.start`, `.stop` | Persistent desktop, HTTP or stdio MCP host. Only the owning controller stops its managed child. |
| `cysynck.network.*`, `cysynck.chat.send`, `cysynck.transfers.*` | Existing WireGuard privileges, pinned identities and active VPN/receiver requirements. Router changes retain native ownership checks. |
| `cysynck.project.cyrevision-*`, `cysynck.cyrevision.*` | Existing Git exchange project, public identity verification and recovery plans. Private signing keys remain in the local profile. |
| `cysynck.plugins.load` | Persistent host, local library path and `expected_sha256` of the reviewed library. Loading executes native code. The plugin remains loaded until that application instance exits. |
| `cysynck.receiver.start`, `.stop`, `.status` | Persistent host for lifecycle changes. The desktop controls its existing receiver; headless MCP owns its own receiver. |
| `cysynck.settings.get`, `.set` | Saved preferences; `.set` applies live through a desktop endpoint, otherwise on the next desktop launch. |
| `cysynck.ui.status`, `.configure`, `.help`, `cysynck.startup.*` | Running desktop endpoint. UI configuration uses `ui_view`, `ui_project`, `ui_theme`, `ui_language`, `ui_close_to_tray`, `ui_start_hidden`. Startup uses the native owned login entry. |
| `cysynck.service.plan`, `.export`, `cysynck.maintenance.tick` | Service files are exported with a reviewed plan, never installed. Tick runs the configured scheduler once; a headless MCP host does not enable periodic maintenance by itself. |

Preview-only calls to combined preview/apply operations remain available in read-only
mode; `apply:true` requires write authorization and a fresh plan token. SDK workers
call the real Core handlers. Desktop-only actions enter a bounded queue drained on
the UI thread, including when another view is displayed. A queued action cancelled
before execution is removed, and shutdown rejects pending work before workers join.
An already committed operation returns its receipt even if cancellation arrives.
Inspect durable state before deciding whether to retry an interrupted mutation.

Raw API tokens, VPN private keys and signing keys are never returned. Ordinary project
folders are not indexed as MCP resources. Ripgrep/file search stays disabled because
there is no host-selected search workspace; resource grep searches registered text.
Unreal scene/editor control remains a separate host, connected through explicitly
selected external MCP tools rather than direct access to Unreal objects.

## CLI and running instances {#cli}

Start a desktop endpoint without opening its settings manually:
`cysynck-desktop --state /absolute/profile --mcp-port 0`.
The loopback URL and token-file location are printed on stderr. Writes remain
disabled unless you select **Allow application actions** or explicitly launch with
`--mcp-allow-write`. Use the same profile with `tools ... --endpoint URL`.
For example, `tools call cysynck.ui.configure @ui.json --endpoint URL`, where
`ui.json` contains `{"parameters":{"ui_language":"fr","ui_theme":"navy","ui_view":"Overview"}}`,
changes the running window. Use `cysynck.ui.status` to inspect its current state.

```sh
cysynck --state /absolute/profile tools help
cysynck --state /absolute/profile tools info
cysynck --state /absolute/profile tools list
cysynck --state /absolute/profile tools describe cysynck.project.create
cysynck --state /absolute/profile tools call cysynck.overview '{}'
cysynck --state /absolute/profile tools call cysynck.project.create @create.json --allow-write
```

`create.json` contains `{"parameters":{"name":"Example","root":"/existing/project","mode":"backup"}}`.
These commands use the actual profile without opening a window. JSON goes to stdout;
diagnostics go to stderr. Exit 0 means a completed request, 1 a runtime/business error,
2 invalid command syntax, 3 denied access and 4 an `ok:false` business check (its
JSON report remains on stdout). Native previews can legitimately return
blockers or `can_apply:false`; inspect their domain fields before applying.

To address the exact desktop instance, open **Assistant**, start its local MCP
endpoint, and use its displayed port. Its bearer secret is the selected profile's
`api-token` (or set `CYSYNCK_API_TOKEN` in the client environment):

```sh
cysynck --state /same/profile tools list --endpoint http://127.0.0.1:PORT
cysynck --state /same/profile tools call cysynck.overview '{}' --endpoint http://127.0.0.1:PORT
cysynck --state /same/profile tools read-resource RESOURCE_URI --endpoint http://127.0.0.1:PORT
```

This endpoint is separate from the existing Web/API `serve` port. Legacy
`--server http://127.0.0.1:8484 ...` retains its existing protocol and commands.
The embedded legacy command panel retains Core commands; assistant/MCP launchers
are console executable entry points. The installed package includes that executable.

The retained `snapshot-inventory` export requires a running host in console mode:
start `mcp --port PORT`, then call it with `--endpoint`. Its arguments are
`{"project_id":"PROJECT_ID","snapshot":"SNAPSHOT_ID"}`. Use `tools read-resource URI
--endpoint URL` to retrieve it. This prevents returning a reference owned by a
console process that has already exited. The host must remain running during
retention. Small resources return their complete text directly; large resources
return a manifest linking `firstUri` and subsequent `nextUri` chunks.
`projects.list` also uses top-level filter/pagination fields shown by its schema.

## MCP connection {#mcp}

Stdio clients launch the real console executable with these arguments:

```json
{"command":"/absolute/bin/cysynck","args":["--state","/absolute/profile","mcp","--stdio"]}
```

Stdio uses newline-delimited JSON-RPC and reserves stdout for protocol messages.
It requires local process launch rights; there is no additional stdio bearer login.
For HTTP, use **Start local MCP** in the desktop or:

```sh
cysynck --state /absolute/profile mcp --port 8765
```

The SDK binds loopback only. MCP uses `http://127.0.0.1:8765/mcp`,
`Authorization: Bearer TOKEN`, JSON requests and the SDK's session handshake.
Native SDK dispatch is at `/v1`. The current SDK supports tools, resources,
bounded pagination, cancellation and optional change subscriptions. HTTP uses
JSON responses, not a browser SSE endpoint; this integration does not promise
resumable HTTP event streams. Older clients retain TTL refresh/explicit cache clearing.
Use `--full` or the desktop **Full MCP catalogue** option for every schema in discovery;
otherwise clients search, describe and invoke through the five compact gateway tools.

## Native chat {#chat}

Open **Assistant** in the native sidebar. **Detect providers** performs cancellable
SDK discovery on a worker; it never sends a prompt or installs anything. Select a
detected connection, or a provider and its endpoint/executable manually. API secrets
come from the named environment variable and stay outside history/configuration.
Use an actual model from the provider catalogue. Remote API model choice remains
explicit; an installed agent or local service can use its native/default selection.
Agent authentication is managed by the agent's existing local login.
The selector offers Codex and the OpenAI, Anthropic, OpenRouter, Ollama,
OpenAI-compatible, llama.cpp and LM Studio factories: these actually accept the
shared tool registry in this SDK revision. Other native agents use the external
MCP connection instead; their chat transports are not advertised as bound tools.

The SDK ChatUI panel renders streaming Markdown, connection/generation errors,
cancellation and attachments. Enter a local **Attachment path** and use the panel's
attachment action; content support depends on the provider. No file is attached
implicitly. Optional private histories live in `assistant-history` in the profile.
Choose a stable History ID and enable **Restore saved session** to restore it;
missing/invalid sessions report an error instead of starting a substitute session.
The SDK retains full history when a context window is enabled. A context limit is
serialized message bytes, not a token estimate. Native CLI agents keep their own
context management. History is private local data; remove unwanted histories yourself.
If the configured context budget cannot contain the active tool exchange, generation
reports a context error. Increase the budget or disable the limit; the SDK does not
split tool calls from their results or silently discard the active turn.

## External tools {#external}

The **External application tools** settings accept a user-selected MCP URL, a token
environment variable and an explicit comma-separated allowlist. Discovery and calls
run on workers. Connected tools appear as `remote.ENDPOINT_HASH.NAME`; separate URLs
have separate namespaces. Selected external tools require the same local write authorization
and the external server's own rights. External `isError` responses become errors.
Resource list/read helpers let the assistant recover referenced remote chunks.
The SDK client follows supported discovery pagination and privately caches compatible
reads. Reconnecting an already registered tool is rejected; restart the assistant
host to replace a remote connection. No external server is enabled automatically.

External SDK subscriptions are enabled when supported, with TTL fallback. The
`remote.ENDPOINT_HASH.cache.clear` helper explicitly invalidates compatible reads;
remote tool calls also clear their client's cache and are never replayed. Selected
tool schemas are a connection snapshot: restart/reconnect the assistant host when
the remote application's tool contracts change. Notifications do not authorize
new tools outside the allowlist.

## Independent optimization settings {#options}

Optional `assistant-options.json` lives in the private profile. Restart the assistant
host after editing it. All fields are independent; these are the defaults:

```json
{
  "compact_discovery": true,
  "notifications": true,
  "cache_ttl_ms": 30000,
  "inline_bytes": 16384,
  "disk_spool": false,
  "spool_quota_mib": 128,
  "retention_seconds": 300,
  "metrics": false,
  "context_bytes": 0
}
```

Compact large results return private manifests/chunks. The gateway keeps at most
16 MiB/128 retained results in memory; optional disk spooling uses `assistant-results`
with the configured quota and expiry. `cysynck.project.snapshot-inventory` produces
JSONL progressively from pinned 100-file pages; read the resource manifest's
`firstUri`, then every chunk's `nextUri`. Expired handles fail without repeating
the export. Durable backups, Git/recovery/transfer receipts remain in Core storage
independently of this temporary retention. Do not use result handles as job storage.

The registry/resources share the SDK change journal. Live discovery changes can
invalidate subscribed SDK clients before TTL; mutation results are never cached.
Existing paginated native reads remain available. Optional metrics go to private
`assistant-metrics.jsonl`: operation labels, duration, byte counts, status and cache
metadata only; no prompts, arguments, results, credentials or profile identifiers.
No automatic summarizer is configured.

## Build and validation {#validation}

```sh
cmake -S . -B build -DCYSYNCK_CYMCP_SOURCE_DIR=/sources/CyMCPSDK
cmake --build build --config Release --target cysynck cysynck-desktop cysynck-assistant-tests
ctest --test-dir build -C Release -R '^cysynck-assistant(-cli)?$' --output-on-failure
npm run build --prefix .CyDocBuild
```

`CYSYNCK_DESKTOP=OFF` builds the headless server/CLI without CyUI, ImGui or ChatUI.
Linux/macOS SDK HTTP requires the system libcurl development package. The application
package carries the linked SDK code and its license notices; providers and models
remain external. Tests use isolated, automatically removed fixtures. Deterministic
provider fixtures establish integration behavior, not compatibility with every live
provider/account. Platform and live-provider checks must be reported separately.
