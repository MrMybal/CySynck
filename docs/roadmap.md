# Development milestones

## Delivered in 0.16

New Git-mode projects can initialize their repository from a preview tied to the project,
folder, branch and SHA-1/SHA-256 format. Existing metadata, bare repositories and files are
preserved. The CLI, C/C++ SDK and bilingual interfaces share this explicit setup flow.

First imports can be reviewed before the first local commit. The complete peer tree is
compared with an empty tree, while history and individual commits remain available. Nullable
local commit IDs and pinned symbolic refs distinguish an unborn branch from detached or
corrupt HEAD and invalidate stale requests. Native and Web history lists scroll independently.
See [Git setup](git-setup.md). Checking out or merging imported files remains a separate Git action.

## Delivered in 0.15

Imported branches now expose paginated topological commit history, recorded authors and
committers, subjects and ordered parents. Ref identities pin continuation pages and stale
responses are discarded by both interfaces. A selected commit can be compared with a chosen
parent, including secondary merge parents, root commits and empty commits. Comparisons use
committed objects, preserve local changes and share the bounded diff viewer. SHA-1 and SHA-256
repositories are supported; shallow history is refused instead of misreporting a boundary as
a root. CLI and C/C++ API routes mirror the bilingual CyUI and Web views.
See [Git history](git-history.md). Applying or merging commits remains a separate Git operation.

## Delivered in 0.14

Three Git collaboration milestones share the C++ core, CLI, SDK and bilingual interfaces:

- Per-project LFS policy, including an explicitly approved existing custom `lfs.storage`
  directory. The saved policy also controls automatic Git-mode synchronization.
- Imported-branch review with ancestry, changed files, bounded committed-text previews,
  unified patches and the desktop CyUI comparison viewer. Review preserves HEAD, index
  and working files and never runs external diff or text-conversion commands.
- Explicit cleanup of owned exports and unreferenced exchange LFS objects, with selection,
  a catalog-dependent preview token, retained-reference checks and partial-failure reporting.

See [LFS policy](git-lfs.md), [Git review](git-review.md) and [Git storage](git-storage.md).
Git merging, distributed acknowledgements and local LFS cache pruning remain separate work.

## Delivered in 0.13

Git exchange now includes canonical Git LFS v1 assets by default. C++ inspects the exact exported
branch history, including deleted versions and shared content, and transports verified objects
through Syncthing. Import checks the inventory against Git objects before installing the LFS
cache and publishing a tracking ref; HEAD, staged changes and working files are preserved.
Distinct manifests keep older clients from silently importing incomplete LFS exports, while
explicit legacy export remains available. CLI, native desktop and Web expose the same behavior.
See [offline Git LFS exchange](git-lfs.md) for limits and compatibility.

## Delivered in 0.12

Resumable direct files now default to 1 GiB per file, with configurable limits up to 4 GiB
and separate 4 GiB default inbox/outbox quotas (up to 64 GiB each). Settings and usage are
available through the CLI, C/C++ API and bilingual desktop/Web views. Quota reductions preserve
existing reservations and received payloads. Sender capture streams into one private snapshot
without a duplicate backup object; exact-name partial cleanup scales linearly with chunk count.
Tests send a real 101 MiB binary file over chunk HTTP with a lost acknowledgement, restart,
changed source, resume and verified export. A 4 GiB metadata reservation exercises 64-bit
boundaries without allocating a multi-gigabyte payload. Native Linux/macOS transfer execution
and multi-gigabyte runtime benchmarking remain pending. See [direct transfers](transfers.md).

## Delivered in 0.11

Large-file backups now use fixed 4 MiB blocks by default for files from 8 MiB to 256 GiB.
Snapshots share blocks across projects and versions; whole-file storage remains selectable.
Verification checks both block and complete-file SHA-256. Restoration, bounded diff previews
and explicit cleanup support mixed old/new manifests. CLI, desktop and Web expose the policy
and storage counters in English/French. Dedicated fixtures cover a mixed 513-file Unicode tree,
corruption and repair, hostile recipes, shared retention, linked namespaces and concurrent cleanup.
See [block backups](block-backups.md).

## Delivered in 0.10

Direct files now use restart-safe journals and 1 MiB SHA-256 chunks. Manual resume reconciles
the receiver's verified prefix with the sender's frozen source, including a lost acknowledgement.
The CLI, native desktop and bilingual Web console expose progress, resume and local removal.
The legacy endpoint remains available explicitly. The initial 100 MiB resumable-file cap
was raised in 0.12. See the [transfer protocol and limits](transfers.md).

The real-engine fixtures keep an absolute 128 MB free-space reserve and require 256 MiB
available before running. This makes their small files usable on large developer volumes
where Syncthing's percentage threshold would otherwise block scans. Production engine
configuration retains its normal disk thresholds.

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

- Exercise multi-gigabyte resumable transfers and native Linux/macOS transport and services.
- Evaluate content-defined block boundaries and benchmark much larger project trees.
- Extend Git LFS storage/locking support and verify CyRevision interoperability.
- Design and test WireGuard rendezvous/NAT traversal against real network configurations;
  Syncthing discovery and relays do not themselves provide that transport.

Implementation guides: [diagnostics](diagnostics.md), [services](services.md),
[integration](integration.md) and [platform packaging](packaging.md).
