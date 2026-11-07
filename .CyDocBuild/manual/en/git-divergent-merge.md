---
id: git-divergent-merge
title: Reviewed divergent Git merge (0.27)
description: Usage, commands and limits for Reviewed divergent Git merge (0.27).
section: Git
order: 37
parent: git-integration
revision: 1
---

# Reviewed divergent Git merge (0.27) {#reviewed-divergent-git-merge-027}

Use **Preview repository merge** in the Git branch comparison view, then **Apply reviewed merge**
when the whole-repository preview is ready. CyUI and Web default to English and include French.
The CLI and C/C++ SDK use the same core operation:

```sh
cysynck git merge PROJECT_ID PEER_ID BRANCH
cysynck git merge PROJECT_ID PEER_ID BRANCH --apply true --plan PREVIEW_TOKEN
```

POST `/api/projects/PROJECT_ID/git-merge` accepts `{"peer":"PEER","branch":"main"}` for preview,
or adds `"apply":true,"plan":"PREVIEW_TOKEN"` for application. Optional `base`, `head` and
`local_ref` pin the branch context just as for [fast-forward integration](doc:git-integration).
This is an explicit action; background bundle exchange never applies merges.

## What is reviewed {#what-is-reviewed}

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
Git's NUL-delimited conflict records. Unresolved or unsupported results have no applicable token.
The separate [three-way file review](doc:git-merge-review) remains available to inspect conflicts.

## Choosing committed files for conflicts (0.25) {#choosing-committed-files-for-conflicts-025}

For supported UTF-8 text conflicts, CyUI and Web offer **Use local file** or **Use peer file**
for each reported path. A missing side is labelled **Use local deletion** or **Use peer deletion**.
Use the three-way file review to inspect committed versions before choosing. Each choice replaces
the **entire file**, including otherwise nonconflicting edits in that file. Other files keep
their automatic merge result. From 0.26, an editor can combine changes within a conflicted file.

Nothing is selected by default. Changing or clearing a choice disables application until another
preview succeeds. Every conflict needs a choice; partial selection remains blocked. Choices cannot
override clean files or select arbitrary objects or filesystem paths. Edited text is explicit,
as described below.

The CLI accepts a UTF-8 JSON file through `--resolutions FILE.json`. For example:

```json
[
  {"path":"Source/Game.cpp","side":"local"},
  {"path":"Notes.txt","side":"peer"}
]
```

```sh
cysynck git merge PROJECT_ID PEER_ID BRANCH --resolutions choices.json
cysynck git merge PROJECT_ID PEER_ID BRANCH --resolutions choices.json --apply true --plan PREVIEW_TOKEN
```

The API/C SDK accepts the same array in `resolutions`. Send the same choices with application;
the token binds their canonical path order and the complete resulting tree. Duplicate paths,
case aliases, unknown conflict paths, extra fields and unknown sides are refused. `local`/`peer`
entries contain only `path` and `side`; `edited` entries additionally require string `content`.
The array is bounded to 10,000 paths and 1 MiB. Fast-forward integration refuses this field.

