# Reviewed Git branch integration

From 0.19, an imported branch head can update the local project by fast-forward.
The operation updates the currently selected local branch, the Git index and working
files. It also supports a new repository whose local branch has no first commit yet.
It creates no merge commit and never rewinds or merges divergent history.

## Preview and apply

Import and review a peer's bundle first. Then request a fresh integration preview:

```sh
cysynck git integrate game-team PEER_ID main
cysynck git integrate game-team PEER_ID main --apply true --plan PREVIEW_TOKEN
```

Optional `--base COMMIT|unborn`, `--head FULL_OID` and `--local-ref refs/heads/BRANCH`
pin the comparison context. The preview token also binds the project, state profile,
configuration digest and exact before/after tree inventory. Application inspects the
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
`files_before`, `files_after`, `file_count` and `plan`. Blocked previews provide reasons
and no token. Invalid repository context can return an error instead. Success returns
`applied: true`, `status: "complete"`, `receipt` and `receipt_path`.

## Current scope

- An ordinary repository rooted exactly at the project folder, with its own physical
  `.git` directory and a selected local branch; complete SHA-1 and SHA-256 histories.
- A clean index and byte-for-byte working files. Raw file hashes are checked even when
  Git's stat cache could hide modifications. Assume-unchanged/skip-worktree flags and
  changed executable bits are refused. Staged changes are preserved by refusal.
- **All untracked and ignored files block integration**, even when they appear unrelated.
  Preserve them outside this project first. No automatic stash or deletion is performed.
- Regular files only: at most 10,000 per tree, 1 GiB per blob and 16 GiB per tree.
  Linked files, submodules, unsafe paths, metadata aliases and case collisions are refused.
  Case-only renames require manual Git handling.
- LFS pointers, active checkout filters, encoding conversion, text/EOL conversion and
  ident expansion are refused. Use [verified separate extraction](git-extraction.md)
  for these revisions. This integration route does not hydrate LFS assets yet.
- Sparse working trees, split/shared indices, linked working trees, object alternates,
  grafts, shallow/promisor repositories and in-progress Git operations require manual
  handling. Repository/configuration `GIT_*` overrides must be unset.

An installation of Git supporting `check-attr --source` is required (Git 2.43 or later).
The effective user `core.attributesFile` is disabled for this operation; repository and
system attributes are inspected and must satisfy the rules above. Files are checked
again after checkout against their reviewed raw Git object IDs.

## Execution and interrupted operations

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

Each Git helper has a 120-second timeout. The complete fast-forward and receipt update
are not one filesystem transaction. Receipts identify the reviewed before/after commits
for inspection; they are not backups of uncommitted work. Native Linux/macOS execution
remains part of platform validation.

Git behavior references: [fast-forward and ignored-file handling](https://git-scm.com/docs/git-merge),
[attributes from a selected tree](https://git-scm.com/docs/git-check-attr).
