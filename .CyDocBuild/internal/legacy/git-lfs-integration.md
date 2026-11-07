# Reviewed fast-forward with LFS pointers (0.34)

An explicit pointer-only policy lets an imported Git branch update an unborn or clean local
branch containing canonical Git LFS pointers. This is useful for initial imports and server
checkouts that keep pointers until assets are needed. The default policy still refuses LFS.
Divergent LFS merging and automatic replacement of materialized working assets remain pending.
From 0.39, [reviewed pointer restoration](git-dehydration.md) can separately prepare unchanged
expanded working assets for this integration, preserving their original bytes in recovery.

```sh
cysynck git integrate PROJECT PEER main --lfs-policy pointers
cysynck git integrate PROJECT PEER main --lfs-policy pointers --apply true --plan PREVIEW_TOKEN
```

The authenticated POST `/api/projects/PROJECT/git-integrate` accepts `lfs_policy:"pointers"`
or `lfs_policy:"reject"` (the default). Repeat exactly the selected policy for application.
The existing plan binds profile/project, revision, configuration, index, tracked bytes,
local-data metadata and policy. New previews are required when those observations change.
The preview's `project_hash` also pins the complete registered project settings, so editing
those settings requires a fresh preview for either integration kind.
The normal branch, path, lock, first-import and no-overwrite checks still apply.

## Resulting checkout

The Git branch and index advance to the imported head. Working files contain the exact
committed pointer text. Integration creates no asset cache objects, downloads nothing and
invokes no LFS helper or configured filter command. The saved `git_lfs` export policy remains
independent. Bundle import may already have populated the cache through its separate verified
LFS exchange workflow.

After application, use [cache verification/refill](git-cache.md) if needed, then preview
[materialization](git-materialization.md) to expand exact committed pointers from verified
local cache bytes. Hydration is a separate reviewed operation with its own receipt and
preserved originals. It is not an atomic part of branch integration.

Tracked working files must match their committed raw bytes. Already materialized assets,
local changes, staged changes and assume-unchanged/skip-worktree entries block further
integration. Preserve or reconcile them using an appropriate Git workflow before trying
again. The explicit pointer-restoration step handles unchanged expanded assets; CySynck does
not automatically dehydrate, stash or overwrite them as part of branch integration.

## Attributes and bounds

- Only canonical LF-terminated v1 pointers with SHA-256 identities are accepted. Malformed,
  legacy or extended pointers remain refused.
- A `filter=lfs` attribute is permitted only on paths whose committed blobs are canonical
  pointers. Other checkout filters and text/encoding/EOL/ident conversions remain refused.
  Common `filter=lfs diff=lfs merge=lfs -text` declarations work for fast-forward; no merge
  driver is executed. This policy is unavailable for the divergent `git-merge` operation.
- Existing limits remain 10,000 regular files per tree, 1 GiB per ordinary Git blob and
  16 GiB of Git blob bytes. LFS references add bounds of 4,096 unique identities, 4 GiB per
  asset and 16 GiB of unique referenced asset bytes per tree. Conflicting sizes within a tree
  for the same SHA-256 identity are refused. These are before/after tree inventories; bundle
  exchange and cache inspection perform their separate committed-history checks.

Previews/results expose `lfs_policy`, `lfs_before` and `lfs_after`. Each inventory summary has
`files` (pointer paths), `objects` (unique identities) and `bytes` (unique referenced asset
bytes). Repeated paths to the same asset count once in `objects`/`bytes`; empty assets count
as objects. `files_before`/`files_after` annotate each pointer with its `lfs` identity/size.

## Receipts and interfaces

Existing integration history, receipt checks and reviewed-interruption acknowledgement work
with this policy. Checks derive the pointer trees again from recorded commits and validate
the stored LFS counts. Receipts describe the pointer checkout state. Later materialization
changes its raw working bytes, so an integration receipt check reports that difference;
use the separate materialization receipt to verify expanded files and preserved pointers.

CyUI/Web expose an unchecked-by-default option, a pointer-only explanation and preview counts
in English and French. CLI and C/C++ use the existing request API and C ABI version 1. Native
Linux/macOS/Unreal execution and interaction with the new GUI controls remain separate checks.
