# Offline LFS working-tree materialization

After checking out a committed revision containing LFS pointer files, CySynck can replace
those exact pointers with verified assets already available in the local LFS cache. This is
an explicit operation, separate from bundle import, branch integration and revision extraction.
It performs no fetch and requires no Git LFS executable. Git remains responsible for checkout
and merging. From 0.34, [explicit fast-forward integration](git-lfs-integration.md) can leave
committed LFS pointers ready for this separate materialization workflow.

Use **Preview LFS materialization** in the Git exchange view, inspect the file states, then
**Materialize verified assets**. CyUI and Web default to English and include French.
The same C++20 operation is available through the CLI and existing C ABI version 1:

```sh
cysynck git materialize PROJECT_ID
cysynck git materialize PROJECT_ID --apply true --plan PREVIEW_TOKEN
```

POST `/api/projects/ID/git-materialize` accepts `{}` for preview, or adds `apply:true` and
the preview's `plan`. Optional `base` and `local_ref` pin the current committed HEAD and branch,
including detached HEAD. The plan binds the complete inspection, index inventory, Git
configuration, project settings, approved cache location, selection and pointer fingerprints.
Reusing a stale plan fails.

## Selecting files and folders (0.36)

Leave **LFS selection** empty for all committed LFS pointers, or enter one relative path per
line. A folder needs a trailing `/`; paths are literal and do not use glob patterns. The
preview reports only matching pointer files and an `excluded` count of other committed
pointer paths. Missing or damaged cache objects outside the selection are not inspected and
do not block selected assets. Unselected working files are not read or written by this step.

CLI callers provide a UTF-8 JSON array through `--paths-file` (at most 32 KiB):

```json
["Content/Maps/", "Content/Textures/logo.uasset"]
```

```sh
cysynck git materialize PROJECT_ID --paths-file selection.json
cysynck git materialize PROJECT_ID --paths-file selection.json --apply true --plan PREVIEW_TOKEN
```

The API accepts the same array in `paths`; omit it for the existing all-pointer behavior.
Explicit `null` and empty arrays are rejected. Supply the selection again when applying.
Response `selection` is `null` for all pointers or a sorted, deduplicated array. Overlapping
selectors include a file once, and each selector must match a committed LFS pointer. Ordinary
files, unknown paths, unsafe components and case aliases are refused. Selection accepts
1–256 input paths totaling at most 16 KiB of UTF-8 path text. These limits precede deduplication.

Changing the selection invalidates the UI preview. The token binds the exact canonical
selection, including overlapping selectors, and project settings. Recovery files and receipts
cover only the selected pending pointers. History exposes selection and excluded counts;
receipt checks reconstruct the selected scope from the recorded commit, and interrupted
operations can still be acknowledged after an exact check. Earlier receipts remain readable.

Selection does not bypass complete committed-tree validation, repository/index checks or
the existing global inventory limits. Malformed pointers or unsafe committed paths elsewhere
can therefore still block inspection. Pause external writers during application.

## File states and preservation

Only canonical LFS v1 pointers in ordinary committed files, with the same stage-zero index
entry and committed `filter=lfs` attribute, can be materialized. Active text conversions and
local `.git/info/attributes` are refused. Hooks, filters, fsmonitor and network operations are
not executed. Configured clean/smudge/process programs can remain installed without being run.

Preview reports `files`, `pending`, `materialized`, `skipped`, `bytes`, `errors` and
`can_materialize`. Each file has one of these states:

| State | Behavior |
| --- | --- |
| `pointer` | Exact committed pointer; requires a fully verified cache object |
| `materialized` | Working bytes already match the committed asset |
| `modified` | Different local content is preserved |
| `missing` | Local deletion is preserved; the operation does not recreate it |
| `staged` | A staged change is preserved |
| `index_flags` | Assume-unchanged or skip-worktree path is preserved |
| `read_only` | Read-only pointer is preserved |
| `attributes` | Missing LFS filter or unsupported committed text conversion is skipped |
| `blocked` | Unsafe filesystem entry or another inspection error prevents application |

Missing or corrupt cache objects needed by any pending file block the operation. Import the
required verified objects before previewing again. Custom `lfs.storage` paths require the
existing explicit per-project approval. Ordinary files, unrelated staged edits, ignored and
untracked files are not changed. HEAD, refs and the index are preserved.

Inspection supports ordinary complete SHA-1/SHA-256 repositories, up to 10,000 tracked files
and 4,096 LFS paths, with 4 GiB per asset and 16 GiB of referenced asset bytes. Linked worktrees,
bare/partial/shallow repositories, shared/sparse indexes and active Git operations are refused.
Paths, case aliases, links, metadata names and the reserved recovery namespace are checked.

## Publication and recovery files

Application first streams every required asset into a new owner-private
`PROJECT/.cysynck-lfs/OPERATION_ID/after/` directory. The 64 KiB copy buffer and complete
SHA-256 checks bound memory and verify actual bytes, including empty and shared assets.
Staging needs additional disk space for the selected assets. The complete staging phase
finishes before any working pointer is moved.

Each unchanged pointer is then renamed into `before/RELATIVE_PATH` inside that same private
operation directory. Its fingerprint is checked again after the rename. The staged asset
inherits its permissions and Windows DACL and is published only if the working destination
is absent. A concurrently created destination is preserved. Original pointer files remain
available after success; known staging files are moved away as they are published.

