---
id: git-review
title: Review imported Git branches
description: Usage, commands and limits for Review imported Git branches.
section: Git
order: 32
parent: git-setup
revision: 1
---

# Review imported Git branches {#review-imported-git-branches}

CySynck compares the current committed `HEAD` with a previously imported
`refs/remotes/cysynck/PEER/BRANCH`. Review is local and read-only: it does not check out files,
merge, refresh the index or fetch missing Git/LFS objects. Dirty and staged changes stay local.
From 0.16, a valid branch without its first local commit is also supported: `base:null`,
`relation:"unborn"`, zero local-only commits and the complete imported tree compared with an
empty tree. This includes files unchanged by the peer's latest commit. See [first setup](doc:git-setup).

```sh
cysynck git branches PROJECT_ID
cysynck git review PROJECT_ID PEER_ID BRANCH
cysynck git review-file PROJECT_ID PEER_ID BRANCH 'relative/path.cpp'
```

The overview returns the local and imported commit IDs, merge base when one exists,
commit counts unique to each side and a relation: `identical`, `fast_forward`, `local_ahead`,
`diverged` or `unrelated`. Changes compare the two committed trees directly; this is not a
three-way merge preview or a comparison limited to changes since their common ancestor.
Renames appear as deletion/addition pairs. Mode changes, symbolic links and submodules remain visible.

From 0.23, the separate [three-way committed review](doc:git-merge-review) compares both branches
with their common ancestor and offers bounded per-file text proposals, including diff3 conflicts.

The native Git exchange view lists imported branches and renders file previews with CyUI's
side-by-side and unified diff viewer. The Web console provides a changed-file list and unified
patch. Both default to English and include French translations.

## Text and metadata previews {#text-and-metadata-previews}

Text preview is limited to 512 KiB per side and valid UTF-8 without NUL bytes. Larger, binary,
non-UTF-8 and submodule changes return metadata and an explanatory message. Git symlink targets
are displayed from their committed blob; filesystem links are not followed. Canonical Git LFS
pointers are identified with their asset SHA-256 and size; the patch compares pointers and does
not load or compare the asset payload.

Object sizes come from Git before reading content. Listings use NUL delimiters, and selected
paths are passed as literal pathspecs, so spaces, Unicode and wildcard characters are preserved.
Patches use Git's quoted-path syntax. External diff programs, textconv, replacement objects and
network protocols are disabled. The implementation uses the documented
[raw Git diff format and preview flags](https://git-scm.com/docs/git-diff).

At most 10,000 changed files or imported refs are listed; paths are limited to 4096 UTF-8 bytes
and text patches to 4 MiB. Exceeding a bound fails explicitly instead of reporting a partial list.

## Stable review context {#stable-review-context}

File-preview requests can include the overview's `base`, `head` and `local_ref`. If a pinned ref has
changed, the request is refused and the interface must refresh. Ref identities are checked again
after comparison. Interfaces pin these values automatically and discard stale responses.

```sh
cysynck git review-file PROJECT_ID PEER_ID BRANCH path.cpp --base LOCAL_COMMIT --head PEER_COMMIT
cysynck git review-file PROJECT_ID PEER_ID BRANCH path.cpp --base unborn --head PEER_COMMIT --local-ref refs/heads/main
```

`local_ref` is the symbolic HEAD target, or null for detached HEAD (`--local-ref detached` in
the CLI). Pinning it also detects a switch between two unborn branches. Corrupt or non-commit
HEAD targets fail explicitly and are never interpreted as empty history.

The local token-protected API and C/C++ SDK expose POST project routes:

| Route | Body |
| --- | --- |
| `/api/projects/ID/git-branches` | `{}` |
| `/api/projects/ID/git-review` | `{"peer":"PEER","branch":"main"}` |
| `/api/projects/ID/git-review-file` | `{"peer":"PEER","branch":"main","path":"file.cpp","base":"...","head":"..."}` |

From 0.19, the branch comparison view also offers [reviewed fast-forward integration](doc:git-integration)
as a separate explicit action. Eligible divergent histories support a separate [reviewed merge](doc:git-divergent-merge).

Version 0.15 adds [paginated history and per-commit review](doc:git-history), including root
commits and explicit selection of a merge parent. These views use the same preview bounds.
