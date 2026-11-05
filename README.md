# CySynck

English · [Français](README.fr.md)

A project-based synchronization and backup companion written in **C++20**, powered by
[Syncthing](https://github.com/syncthing/syncthing) and
[CyUI](https://github.com/MrMybal/CyUI).

One shared core powers the native desktop app, CLI, local Web console and integration SDK.
Build a headless server without graphical dependencies. No Python or .NET runtime is required.

**Status: 0.39.0, under active development.** Windows x64 is validated locally. The headless
Linux core, CLI and test binaries also cross-compile from Windows with Zig.
Native Linux and macOS execution still needs validation on those platforms.
The Unreal bridge is an integration example and has not yet been compiled inside Unreal.
The initial GitHub run passed on Windows and exposed Linux and macOS configuration issues.
The current sources address those blockers and additional libc++ portability issues;
those fixes and the new package smoke checks still need another native CI run.

## Features

| Area | Available in 0.39 |
| --- | --- |
| Projects | Separate folders, settings and modes; overlapping project roots are rejected |
| Sync modes | Two-way, send only, receive only, local backup and Git bundle exchange |
| Desktop | Native CyUI overview, diffs, backups, recovery, Git, network, activity and embedded CLI |
| Background operation | Native tray icon, close to tray, reopen on second launch and scheduled backups while hidden |
| Startup | Explicit per-profile login registration; disabled by default |
| Diagnostics | Project paths, required/optional tools, explicit version and Syncthing connection checks |
| Services | Reviewed per-profile systemd/LaunchAgent export, with installation instructions |
| Backups | Whole-file, fixed 4 MiB blocks or optional content-defined blocks, scheduling, retention, integrity checks and restore preview |
| Storage | Explicit cleanup of unused objects, with a preview and checks across all retained manifests |
| Diffs | Snapshot or two-file comparison, side-by-side and unified views, statistics and Git-compatible patches |
| Conflicts | Version selection or manual text merge, checked preview, preserved sources and exportable recovery history |
| Syncthing | Supervised isolated instance, crash restart, device and folder management, existing-instance support and access to the original Web UI |
| Git collaboration | Repository setup, bundles and verified LFS history, branch/commit diffs, three-way file proposals, history, revision extraction, reviewed fast-forward/divergent integration, whole-file choices or manual text editing for conflicts and exchange cleanup |
| Plugins | Loaded-library catalog, optional bilingual metadata, typed per-project mode settings, C loading API and buildable SDK example; ABI v1 compatibility |
| VPN | WireGuard identities, public profile exchange through Syncthing, peer trust and official tunnel tools |
| Direct chat and files | VPN transport, project permissions, pairing secret, local history and SHA-256 verification |
| Resumable transfers | Verified 1 MiB chunks, files up to 4 GiB, configurable profile quotas, frozen source and manual resume |
| Received files | Durable inbox catalog, integrity verification and export to a separate directory |
| Integration | C++ library, C ABI with UTF-8 JSON and an Unreal/Blueprint bridge example |

Unchanged expanded LFS assets can now be preserved in recovery while exact committed pointers
are restored after a checked preview. This prepares the existing pointer-only Git integration
without invoking a filter or needing a local asset cache. CLI, C/C++ SDK and bilingual CyUI/Web
share the operation and its receipt checks. See [restoring LFS pointers](docs/git-dehydration.md).

Interrupted LFS operations can resume from mixed pointer/published states and recreate missing
staging from the verified local cache after a fresh preview. Original pointers remain protected;
already published assets are verified without being rewritten. CLI, SDK and bilingual CyUI/Web
share the recovery workflow, including staging counts and path-specific blocking reasons.
See [resuming LFS staging and mixed states](docs/git-materialization.md#resuming-staging-and-mixed-states-038).

LFS materialization can select exact files or folders after a checked preview. Missing cache
objects outside that selection do not block the operation, and unselected working files stay
untouched. CLI, C/C++ SDK and bilingual CyUI/Web share the workflow and receipt checks.
See [selective LFS materialization](docs/git-materialization.md#selecting-files-and-folders-036).

Git integration and receipt verification hash working files in bounded batches, reducing
Git process launches while checking exact raw bytes. SHA-1/SHA-256 repositories, binary data
and same-size edits with restored timestamps are covered. See [bounded Git inspection](docs/git-object-inspection.md).

Git fast-forward integration can explicitly keep canonical LFS pointers, including on
first import. Assets are materialized separately from verified cache bytes; expanded or
modified working files remain protected by refusal. See [LFS pointer integration](docs/git-lfs-integration.md).

Git LFS cache inspection reports missing, present and damaged committed-history assets.
Explicit verification and reviewed refill from existing peer exchange objects preserve working
files, index and existing cache entries. Local receipts record completion or interruption through
CLI, SDK and bilingual CyUI/Web. See [offline LFS cache refill](docs/git-cache.md).
Recovery receipts can now be checked against their recorded Git history and current cache
bytes. Explicit acknowledgement records reviewed interruptions, including the remaining missing
objects, while preserving the original incomplete status and installed count.

Backup scope previews explain included files and the first matching exclusion rule, with
bounded pages and metadata freshness checks. Review proposed rules before saving, or launch
a manual backup against the saved-rule preview. CLI, C/C++ SDK and bilingual CyUI/Web share
this workflow. See [backup scope and exclusion rules](docs/backup-scope.md).

Plugins now expose names, versions and mode descriptions through the CLI and bilingual CyUI/Web
catalog. Optional descriptors provide typed per-project controls with validated defaults and
checked updates. C/C++ applications can explicitly load libraries, and the packaged SDK includes
a standalone extension example. See [native plugins and mode options](docs/plugins.md).

Snapshot workflows now include paginated file browsing, selected file/folder restoration with
checked previews, and comparisons between two saved versions. Whole-file, fixed-block and CDC
storage share the same operations in CLI, SDK and bilingual CyUI/Web views. See
[snapshot browsing, comparison and selective restoration](docs/snapshot-workflows.md).

Committed LFS pointer files can be materialized from a verified local cache after an explicit
preview. Modified, missing, staged and read-only files are preserved. Original pointers and
interrupted-operation receipts remain available for verification and acknowledgement through
the CLI, C/C++ SDK and bilingual CyUI/Web views. Branch updates use the separate explicit
pointer-only fast-forward workflow; divergent LFS merging remains pending.
See [offline LFS materialization](docs/git-materialization.md).

Small committed blobs are inspected in bounded batches rather than one Git process per blob.
See [object inspection and measured scope](docs/git-object-inspection.md).

Divergent branches can be integrated after a whole-repository merge preview. Per-region local,
peer or combined choices build an editable draft while keeping surrounding automatic changes.
The C++ core recomputes each draft against its pinned proposal before returning it to the editor.
A manual text editor
can combine changes from both committed sides, with ancestor/source views, bounded UTF-8 content,
marker checks and exact edited text in private recovery receipts. Text conflicts also offer
explicit local/peer committed-file choices, including deletions; each choice replaces the entire
file. Every conflict needs a choice and a fresh preview before application. The action creates
a two-parent commit, verifies the complete tree and preserves unrelated local data and a recovery
receipt. Preview objects stay private. CLI, SDK and bilingual CyUI/Web views share the operation.
See [reviewed divergent merge](docs/git-divergent-merge.md).

Three-way Git review compares a local branch and an imported branch with their common ancestor.
Supported text files offer a combined proposal or labelled conflicts, with separate views of
all three committed versions. Proposals preserve local work and do not apply a divergent
repository merge. CLI, SDK and bilingual interfaces share the same operation.
See [three-way Git review](docs/git-merge-review.md) for limits and manual-review cases.

Backup projects can select `--storage cdc` to reuse content-defined blocks after insertions or
deletions shift the bytes of a large file. The optional mode uses 1–8 MiB blocks for files from
8 MiB through 64 GiB; smaller or larger files use whole-file storage. SHA-256 checks cover each
block and the assembled file. The default remains fixed 4 MiB blocks (`auto`). Existing snapshots
remain restorable when policies change. See [backup storage](docs/block-backups.md).

Imported branches can be integrated into the local project after a reviewed preview when
history can advance by fast-forward, including the first local checkout. The operation updates
HEAD, the index and working files, preserves a private receipt and refuses staged or modified tracked files,
divergent history, LFS pointers and checkout filters. Unrelated untracked and ignored files
remain in place; their metadata inventory is pinned to the preview and checked after checkout.
Incoming-path collisions remain blocked. Integration receipts have a paginated history and
can be checked against the current repository. Interrupted operations can be marked as reviewed
after a fresh check, preserving the original status, error and all Git files.
See [Git branch integration](docs/git-integration.md) for limits and recovery.

Imported revisions can now be extracted into a new folder after a reviewed preview, including
verified LFS assets from the approved local cache. The source repository and local changes
stay intact, and private receipts record complete or interrupted copies. Ordinary Git blobs
now stream through bounded buffers up to 1 GiB each; LFS assets support 4 GiB each, within
a 16 GiB output limit. Copying verifies the exact Git object ID and complete-file SHA-256.
See [Git revision extraction](docs/git-extraction.md).

New Git projects can initialize their repository through a reviewed preview in the CLI, SDK,
desktop or Web console. Imported branches remain readable before the first local commit;
branch comparison then shows their complete tree against an empty tree. Existing files and
staged work are preserved. See [Git setup and the first import](docs/git-setup.md).

Large-file backup storage now reuses unchanged blocks across snapshots and projects. The
default policy uses 4 MiB blocks for files from 8 MiB to 256 GiB; `project set ID --storage whole`
keeps whole-file storage. Existing snapshots remain readable. See [block backups](docs/block-backups.md)
for the format, statistics and cleanup rules.

## Languages

The core, CLI, API identifiers and built-in diagnostics use **English**.
The desktop and Web interfaces start in **English**, with **French** as the second language.
Desktop preferences are saved per state profile. The Web language is saved in the browser;
changing it preserves form input, file contents and API data. An unsupported language falls
back to English. Filenames, project names, logs from external tools and user content are not translated.

## Build and run

Requirements: **CMake 3.24+**, Git and a C++20 compiler. CMake downloads the declared dependencies.

- **Windows:** Visual Studio 2022 with C++ tools.
- **Linux desktop:** OpenGL, X11 and Wayland development libraries required by GLFW;
  `libayatana-appindicator3-dev` enables the tray.
- **macOS:** Xcode Command Line Tools and CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix dist
```

Run `dist/bin/cysynck-desktop.exe` on Windows, `dist/bin/cysynck-desktop` on Linux,
or `open dist/CySynck.app` on macOS.
Use `dist/bin/cysynck help` for CLI commands (append `.exe` on Windows).
The scripts `scripts/build.ps1` and `scripts/build.sh` perform these steps.

Create a platform archive with `cpack --config build/CPackConfig.cmake -C Release -B dist/packages`.
Linux uses `.tar.gz`; Windows and macOS use ZIP. The macOS app includes its core and Web
resources. See [platform packaging and Linux cross-compilation](docs/packaging.md).

The Windows ZIP includes a per-user installer:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-CySynck.ps1
```

It installs under `%LOCALAPPDATA%\Programs\CySynck` with Start menu shortcuts.
It requires no administrator rights and does not enable startup automatically.
The same script supports update and removal. See the [Windows installation guide](docs/install.md) (French).

### Headless Linux server

```sh
cmake -S . -B build-server -DCMAKE_BUILD_TYPE=Release -DCYSYNCK_DESKTOP=OFF
cmake --build build-server --parallel
./build-server/cysynck serve --host 127.0.0.1 --port 8484
```

The local Web console is available at that address. Run `cysynck api-token` with the same
`--state DIR` to obtain its access key. Use an SSH tunnel for a remote server: the current
HTTP API is intended for loopback access.

## First backup project

```sh
cysynck project add Documents /srv/documents --mode backup --interval 60 --keep 30
cysynck project list
cysynck backup PROJECT_ID
cysynck changes PROJECT_ID
cysynck diff PROJECT_ID notes.txt
cysynck snapshots PROJECT_ID
cysynck verify PROJECT_ID --snapshot SNAPSHOT_ID
cysynck restore-plan PROJECT_ID SNAPSHOT_ID /srv/restored-copy
cysynck restore PROJECT_ID SNAPSHOT_ID /srv/restored-copy
```

Quote paths containing spaces. `--state DIR` selects a separate profile for the CLI or desktop.
Default state locations are:

| Platform | Default state directory |
| --- | --- |
| Windows | `%LOCALAPPDATA%/CySynck` |
| macOS | `~/Library/Application Support/CySynck` |
| Linux | `$XDG_STATE_HOME/cysynck`, otherwise `~/.local/state/cysynck` |

Scheduling runs while the desktop or `cysynck serve` is active. An interval of `0` disables
scheduled backups. Pausing a project suspends its schedule.

Restores require a **new separate directory** outside all projects, exchange folders and state.
Integrity failures detected during preflight create no destination. Later disk errors may leave
a partial restored copy. Source files are preserved. Backups of actively changing files are
not transactional snapshots of a database or a whole project.

To inspect and reclaim unused backup objects:

```sh
cysynck storage status
cysynck storage prune
cysynck storage prune --apply true --plan PREVIEW_TOKEN
```

Cleanup requires a current preview and refuses damaged manifests. Objects referenced by any
retained manifest are preserved, including manifests belonging to projects no longer registered.
Recovery copies, Git bundles and Syncthing's own versions are not removed by this operation.

## Connect Syncthing

Install [official Syncthing](https://syncthing.net/) separately; its binary is not included.
In the desktop **Engine** view, select **Managed instance**, enter the executable path,
save and start it. CySynck creates its own identity, API key and configuration under its state.
Enable **Start with CySynck** to start the managed engine with the desktop or service.

The CLI offers the same configuration:

```sh
cysynck engine configure --executable /opt/syncthing/syncthing --port 8385 --listen-port 22001 --auto-start true
cysynck engine run
```

`engine run` stays in the foreground; Ctrl+C stops its child. The desktop and service supervise
their own managed process. A crash triggers a restart attempt, with a delay after failures.
An existing external Syncthing instance can also be connected using its local URL and API key.

Add each device on both sides, use the **same project ID** on every device, register the project,
then share it with the peer. The original Syncthing Web interface remains available.

```sh
cysynck engine add-device DEVICE_ID --name Laptop --address dynamic
cysynck register PROJECT_ID
cysynck engine share PROJECT_ID DEVICE_ID
```

CySynck manages only explicitly registered `cysynck-<project_id>` folders on an external engine.
New folders are created paused while required ignore rules are installed. Project snapshot
exclusions and Syncthing transfer ignores are separate settings.

`cysynck sync PROJECT_ID` requests a scan; Syncthing continues transferring changes in the
background. A backup before a manual scan does not cover every incoming transfer.
Syncthing versioning protects remotely replaced files; scheduled snapshots also cover local edits.

## Review differences and resolve conflicts

```sh
cysynck compare PROJECT_ID path/left.txt path/right.txt
cysynck conflicts PROJECT_ID
cysynck conflict-diff PROJECT_ID notes.sync-conflict-20261104-120000-ABCDEFG.txt
cysynck resolution-plan PROJECT_ID CONFLICT_PATH --choice conflict
cysynck pause PROJECT_ID
cysynck resolve-conflict PROJECT_ID CONFLICT_PATH --choice conflict --plan PREVIEW_TOKEN
cysynck resolutions PROJECT_ID
cysynck resolution-export PROJECT_ID RESOLUTION_ID /srv/recovered-copies
```

Choose `current`, `conflict` or `merged`; the latter accepts manual UTF-8 content.
The desktop and Web editors use LF line endings. Source choices support up to **100 MiB per
file**; manual merges support **512 KiB** of UTF-8 text. This is a manual merge, without a
three-way merge base.

Applying a resolution requires a paused project and a current preview. Both versions are copied
into private state and the source files are moved into the project's excluded
`.cysynck-recovery/` directory. A file appearing during publication is not overwritten.
Interrupted operations are flagged for review. The project remains paused after application;
resume it explicitly after checking the result. Recovery copies are retained until managed manually.

## Git collaboration

```sh
cysynck project add Game /work/game --id game-team --mode git-bundles --exchange-path /work/game-exchange
cysynck register game-team
cysynck git export game-team
cysynck git inbox game-team
cysynck git import game-team PEER_ID BUNDLE_ID
```

Each device has its own repository and local exchange folder. Syncthing shares the exchange folder,
rather than synchronizing a live `.git` directory. Export requires a committed, attached branch.
Uncommitted changes stay local. Import creates `refs/remotes/cysynck/<peer>/<branch>`;
review is available in the CLI, Web and CyUI desktop; merging remains an explicit Git operation.
Commits and authors are preserved.

```sh
cysynck project set game-team --git-lfs true
cysynck git branches game-team
cysynck git review game-team PEER_ID BRANCH
cysynck git review-file game-team PEER_ID BRANCH relative/path.cpp
cysynck git history game-team PEER_ID BRANCH --limit 50
cysynck git commit game-team PEER_ID BRANCH FULL_COMMIT_ID
cysynck git commit-file game-team PEER_ID BRANCH FULL_COMMIT_ID relative/path.cpp --parent 1
cysynck git storage game-team --exports OLD_EXPORT_ID
cysynck git storage game-team --exports OLD_EXPORT_ID --apply true --plan PREVIEW_TOKEN
```

The saved LFS policy also controls automatic Git synchronization. Existing custom `lfs.storage`
directories require explicit approval in project settings. Review reads committed Git blobs,
preserves local changes and identifies binary files, submodules and LFS pointers. Cleanup removes
only selected local exchange exports and unused exchange objects; retained exports protect shared
assets. History includes recorded authors and committers, all merge parents and stable pagination.
Select a commit to compare its files with a chosen parent, including root and empty commits.
See [branch review](docs/git-review.md), [commit history](docs/git-history.md) and
[exchange storage](docs/git-storage.md).

Version 2 includes Git LFS objects from the exported branch history, including deleted versions,
with SHA-256 verification and deduplication. Import preserves HEAD, the index and working files.
Both peers need 0.13 or later; `git export ID --lfs false` produces a legacy pointer-only bundle.
See [Git LFS exchange](docs/git-lfs.md) for limits and the default cache layout. Signed manifests,
LFS locks and CyRevision protocol compatibility remain planned. Exchange folders should contain only trusted peers.

## VPN, direct chat and file transfer

WireGuard remains the tunnel engine; CySynck coordinates identities, public profiles and official
tools. Syncthing discovery supplies peer information and transports public profiles.
**Automatic WireGuard NAT traversal is not implemented**, and a Syncthing relay does not relay
WireGuard packets. Restrictive networks may still need reachable endpoints or additional infrastructure.

Tunnel activation requires the relevant system permissions. Starting CySynck does not create
routes or activate a tunnel. Direct chat and file receivers bind to the configured VPN address.
Direct files use verified **1 MiB chunks**, with a default **1 GiB file limit** and separate
**4 GiB inbox/outbox quotas** including pending reservations. Configure up to 4 GiB per file
and 64 GiB per quota. Interrupted sends keep their ID and frozen source for manual resume:

```sh
cysynck send-file PROJECT_ID PEER_ID assets/example.bin
cysynck transfers list PROJECT_ID
cysynck transfers resume PROJECT_ID TRANSFER_ID
cysynck transfers limits
cysynck transfers configure --file-limit-mib 4096 --inbox-quota-mib 8192 --outbox-quota-mib 8192
```

The desktop Network view and Web console expose the same progress and resume actions.
For older receivers, explicitly choose `send-file ... --legacy true` (single request).
See [resumable transfers](docs/transfers.md) for quotas, local cancellation and protocol details.

Received files are available under **Network → Open received files** on the desktop and in the
Web console. Their receipts survive chat history retention. Export checks the full SHA-256
and preserves the received source:

```sh
cysynck received list PROJECT_ID
cysynck received verify PROJECT_ID TRANSFER_ID
cysynck received export PROJECT_ID TRANSFER_ID /srv/new-export
```

See [received files](docs/inbox.md) for the export layout, legacy receipts and limits.

## Background app and integration

The desktop can close to the native tray, keep backups running and reopen on a second launch.
If the tray is unavailable, the window remains accessible and closing it exits the app.
Startup registration is explicit and disabled by default.

```sh
cysynck-desktop --background
cysynck-desktop --foreground
cysynck-desktop --no-tray
cysynck --state /path/to/profile desktop startup status
cysynck --state /path/to/profile desktop startup enable
cysynck --state /path/to/profile desktop startup disable
```

The installed SDK exposes a CMake target:

```cmake
find_package(CySynck CONFIG REQUIRED)
target_link_libraries(MyApp PRIVATE CySynck::Core)
```

`include/cysynck/c_api.h` provides opaque handles and UTF-8 JSON across a C ABI.
Free returned strings with `cysynck_free`. Native plugins use a versioned C ABI and are
loaded explicitly. `integrations/unreal/CySynck` contains a dynamically loaded Blueprint bridge example.

The installed package includes buildable C++ and pure C SDK examples. See
[integration and the C ABI](docs/integration.md) for ownership, UTF-8 and deployment rules.

## Diagnostics and service preparation

Use **Diagnostics** on the desktop or Web console, or run `cysynck doctor` for a local
inspection. `cysynck doctor --probe true` explicitly tests tool versions and the configured
Syncthing connection. Reports omit secrets; see [diagnostics](docs/diagnostics.md).

The **Services** view prepares and exports a headless Linux or macOS user service. Export
does not install or activate it. The same operation is available through the CLI and C ABI:

```sh
cysynck service plan --platform linux --executable /opt/cysynck/bin/cysynck \
  --target-state /srv/cysynck/team --port 8484
```

Review the returned configuration and export it with its preview token. Follow the exported
README on the target machine. See [service preparation](docs/services.md) for the full command,
target paths, user-session requirements and current validation limits.

## Tests and secret scanning

Tests cover backups, diffs and patch application with real Git, resolution recovery, API,
plugins, direct transport, startup and desktop behavior. Additional tests can run against
two isolated local Syncthing engines:

```sh
cmake -S . -B build -DCYSYNCK_TEST_SYNCTHING=/path/to/syncthing
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

These tests disable Internet discovery and retain fixtures under `build/real-sync-data`.
See [GitHub preparation and Gitleaks](docs/github.md) for the publishing procedure and audit scope.
A dedicated workflow scans the full Git history with the default Gitleaks rules.

## Documentation and roadmap

See the [milestone status](docs/roadmap.md) for delivered work and remaining validation.

Detailed guides are currently in French:

- [Desktop](docs/desktop.md), [installation](docs/install.md) and [architecture / SDK](docs/architecture.md)
- [Backups and storage](docs/backups.md), [diffs](docs/diffs.md) and [conflict recovery](docs/resolutions.md)
- [Network, VPN and direct transfers](docs/network.md)

Planned work includes native Linux/macOS validation, signed installers and updates,
optional VPN rendezvous/relay support, Git LFS and CyRevision compatibility, larger-scale
content-defined block backups, larger project trees and larger direct payloads.

Syncthing and CyRevision source code is not incorporated into this project.
Dependency versions, licenses and external references are listed in [THIRD_PARTY.md](THIRD_PARTY.md).
