# Imported Git history and commit review

CySynck 0.15 lists the commits reachable from an imported peer branch and compares a selected
commit with one of its parents. This complements the comparison of the current local HEAD
against a peer's head. The C++ core serves the CLI, SDK, native CyUI app and English/French Web console.

```sh
cysynck git history PROJECT PEER BRANCH --limit 50
cysynck git history PROJECT PEER BRANCH --limit 50 --offset 50 --base LOCAL_HEAD --head PEER_HEAD
cysynck git commit PROJECT PEER BRANCH FULL_COMMIT_ID
cysynck git commit-file PROJECT PEER BRANCH FULL_COMMIT_ID relative/path.cpp --parent 1
```

## History pages

History uses topological order, including commits reached through secondary merge parents.
Each entry contains `oid`, ordered `parents`, `subject`, `author` and `committer`. The latter
two include the recorded name, email and ISO date. Git's mailmap, notes and signature-verification
programs are disabled; identities are recorded metadata and are not proof of trust.

Pages default to 50 commits and accept `limit` from 1 to 100 and `offset` from 0 to 100000.
The response includes `commits`, `offset`, `limit`, `next_offset` and `truncated`, along with
the branch review context (`base`, `head`, `local_ref`, `peer`, `branch`, `tracking_ref`). Continuation pages
require the original `base` and `head`. A changed local HEAD or imported ref invalidates the
request; refresh the branch review before continuing. `next_offset:null` indicates the end or
the offset bound; `truncated:true` distinguishes that bound from the actual end of history.

From 0.16, history is also available before the first local commit. Pass `base:null` to the API
or `--base unborn` in the CLI and retain `head` plus `local_ref` from the first response. This
ref pin detects switching between unborn branches; a new first commit invalidates the null
base. Detached HEAD uses `local_ref:null` (`--local-ref detached`).

Metadata fields are limited to 16 KiB, parent lists to 256 entries and captured history output
to 8 MiB. Invalid UTF-8 or incomplete metadata fails explicitly. Complete, non-shallow
repositories are required, so a shallow boundary cannot be mistaken for a root commit.

## Per-commit comparisons

Commit selection requires a complete SHA-1 or SHA-256 object ID reachable from the selected
imported branch. Abbreviations, revision expressions and unrelated local objects are refused.

The default comparison uses the first parent. For a merge commit, `parent:2` compares against
its second parent, and so on. Parent indexes start at one. A root commit has `parent:0` and
compares recursively against an empty tree without creating an empty-tree Git object. Root,
empty and merge commits remain inspectable. This is a two-tree comparison, not a combined
merge diff or a merge simulation.

The response includes `commit`, `parent`, `comparison_base` (null for a root) and
`comparison_head`. `base` and `head` always retain the branch context for stale-request checks.
Changed-file lists and previews share the [branch review limits](git-review.md): literal paths,
512 KiB text per side, binary/submodule metadata and LFS pointer identification. External diff,
textconv, hooks, replacement objects and network protocols are disabled. No checkout, merge,
ref change or index refresh occurs, and uncommitted work stays intact.

The implementation uses Git's documented [history formats](https://git-scm.com/docs/git-log)
and [tree comparisons](https://git-scm.com/docs/git-diff). Empty trees are hashed for the
repository's object format without writing objects.

## API and SDK

Authenticated POST routes are available through the existing C and C++ SDKs:

| Route | Body |
| --- | --- |
| `/api/projects/ID/git-history` | `{"peer":"PEER","branch":"main","limit":50,"offset":0}` |
| `/api/projects/ID/git-commit` | `{"peer":"PEER","branch":"main","commit":"FULL_OID","parent":1}` |
| `/api/projects/ID/git-commit-file` | `{"peer":"PEER","branch":"main","commit":"FULL_OID","path":"file.cpp","parent":1}` |

All routes accept optional `base`/`head` pins; these are required for history continuation.
The interfaces supply pins automatically and ignore stale responses. Root requests omit
`parent` or set it to zero. Applying a commit or merging a branch remains an explicit Git operation.
