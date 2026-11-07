---
id: git-storage
title: Git exchange storage
description: Usage, commands and limits for Git exchange storage.
section: Git
order: 43
parent: git-setup
revision: 1
---

# Git exchange storage {#git-exchange-storage}

CySynck 0.14 adds explicit cleanup of the current profile's Git exports and unused LFS
objects in the project's exchange directory. The CLI, C/C++ API, native desktop and Web
console share the same preview and application rules. Cleanup is never scheduled automatically.

## Preview and apply {#preview-and-apply}

```sh
# Inspect local exports and orphan LFS objects; do not remove anything.
cysynck git storage game-team

# Select exports that peers no longer need, then review the resulting counters and errors.
cysynck git storage game-team --exports EXPORT_ID_1,EXPORT_ID_2

# Apply that exact selection with the returned plan token.
cysynck git storage game-team --exports EXPORT_ID_1,EXPORT_ID_2 --apply true --plan TOKEN
```

Omitting `--exports` selects no exports: application then removes only unreferenced LFS
objects. In either interface, select exports and preview again before applying. Removing
an export stops later imports of that export by other peers once the deletion propagates
through Syncthing. There is no remote acknowledgement or distributed retention policy.

The authenticated POST route `/api/projects/ID/git-storage` accepts
`{"exports":["EXPORT_ID"],"apply":false}` for preview and the same selection with
`"apply":true,"plan":"TOKEN"` for application. Its response includes `exports`,
`selected_exports`, `objects`, `stored_lfs_bytes`, `retained_objects`, `missing_objects`,
`unused_objects`, `reclaimable_bytes`, `ignored_entries`, `errors`, `can_prune` and `plan`.
Application adds `removed_exports`, `removed_objects`, `reclaimed_bytes` and `failures`.
Counts describe the reviewed plan; reclaimable/reclaimed bytes are logical file sizes,
which may differ from physical disk space on compressed or deduplicated filesystems.

## Ownership and consistency {#ownership-and-consistency}

Only `exchange/peers/LOCAL_PEER_ID` is eligible. No local Git objects, refs, working files,
approved LFS caches or other peer namespaces are removed. LFS dependencies are collected
from every retained version-2 manifest; a shared object is protected until no retained
local export references it. Version-1 exports have no transported LFS inventory.

Preview hashes manifest bytes and records sizes and modification times for catalog files
and known objects. A changed selection, new export, changed manifest or object invalidates
the token. Selected bundles are fully hashed before deletion. Malformed, unsupported,
linked or incomplete retained metadata blocks cleanup. Scans are limited to 10,000
manifests and 100,000 directory entries; exceeding a limit also blocks application.

Application shares a local filesystem lock with export publication, unpublishes each
selected manifest before removing its bundle, and scans the remaining catalog again
before deleting objects. Object deletion uses fixed-depth lowercase SHA-256 paths and
never recurses. Unknown files, temporary files and unindexed bundles are preserved for
manual review. A partial failure is reported; export removal failures stop LFS pruning.
The lock coordinates local CySynck writers; it is not a distributed Syncthing lock.
Each installation must keep its own peer identity and must not write another peer's namespace.