Preview adds `conflict_files` (each path's nullable `local`/`peer` file metadata), canonical
`resolutions`, `unresolved_conflicts` and `result_ready`. `merge_clean` describes the original
automatic merge: it stays false when explicit choices resolve conflicts. `result_ready:true`
means the candidate tree is complete; `can_integrate` additionally requires all repository checks
to pass. `conflicts` retains the original paths even after resolution. An unresolved preview
exposes no applicable inventory or token; its diagnostic tree may contain conflict markers.

The private repository loads the automatic result into its own index, applies selected committed
blob identities/modes or deletions with [`git update-index --index-info`](https://git-scm.com/docs/git-update-index#_using_index_info),
then writes and validates the complete resulting tree. The source index, worktree and object
storage remain untouched during preview. Apply and receipt checks recompute the same choices.
Add/add and modify/delete text conflicts are supported. Structural/synthetic paths, binary or
non-UTF-8 conflicts, oversized text and the other restrictions below require manual Git handling.

## Editing conflict text (0.26) {#editing-conflict-text-026}

Choose **Edit merged text** for a supported conflict. CyUI and Web show the ancestor and both
committed versions in read-only views and seed an editable draft with the three-way proposal.
For modify/delete conflicts, the surviving committed text seeds the draft. Previous edits can
be reopened while the comparison remains open. These drafts are kept in interface memory until
you use them; refreshing or switching imported branches/projects discards them.

Remove conflict markers, combine the desired regions, then choose **Use edited text** and preview
again. Cancel closes the editor without changing its selected resolution. Preview and application
are disabled while an editor is open. Saving a draft invalidates the earlier plan. Other files
retain their automatic result or their own explicit resolution. This is a manual text editor;
it does not infer which changed lines should win.

CLI files and the existing C/C++ request route accept:

```json
[
  {"path":"Source/Game.cpp","side":"edited","content":"combined text\n"},
  {"path":"Notes.txt","side":"peer"}
]
```

Edited content must be valid UTF-8 without NUL, at most 512 KiB per file, and fit the existing
1 MiB serialized resolution-array bound. An empty string creates an empty regular file; it
does not delete it. Canonical Git LFS pointers are refused. Lines beginning, after optional
spaces/tabs/CR, with `<<<<<<<`, `|||||||`, `=======` or `>>>>>>>` are refused conservatively,
including longer marker runs. Legitimate text containing such lines needs a committed-file
choice or manual Git handling. This check helps catch leftover proposal markers; users still
review the final text.

When both committed sides exist, their file modes must agree for text editing. Otherwise use
a committed-file choice or resolve permissions in Git. With one deleted side, the surviving
file's mode is retained. `conflict_files[].editable` exposes this condition.

[`git hash-object -w --no-filters --stdin`](https://git-scm.com/docs/git-hash-object) creates edited
blobs only in the private workspace. The full resulting tree is validated before issuing a
plan. Requests, application and receipt checks use the exact supplied bytes, including CRLF and
a missing final newline. The Web textarea uses browser LF line endings; preview hashes reflect
the text actually submitted. Application sends the same edited content and token, imports the
reviewed objects and performs the existing verified checkout.

Edited text is stored in the private integration receipt for deterministic recomputation,
including checks before or after an interrupted checkout. Receipt summaries expose only an
`edited_files` count, alongside `resolved_conflicts`. Receipts using `side:"edited"` require
core 0.26 or later. Earlier receipts and committed-file choices remain supported.

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

## Choosing individual conflict regions (0.27) {#choosing-individual-conflict-regions-027}

The text editor also offers **Choose conflict regions** for unambiguous diff3 proposals.
Each region shows the ancestor, local and peer text. Choose **Use local region**,
**Use peer region** or **Use both (local then peer)** for every region, then select
**Build draft from selected regions**. This replaces the editor text, preserving the
proposal's surrounding automatic changes. It does not save or apply the draft: inspect and
retouch it, choose **Use edited text**, and run the repository preview before application.
Saved drafts can be reopened; region selectors start unresolved each time the editor opens.
Cancel keeps the existing file resolution. These are per-file text choices; repository-level
structural conflicts and file permissions still follow the existing integration checks.

`POST /api/projects/ID/git-merge-file` adds a `regions` object: `available`, `reason`,
`count`, `token` and ordered `segments`. Context segments contain `kind:"context"` and
`text`. Conflict segments contain `kind:"conflict"`, zero-based `id`, `local`, `ancestor`
and `peer` strings. Its count is exact even when Git caps its exit status at 127.
The maximum is 256 regions per file. Existing source lines starting with seven conflict-marker
characters disable the assistant to avoid treating source content as delimiters. Unsupported,
ambiguous, malformed or oversized proposals expose no selectable segments or token; the
manual editor and committed-file choices remain available within their existing restrictions.

Send the same branch context, path, `region_token` and a `region_choices` array of
`"local"`, `"peer"` or `"both"` in region order to the same route. The core regenerates the
proposal and checks its context-bound token before returning `composed_text`. Every region
needs a choice; invalid, missing or stale choices are refused. Caller-supplied segments are
never used. The array is bounded to 256 entries and 16 KiB. Composition remains read-only
and refuses output above the existing 512 KiB edited-text limit. No new C ABI is needed.

```powershell
cysynck git merge-file team alice main Source/Game.cpp
# Save ["local", "both"] to regions.json and use regions.token from the response.
cysynck git merge-file team alice main Source/Game.cpp --regions regions.json --region-token TOKEN
```

Both choices concatenate local then peer bytes without deduplication or an extra separator.
Region bodies and context retain the exact bytes emitted by
[`git merge-file --diff3 --stdout`](https://git-scm.com/docs/git-merge-file), including CRLF.
Git can insert a newline next to a conflict marker when an input lacked its final newline;
composition preserves that proposal behavior. Retouch the final newline in the editor if
needed. Web textareas normalize line endings to LF as before. The token identifies a file
proposal, not permission to apply a merge. Only the final edited text and the existing reviewed
integration plan determine application and receipts; per-region UI choices are not stored.

## Applying a reviewed result {#applying-a-reviewed-result}

A valid Git `user.name` and `user.email` are required and shown by preview. Author/committer
environment overrides must be unset. The token binds the identity, message, complete result
tree, explicit conflict choices, branch context, configuration, index and unrelated local-data metadata. Dates and random
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

Existing in-progress Git operations, unresolved choices, staged or modified tracked files and local path collisions are refused.
Unrelated untracked and ignored regular files are preserved. Close other Git clients and avoid
concurrent edits: this lock is specific to CySynck and cannot make external programs participate
in one filesystem transaction. See [local-data freshness](doc:git-integration#local-data-freshness).

## Receipts and interruption {#receipts-and-interruption}

Merge receipts use the existing `git-integrations`, `git-integration-check` and
`git-integration-acknowledge` routes and private `git-integrations/PROJECT/ID.json` location.
They add the merge kind, result tree, common ancestor, reviewed identity and result commit.
Use core 0.24 or later to inspect these receipts; older fast-forward receipts remain supported.
Receipts with explicit conflict choices require core 0.25 or later. They retain canonical
`resolutions`; paginated summaries add `resolved_conflicts` with the number of choices.

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
