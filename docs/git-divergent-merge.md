# Reviewed divergent Git merge (0.24)

Use **Preview repository merge** in the Git branch comparison view, then **Apply reviewed merge**
when the whole-repository preview is clean. CyUI and Web default to English and include French.
The CLI and C/C++ SDK use the same core operation:

```sh
cysynck git merge PROJECT_ID PEER_ID BRANCH
cysynck git merge PROJECT_ID PEER_ID BRANCH --apply true --plan PREVIEW_TOKEN
```

POST `/api/projects/PROJECT_ID/git-merge` accepts `{"peer":"PEER","branch":"main"}` for preview,
or adds `"apply":true,"plan":"PREVIEW_TOKEN"` for application. Optional `base`, `head` and
`local_ref` pin the branch context just as for [fast-forward integration](git-integration.md).
This is an explicit action; background bundle exchange never applies merges.

## What is reviewed

This route requires genuinely divergent, complete SHA-1 or SHA-256 histories with exactly one
common ancestor. Fast-forwards and unborn local branches use `git integrate`. Unrelated histories,
multiple merge bases, detached HEAD and incomplete repositories need manual handling.

The full result is computed with [`git merge-tree --write-tree`](https://git-scm.com/docs/git-merge-tree)
in a private bare repository under `STATE/git-merge-work/RANDOM_ID/`. A read-only Git alternates
file gives that temporary repository access to existing source objects. New trees and blobs are
written only into the private workspace. Source HEAD, refs, index, working files, configuration
and object storage are preserved during preview. No merge commit is created by preview.

Preview returns the existing integration fields, plus `integration_kind:"merge"`, `merge_base`,
`merge_clean`, `conflicts`, `result_tree`, `result_commit:null`, `merge_identity` and `merge_message`.
`files_after` is the complete merged inventory when the tree is clean. Conflict paths come from
Git's NUL-delimited conflict records. Conflicted or unsupported results have no applicable token.
The separate [three-way file review](git-merge-review.md) remains available to inspect conflicts.

Rename detection and renormalization are disabled. Paths are validated across the local, peer,
ancestor and resulting trees, including unchanged files. Case-only renames and case aliases,
symlinks, submodules, LFS pointers and active checkout conversions require manual handling.
Custom/default merge rules and nonempty `.git/info/attributes` are refused. Configured external
merge drivers, filters, hooks, rerere and network access are disabled in the private workspace.
The effective user attributes file is disabled as in fast-forward integration; committed and
system attributes must pass the checks.

Case checks use Windows ordinal comparison on Windows and ASCII folding on POSIX. Unicode
normalization and filesystem-specific aliases still need native platform validation.

Regular blobs retain the integration limits of 1 GiB each, 10,000 files per tree and 16 GiB per
tree. Content changed differently on both sides at the same path must be UTF-8 without NUL,
at most 512 KiB per side, with at most 16 MiB of overlapping text in total. One-sided regular
binary files can be carried unchanged by object identity. Each Git subprocess has a 120-second
timeout. Git must support `merge-tree --write-tree --merge-base` and `check-attr --source`;
this version was tested with Git 2.45.1.

## Applying a clean result

A valid Git `user.name` and `user.email` are required and shown by preview. Author/committer
environment overrides must be unset. The token binds the identity, message, complete result
tree, branch context, configuration, index and unrelated local-data metadata. Dates and random
workspace names are excluded, so an unchanged preview remains stable across requests.

Application uses the same clean-index/raw-working-file checks, local-data collision checks and
repository integration lock as fast-forward integration. It recomputes the result after acquiring
the lock and again before publishing objects. A two-parent unsigned commit is created privately
using [`git commit-tree`](https://git-scm.com/docs/git-commit-tree), with the reviewed local commit
as first parent and peer commit as second parent. An incremental local bundle, bounded to 64 MiB,
imports the new immutable objects without updating refs. No network fetch is used.

The new commit ID is saved to the incomplete receipt before native Git fast-forwards the local
branch to it. Git updates HEAD, index, reflogs, `ORIG_HEAD` and working files; post-checks verify
the exact resulting tree's raw file identities, index cleanliness, executable permissions on
POSIX and preserved local-data metadata. Success returns `result_commit`, `applied:true` and
`status:"complete"`. Peer tracking refs remain available as before.

Existing conflicts, staged or modified tracked files and local path collisions are refused.
Unrelated untracked and ignored regular files are preserved. Close other Git clients and avoid
concurrent edits: this lock is specific to CySynck and cannot make external programs participate
in one filesystem transaction. See [local-data freshness](git-integration.md#local-data-freshness).

## Receipts and interruption

Merge receipts use the existing `git-integrations`, `git-integration-check` and
`git-integration-acknowledge` routes and private `git-integrations/PROJECT/ID.json` location.
They add the merge kind, result tree, common ancestor, reviewed identity and result commit.
Use core 0.24 or later to inspect these receipts; older fast-forward receipts remain supported.

Checks recompute the merged tree in a fresh private workspace and verify it against the receipt.
They also verify the merge commit's ordered parents, tree, author/committer identity and message when available, the
current branch, index, raw tracked files and preserved local metadata. An interrupted receipt
can be acknowledged at its exact original or integrated state. Acknowledgement preserves its
original status/error and does not retry, reset, commit or roll back Git files.

Application is not transactional across bundle import, checkout and receipt updates. Failure
can leave unreferenced objects, a completed checkout or other partial Git state. CySynck preserves
the incomplete receipt and source repository for inspection. Existing Git locks and leftover
`.git/cysynck-integration.lock` are never removed by receipt review.

Temporary workspaces are removed on ordinary completion/failure using guarded individual file
and empty-directory removals. Links or unexpected entries stop cleanup, and a crash can leave
private objects or bundles behind. No recursive deletion follows the alternates file into the
source repository. Inspect abandoned private workspaces before manual cleanup.
