# Offline Git and Git LFS exchange (0.13)

Each device keeps its own Git repository. Syncthing transports an exchange directory containing
Git bundles, SHA-256 manifests and Git LFS objects. A live `.git` directory is never synchronized.
CySynck does not run `git push`, contact an LFS server, check out files or merge branches during
export/import. Git remains an external tool; the inventory and object transport are C++20.

```sh
cysynck project add Game /work/game --id game-team --mode git-bundles --exchange-path /work/game-exchange
cysynck git export game-team
cysynck git inbox game-team
cysynck git import game-team PEER_ID BUNDLE_ID
```

Starting with 0.13, export and the Git mode's synchronize action include LFS by default.
The native Git exchange view and Web console expose the same option in English/French.
An export requires a committed attached branch. Uncommitted content stays local.

## What is exported

The Git bundle contains the attached branch and its reachable history. The LFS inventory scans
small Git blobs in that exact exported history, including merged ancestors, previous versions
and deleted assets. Repeated content is listed once by SHA-256. Objects reachable only from an
unrelated branch are excluded. The commit recorded in the manifest comes from the completed
bundle, so moving the local branch during export cannot change its declared head afterward.

Supported pointers are canonical Git LFS v1: the standard version URL, `oid sha256` and a
nonnegative byte count, with LF endings and less than 1024 bytes. Pointer extensions and
legacy Hawser pointers fail explicitly. Empty ordinary files need no LFS object.
The implementation follows the [official pointer and storage specification](https://github.com/git-lfs/git-lfs/blob/main/docs/spec.md)
and reads immutable objects with [Git's batch object interface](https://git-scm.com/docs/git-cat-file).

Local objects are read from `<git-common-dir>/lfs/objects/aa/bb/SHA256`. Linked Git worktrees
share that common cache. Git LFS itself is not required for CySynck's export/import; install it
to track new assets and materialize them through Git in your normal workflow. `lfs.storage`
overrides are currently refused rather than writing to an inferred custom/shared cache.
Shallow repositories are refused; missing promisor objects are not downloaded during inspection.

## Publication, reception and import

Version-2 exports publish these paths under the selected peer's exchange namespace:

```text
peers/PEER/BUNDLE.bundle
peers/PEER/BUNDLE.lfs.json
peers/PEER/lfs/objects/aa/bb/SHA256
```

Objects are copied and verified before publishing the final manifest. Publication uses a hard
link within the destination filesystem to avoid replacing a concurrently created cache object;
the filesystem must support hard links (for example NTFS, ext4 or APFS). Matching existing
objects are verified and reused. A damaged existing object is preserved and the operation fails.
All paths are derived from checked identifiers; links and junctions are refused.

Syncthing can deliver these files in any order. Inbox entries include `lfs_status` with object
count, bytes, missing count and `complete`. This availability check tests presence and size;
import performs the full SHA-256 verification. Interfaces disable import while objects are missing.

Import verifies the frozen bundle and all incoming objects. It adds immutable Git objects with
[`git bundle unbundle`](https://git-scm.com/docs/git-bundle), without publishing refs, then derives
the LFS inventory again from the imported commit. Omitted, extra, duplicated or inconsistent
manifest objects are refused. Verified LFS objects are installed in the local Git cache before
publishing `refs/remotes/cysynck/PEER/BRANCH`. Git hooks and recursive submodule fetching are disabled.
The current HEAD, index and working files stay untouched. Review and merge separately with Git.

An interrupted or rejected import can leave reusable immutable Git/LFS objects, but it does not
publish the tracking ref before verification completes. Export interruption can leave unreferenced
exchange objects or an unpublished bundle. There is no automatic LFS cleanup, expiry, remote
locking or acknowledgement. Keep exchange folders restricted to trusted peers.

## Compatibility and limits

Both devices need CySynck 0.13 or later for LFS exchange. Older clients ignore the `.lfs.json`
suffix instead of importing a bundle without its assets. Existing version-1 manifests remain
readable. To produce a legacy pointer-only bundle explicitly:

```sh
cysynck git export game-team --lfs false
```

That mode transports Git objects and pointer files; it does not transport their LFS payloads.
The native/Web export checkbox also controls this per-operation choice. Automatic Git-mode
synchronization always uses the new default. Use a separate compatible peer workflow for old clients.

- Up to 100,000 reachable Git objects inspected per export/import.
- Up to 4,096 unique LFS objects, 4 GiB per object and 16 GiB total per bundle.
- A 1 MiB manifest limit, bounded batches and streamed filesystem copies; assets are not read
  into a single memory buffer. Limits do not cap all retained exports or the entire Git cache.
- SHA-1 and SHA-256 Git object identifiers are accepted; LFS content always uses SHA-256.
- LFS objects inside submodules are not exported from their parent repository.
- Signing, LFS locks, LFS server upload/download and CyRevision protocol interoperability remain pending.

## API and SDK

The existing authenticated project routes work through the C/C++ SDK and remote CLI:

| POST route | Body/result |
| --- | --- |
| `/api/projects/ID/export` | `{"lfs":true}` (default), or `{"lfs":false}` for legacy |
| `/api/projects/ID/inbox` | `{}`; array of manifests, with `lfs_status` on version 2 |
| `/api/projects/ID/import` | `{"peer":"PEER","bundle":"BUNDLE"}`; verified LFS counters on success |

The version-2 `lfs` field contains `version:1`, `scope:"branch-history"`, sorted unique
`objects:[{"sha256":"...","size":N}]` and their total `bytes`. It contains no local paths,
credentials or uncommitted filenames. Version-1 manifests cannot carry an LFS inventory.
