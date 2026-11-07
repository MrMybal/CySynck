---
id: architecture
title: C++ architecture and local API
description: Understand the shared core, private state and routes used by application integrations.
section: Developers
order: 70
revision: 1
---

# C++ architecture {#architecture-c}

`cysynck_core` is a C++20 shared library without GUI dependencies.
Desktop and CLI call `Core::request(method, path, json)`; HTTP adapts the same operations.
The interface owns presentation state and asynchronous calls. Linux servers build with `CYSYNCK_DESKTOP=OFF`.

## Structure {#structure}

- `include/cysynck/core.hpp`: C++ API; [request contract](doc:cpp-api).
- `include/cysynck/c_api.h`: version-1 C ABI; allocations are freed by their owning library. See [C reference](doc:c-api).
- `include/cysynck/plugin.h`: trusted native extension contract; [plugin reference](doc:plugin-api).
- `src/core.cpp`, backup/snapshot/diff/resolution modules: projects, content capture, bounded comparisons and preserved recovery.
- Process/child-process modules: shell-free UTF-8 arguments, timeouts, owned child supervision and dedicated environment.
- Engine/network/HTTP modules: isolated Syncthing control, authorized devices, WireGuard pairing and local/direct APIs.
- CLI commands are shared with the desktop command panel. CyUI uses GLFW/OpenGL; platform tray bridges remain separate.
- Startup/session support owns profile locks, window reopening and local login registration without adding GUI dependencies to the SDK/server.

## Persistent state {#etat-persistant}

The profile contains `registry.json`, `snapshots/<project>/<id>.json`, `objects/<sha256>`, block objects, `api-token`, private `vpn/` identity and `inbox/` payloads.
Registry writes are atomic. A mutex and disk lock serialize operations across processes; outgoing transfers release locks during networking to allow simultaneous reception.
`engine/api-key` stores the private REST key and `engine/syncthing/` holds managed certificates/configuration.
State is owner-private, plus SYSTEM/Administrators on Windows.
`engine/owner.lock` is held for the child's lifetime and prevents two controllers starting it.
The supervisor terminates only its own child, without global process-name searches. Syncthing logs are capped at 1 MiB with two older rotations.
Backups read in chunks, retry when size/mtime changes and publish a manifest only after every file is captured.
They do not guarantee application-level transactional consistency across related files.
Restoration covers content/simple permissions, not general ACLs, extended attributes, links, empty directories or original timestamps.

## Local API {#api-locale}

Every `/api/*` route requires `Authorization: Bearer <token>`. Obtain it with `cysynck api-token`.
The Web console stores it in browser session storage. WireGuard private keys are never returned by API; local `vpn write-config` writes private configuration.
Tunnel up/down and Syncthing executable/connection setup remain local-only, including through `/api/cli`.
The server may start/stop its preconfigured managed engine. Status omits its REST key; local `engine connect` reads `CYSYNCK_CONNECT_API_KEY`.

| Route | Method and usage |
| --- | --- |
| `/api/overview`, `/api/events`, `/api/storage` | GET |
| `/api/storage/prune` | POST preview; apply with `apply:true, plan` |
| `/api/engine/status`, `/api/engine/devices`, `/api/engine/log` | GET |
| `/api/engine/configure`, `/api/engine/connect` | POST in the local Core only; HTTP refuses |
| `/api/engine/start`, `/api/engine/stop`, `/api/engine/add-device`, `/api/engine/share` | POST |
| `/api/projects` | POST creation |
| `/api/projects/ID` | PUT settings |
| `/api/projects/ID/snapshots` | GET |
| `/api/plugins`, `/api/projects/ID/mode-options` | GET catalog/options; mode-options also accepts PUT |
| `/api/projects/ID/snapshot-files`, `snapshot-compare`, `snapshot-compare-file` | POST; see [snapshots](doc:snapshot-workflows) |
| `/api/projects/ID/backup`, `changes`, `diff`, `verify`, `restore-plan`, `restore`, `register`, `sync`, `status`, `pause` | POST |
| `/api/projects/ID/compare`, `conflicts`, `conflict-diff` | POST read-only comparisons |
| `/api/projects/ID/resolution-plan`, `resolve-conflict`, `resolutions`, `resolution-export`, `resolution-acknowledge` | POST |
| `/api/projects/ID/git-status`, `export`, `inbox`, `import` | POST |
| `/api/projects/ID/git-merge-review`, `git-merge-file` | POST read-only three-way proposals/region composition |
| `/api/projects/ID/git-setup`, `git-extract`, `git-integrate`, `git-merge` | POST; explicit preview/apply workflows |
| `/api/projects/ID/git-integrations`, `git-integration-check`, `git-integration-acknowledge` | POST receipt history/check/review |
| `/api/projects/ID/git-materialize`, `git-materializations`, `git-materialization-check`, `git-materialization-acknowledge`, `git-materialization-resume`, `git-materialization-export` | POST; [LFS working files](doc:git-materialization) |
| `/api/network/status` | GET |
| `/api/network/configure`, `publish`, `discover`, `trust`, `revoke`, `preview`, `write-config` | POST |
| `/api/network/chat`, `send-file`, `history` | POST; [direct channel](doc:network) |
| `/api/cli` | POST `{"args":["project","list"]}` |
| `/api/doctor` | GET inspection or POST explicit probes |
| `/api/service/plan`, `/api/service/export` | POST service preparation/export |

