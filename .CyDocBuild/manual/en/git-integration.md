---
id: git-integration
title: Reviewed Git branch integration
description: Usage, commands and limits for Reviewed Git branch integration.
section: Git
order: 35
parent: git-setup
revision: 1
---

# Reviewed Git branch integration {#reviewed-git-branch-integration}

From 0.19, an imported branch head can update the local project by fast-forward.
The operation updates the currently selected local branch, the Git index and working
files. From 0.20, unrelated regular local files and ignored build outputs can remain
in the project. It also supports a new repository whose local branch has no first commit yet.
It creates no merge commit and never rewinds or merges divergent history.

From 0.24, the separate [`git merge` operation](doc:git-divergent-merge) can integrate conflict-free
divergent branches through a whole-repository preview and an explicit two-parent commit.
From 0.25, that operation also supports explicit whole-file local/peer choices for bounded
text conflicts, followed by a fresh preview. See its guide for deletion choices and limits.

## Preview and apply {#preview-and-apply}

Import and review a peer's bundle first. Then request a fresh integration preview:

```sh
cysynck git integrate game-team PEER_ID main
cysynck git integrate game-team PEER_ID main --apply true --plan PREVIEW_TOKEN
```

Optional `--base COMMIT|unborn`, `--head FULL_OID` and `--local-ref refs/heads/BRANCH`
pin the comparison context. The preview token also binds the project, state profile,
configuration digest, exact before/after tree inventory and local-data metadata fingerprint.
Application inspects the
repository again; changed refs, staged or working files and configuration invalidate it.
Preview does not update the index or working files. The native and Web branch comparison
views expose **Preview integration** and **Apply fast-forward** in English and French.
The selected historical commit view continues to offer separate extraction.

The C++ request API and version 1 C ABI use:

```text
POST /api/projects/PROJECT_ID/git-integrate
{"peer":"PEER_ID","branch":"main"}
{"peer":"PEER_ID","branch":"main","apply":true,"plan":"PREVIEW_TOKEN"}
```

Preview returns `can_integrate`, `errors`, `relation`, `base`, `head`, `local_ref`,
`files_before`, `files_after`, `file_count`, `local_data` and `plan`. `local_data` includes
`files`, `directories`, `bytes` and a SHA-256 `fingerprint` of the sorted metadata inventory.
Blocked previews provide reasons
and no token. Invalid repository context can return an error instead. Success returns
`applied: true`, `status: "complete"`, `receipt` and `receipt_path`.

## Current scope {#current-scope}

- An ordinary repository rooted exactly at the project folder, with its own physical
  `.git` directory and a selected local branch; complete SHA-1 and SHA-256 histories.
- A clean index and byte-for-byte working files. Raw file hashes are checked even when
  Git's stat cache could hide modifications. Assume-unchanged/skip-worktree flags and
  changed executable bits are refused. Staged changes are preserved by refusal.
- Unrelated untracked and ignored regular files remain in place. Files that collide with
  incoming file names or required parent directories block the operation, even if Git ignores
  them. Local descendants and empty local folders also block replacement by an imported file.
  Case aliases and linked/special entries are refused. Untracked `.gitattributes` files are
  refused because they could change checkout behavior. No stash or local-data deletion occurs.
- The filesystem scan includes at most 100,000 entries outside the root `.git`, with at most
  16 MiB of local metadata. Counts include standalone empty local folders; directories shared
  with a tracked tree are omitted from the local-directory inventory.
- Regular files only: at most 10,000 per tree, 1 GiB per blob and 16 GiB per tree.
  Linked files, submodules, unsafe paths, metadata aliases and case collisions are refused.
  Case-only renames require manual Git handling.
- LFS pointers require the explicit [`--lfs-policy pointers` policy](doc:git-lfs-integration)
  from 0.34; default integration refuses them. Working files remain exact pointers and assets
  are materialized separately. Other active checkout filters, encoding/text/EOL conversion and
  ident expansion are refused. [Verified separate extraction](doc:git-extraction) remains available.
- Sparse working trees, split/shared indices, linked working trees, object alternates,
  grafts, shallow/promisor repositories and in-progress Git operations require manual
  handling. Repository/configuration `GIT_*` overrides must be unset.

An installation of Git supporting `check-attr --source` is required (Git 2.43 or later).
The effective user `core.attributesFile` is disabled for this operation; repository and
system attributes are inspected and must satisfy the rules above. Files are checked
again after checkout against their reviewed raw Git object IDs.

## Local-data freshness {#local-data-freshness}

The local inventory uses relative paths, entry types and permissions, plus regular-file sizes
and modification timestamps. It includes ignored files without relying on ignore rules. Its
metadata fingerprint is part of the preview token and is checked again after checkout. Creating,
removing, renaming or changing the recorded metadata requires a new preview. The receipt retains
the counts, logical byte total and fingerprint.

Directory timestamps are omitted because checkout may add or remove tracked children in shared
folders. Local file contents are not loaded or hashed: a large ignored cache does not need to be
read. The fingerprint does not detect a same-size rewrite whose modification timestamp is restored,
and does not represent a content backup. Byte hashes are still verified for all tracked Git files.
Stop background builds and avoid concurrent edits while applying; post-check failure preserves an
incomplete receipt and does not undo an already completed Git update.