An owner-private receipt is written to
`STATE/git-materializations/PROJECT/OPERATION_ID.json` before staging. It records the committed
context, selected pointers, original fingerprints, cache location, recovery directory and
progress. Success reports `status:"complete"`, `applied:true` and `files_written`.
Failure preserves an incomplete receipt, staged assets and moved sources for inspection.
There is no automatic rollback, retry, removal or restoration of missing files.

CySynck shares `.git/cysynck-integration.lock` with reviewed integration. Close other Git
clients and avoid concurrent edits: this lock coordinates CySynck operations, not external
applications. Multi-file publication is not one filesystem transaction and does not promise
power-loss durability across all files and receipt updates. Leftover locks are preserved.
The new recovery namespace is untracked; consider excluding `.cysynck-lfs/` in your normal
Git workflow. CySynck does not edit `.gitignore` or index that directory.

## Checking and acknowledging receipts

The bilingual interfaces list **LFS materialization receipts** and offer **Check LFS receipt**.
CLI and C/C++ callers use these POST routes, with no ABI version change:

| Route | Body |
| --- | --- |
| `/api/projects/ID/git-materializations` | `{"offset":0,"limit":20}` |
| `/api/projects/ID/git-materialization-check` | `{"receipt":"OPERATION_ID"}` |
| `/api/projects/ID/git-materialization-acknowledge` | `{"receipt":"OPERATION_ID","plan":"CHECK_TOKEN"}` |

```sh
cysynck git materializations PROJECT_ID
cysynck git materialization-check PROJECT_ID OPERATION_ID
cysynck git materialization-acknowledge PROJECT_ID OPERATION_ID --plan CHECK_TOKEN
```

Checks validate the receipt against its immutable committed pointers, current HEAD, staged
entries and index flags, working asset SHA-256, original pointer fingerprints and
known staged files. They do not need the LFS cache to remain present. Unknown recovery entries,
links, altered originals or incomplete staging block acknowledgement and remain preserved.
An existing integration lock also blocks verification until it is inspected manually.
`index_metadata_matches` reports the original raw-index comparison separately. Git stat-cache
refreshes can change those bytes while staged entries and flags remain identical; that alone
does not block acknowledgement. The check token also pins the currently observed raw index.

The result's `position` is `before`, `after`, `partial`, `interrupted`, `changed` or `unknown`.
Per-file diagnostics show the actual working, original and staged fingerprints. `before` means
the original pointer bytes and permissions are present again; copied-back pointers may have
new timestamps. `after` additionally requires the original pointer files to remain unchanged
in recovery storage. `context_matches` must also hold for acknowledgement.

Only an incomplete, unacknowledged receipt at an exact complete before/after state exposes
`can_acknowledge:true` and a fresh token. An acknowledgement writes only receipt metadata.
Its original incomplete status and error remain available; it never retries, rolls back,
restores, removes a lock or deletes recovery files. File changes invalidate a checked token,
and completed acknowledgements cannot be replayed. Partial/interrupted states need recovery
before acknowledgement. Verified interrupted publication can use the explicit resume below.

Catalog pages contain at most 100 entries (20 by default), scanning at most 10,000 receipts.
Malformed neighboring receipts are reported separately; unindexed names are preserved.
Receipt reads and saved acknowledgement metadata are limited to 16 MiB.

## Resuming verified publication (0.37)

**Check LFS receipt** reports `can_resume:true` when an incomplete, unacknowledged operation
has only already-published assets or absent destinations with fully verified staged assets
and intact original pointers. Use **Preview LFS resume**, inspect the counts, then
**Resume verified publication**. These controls are available in English and French.

```sh
cysynck git materialization-resume PROJECT_ID OPERATION_ID
cysynck git materialization-resume PROJECT_ID OPERATION_ID --apply true --plan PREVIEW_TOKEN
```

POST `/api/projects/ID/git-materialization-resume` accepts `{"receipt":"OPERATION_ID"}` for
preview, adding `apply:true` and `plan` to apply. The existing C/C++ ABI exposes the same JSON
route. Preview returns `already_published`, `pending_publications`, `check`, `can_resume`,
`resumed:false` and a token binding the receipt, profile/project settings, Git configuration,
current raw index, staged entries/flags, HEAD and all checked file fingerprints.

Application acquires the shared integration lock and repeats the inspection. It rehashes
each remaining staged asset and original pointer, preserves pointer permissions/Windows DACL,
and publishes only into an absent destination. Existing destinations and already-published
assets are not rewritten. No cache, Git LFS helper, filter or fetch is required. The receipt's
original selection is retained; callers cannot broaden it with a new path list.

If every asset was already published before the interruption, applying the preview completes
only the journal. Success reports `resumed:true`, `status:"complete"` and the exact final
`files_written`. Optional `resume` metadata records the attempt, start, completion and prior
journal count. The original materialization `error`, if present, remains as interruption
history. Receipt catalogs expose resume status. Completed/acknowledged operations cannot be
resumed, and changed files or configuration invalidate the preview.

A failed attempt keeps the receipt incomplete and preserves originals, remaining staging and
already-published files. The latest attempt error is retained (up to 2048 bytes). A new preview
can continue from the verified remaining state; after 1000 attempts manual inspection is
required. No rollback, automatic retries or lock removal is performed. Interrupted staging,
missing/corrupt prepared assets, changed originals, unknown entries, changed Git context, and
mixed `before`/publication states remain blocked for manual recovery.

Pause external writers during recovery. Checks and publication are not one filesystem
transaction, and the CySynck lock does not lock other applications. Native Linux/macOS runtime
validation and real power-loss testing remain required.