Where a row abbreviates action names, all use the same full prefix shown in that row.
Remote administration uses an SSH tunnel: CLI `--server http://127.0.0.1:8484` reads `CYSYNCK_API_TOKEN`.
The desktop uses a local Core. HTTP-only `/api/help`, `/api/help/open` and `/api/help/archive` support local help/distribution; they are not Core JSON operations.
Opening help is restricted to a loopback-bound server with a loopback client; a remote workstation downloads/extracts the manual.

Resolution plans bind profile/project, paths, complete hashes, size, timestamp, simple permissions and content choice.
They are recomputed under the operation lock. The corresponding Syncthing folder must be paused and exclude `.cysynck-recovery` before moving working originals.
`resolutions/<project>/<id>/` retains the JSON journal and verified private copies.
`registry.json.resolution_pending` flags interruption; no automatic retry replaces working files.
Native errors are normalized to UTF-8, including Windows locked-file refusals.

`compare` receives `{"left":"relative/file.txt","right":"relative/other.txt"}` and requires ordinary project files.
`conflicts` returns `files`, `errors`, `count`, `truncated`; `conflict-diff` receives `{"path":"relative/conflict-name.txt"}`.
Results include `before_info`/`after_info` existence, size and SHA-256 when read;
text adds `before`, `after`, `patch`, `added_lines`, `removed_lines`, `hunks`, `approximate`.
`patch_available` signals exportability; `binary:true` means no text preview, also for exceeded size/line bounds.
`identical:null` means content was not completely read. These comparisons do not write project files.

`verify` accepts `snapshot` or uses latest. Corruption returns a valid `ok:false, issues` report; invalid manifests raise an error.
`restore-plan` accepts snapshot/destination, verifies objects and returns entries without creating a directory.
Restore rechecks before exclusively creating new output.
Pruning takes the common disk lock, scans every manifest and retains the union of references.
Catalog errors forbid deletion. The token pins profile, manifests and object names/sizes/dates.
Only recognized unreferenced ordinary SHA-256 files are individually removed; unknown/temporary files remain.
Login registrations belong to local desktop/CLI startup support, not Core/Web.
Entries have profile identities/ownership markers; cleanup removes only its installation's registrations.
Backup scope uses POST `backup-preview`, PUT `backup-excludes` and optional saved-rule `plan` on POST backup; see [scope](doc:backup-scope).
POST `git-lfs-cache`, `git-lfs-recover`, `git-lfs-recoveries`, `git-lfs-recovery`, `git-lfs-recovery-check`, `git-lfs-recovery-acknowledge` share the [offline cache](doc:git-cache) and preserve interruption evidence.

## Native extensions {#extensions-natives}

Build the supplied plugin example as a shared library and load it explicitly:

```sh
cysynck --plugin /path/to/plugin.so modes
cysynck-desktop --plugin /path/to/plugin.so
```

The extension exports `cysynck_get_plugin_v1` and keeps descriptors alive while loaded.
It can add modes, veto `before_backup`/`before_sync` and observe `after_backup`.
A mode receives project settings and state context and must return a valid JSON object.
The ABI exposes no STL or Unreal objects. Callbacks must not let C++ exceptions escape, re-enter the Core or modify manifests during an event.
Extensions are trusted native code with process permissions, not automatically downloaded/loaded from synchronized folders.

## Other C++ applications and Unreal {#autres-applications-c-et-unreal}

After `cmake --install`, build `examples/consumer` using `-DCMAKE_PREFIX_PATH=/path/to/dist`.
Prefer `c_api.h` for another toolchain/compiler ABI.
For Unreal copy `integrations/unreal/CySynck` into project `Plugins/`, then install the native library into `Plugins/CySynck/ThirdParty/CySynck` using `cmake --install --prefix`.
The module checks ABI, loads C functions and exposes `Open`, `Request`, `Close` to Blueprints.
It has no CyUI dependency and adds no second ImGui instance inside Unreal.
Run backup/diff/network operations in an appropriate task, not the rendering tick.
The bridge is synchronous, without asynchronous Blueprint nodes or automatic scheduler; compile/validate with the target Unreal version before shipping.