## Execution and interrupted operations {#execution-and-interrupted-operations}

CySynck invokes native `git merge --ff-only` with hooks, configured filter commands,
fsmonitor, recursive submodules, automatic stash, automatic maintenance and network
protocols disabled. Explicit options override branch squash/autostash preferences.
No signing helper, editor, external diff, merge tool or LFS network helper is invoked.
Existing configuration is preserved. Native Git may update its index, local branch,
reflogs and `ORIG_HEAD` as part of the fast-forward.

Before invoking Git, CySynck saves a private `incomplete` receipt under
`STATE/git-integrations/PROJECT_ID/ID.json`. Success verifies HEAD and each resulting
file and saves `complete`. A failed Git command, write failure or interrupted process
can leave changes in the repository. CySynck preserves that state and receipt; it does
not reset, roll back or retry automatically. Inspect Git status and the receipt before
recovering with normal Git commands, then request a new preview.

An exclusive `.git/cysynck-integration.lock` directory serializes CySynck integrations
across state profiles. After a crash, inspect the repository and confirm no integration
is running before removing that empty directory. Existing Git lock files are preserved.
This directory is not a lock recognized by other Git clients: close other Git clients
and avoid concurrent repository edits while applying. The preview and native Git guards
cannot make external editors or independent Git processes part of one atomic transaction.

## Receipt history and review {#receipt-history-and-review}

From 0.21, the Git view in CyUI and the Web console exposes **Git integration receipts**.
Load the history, select **Check integration receipt**, then use **Mark as reviewed** for an
interrupted operation that passes the checks. This records a review; it leaves the original
`incomplete` status, error and repository intact.

```sh
cysynck git integrations PROJECT_ID --offset 0 --limit 20
cysynck git integration-check PROJECT_ID RECEIPT_ID
cysynck git integration-acknowledge PROJECT_ID RECEIPT_ID --plan CHECK_TOKEN
```

The corresponding authenticated POST routes are:

- `/api/projects/PROJECT_ID/git-integrations`, with `{"offset":0,"limit":20}`.
- `/api/projects/PROJECT_ID/git-integration-check`, with `{"receipt":"RECEIPT_ID"}`.
- `/api/projects/PROJECT_ID/git-integration-acknowledge`, with
  `{"receipt":"RECEIPT_ID","plan":"CHECK_TOKEN"}`.

History returns `records`, `errors`, `total`, `offset`, `limit`, `has_more` and `truncated`.
Summaries omit tree listings; `needs_attention` identifies incomplete, unreviewed receipts.
Pages contain at most 50 records, sorted by creation time then ID descending. A scan reads
at most 1,000 directory entries and 64 MiB, with a 16 MiB bound per receipt. `truncated`
means the state directory also needs manual inspection; its omitted entries are not counted
in `total`. Invalid neighboring records are reported separately without hiding valid history.

A check returns `position` (`before`, `after`, `other` or `unknown`), the current local commit
and branch, `tracked_matches`, `local_data_matches`, `lock_present`, `errors`,
`can_acknowledge` and a `plan` when acknowledgement is available. It derives both expected
trees from full commit IDs and verifies the receipt listings against those objects. Imported
tracking refs can have been removed or advanced. Git object storage must still contain the
recorded commits and blobs. Verification uses the same offline, hook/filter-disabled helpers
and regular-file restrictions as integration; it does not refresh the index.

Acknowledgement is available only for an unreviewed incomplete receipt at its exact original
or integrated commit, on its recorded local branch, with matching index/raw working files and
matching local metadata when the receipt contains it. Receipts from 0.19 have no local inventory:
`local_data_matches` is `null`, disclosed in the interfaces, while tracked files are still checked.
Local metadata changes, staged changes, another branch/commit, unavailable objects and Git
operations or lock files block review until the repository is inspected and reconciled manually.
Complete receipts may be checked at any time; a later legitimate commit can differ from an old
receipt without changing its recorded completion status.

The check token binds the receipt, repository branch/commit, index, effective Git configuration
and current local inventory. Acknowledgement rechecks these before saving `acknowledged_at`
and the inspection evidence. A changed check or already reviewed receipt requires another
inspection and cannot replay its old token. Receipt paths are derived from the current state
directory and validated ID; stored path fields cannot redirect reads or writes. Receipts contain
local diagnostic evidence, without an authenticity signature or a content backup of local files.

Checks and acknowledgement never remove `.git/cysynck-integration.lock`, Git locks or working
files, and never advance HEAD, retry a merge or reset the index. After a crash, the lock requires
the manual inspection described above. Avoid concurrent builds, edits and Git operations during
checks and acknowledgement as during integration; external processes do not honor the CySynck
lock and the inspection is not a filesystem transaction.

Each Git helper has a 120-second timeout. The complete fast-forward and receipt update
are not one filesystem transaction. Receipts identify the reviewed before/after commits
for inspection; they are not backups of uncommitted work. Native Linux/macOS execution
remains part of platform validation.

Git behavior references: [fast-forward and ignored-file handling](https://git-scm.com/docs/git-merge),
[attributes from a selected tree](https://git-scm.com/docs/git-check-attr).
