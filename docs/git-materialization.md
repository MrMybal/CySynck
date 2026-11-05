# Offline LFS working-tree materialization

After checking out a committed revision containing LFS pointer files, CySynck can replace
those exact pointers with verified assets already available in the local LFS cache. This is
an explicit operation, separate from bundle import, branch integration and revision extraction.
It performs no fetch and requires no Git LFS executable. Git remains responsible for checkout
and merging; reviewed branch integration still refuses LFS revisions.

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
configuration, approved cache location and pointer fingerprints. Reusing a stale plan fails.

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
