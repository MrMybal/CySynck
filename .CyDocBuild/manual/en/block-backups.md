---
id: block-backups
title: Block backup storage (0.22)
description: Usage, commands and limits for Block backup storage (0.22).
section: Backups
order: 12
parent: backups
revision: 1
---

# Block backup storage (0.22) {#block-backup-storage-022}

Backups now reuse unchanged portions of large files across snapshots and projects in the same
state profile. The default `auto` policy stores files from **8 MiB through 256 GiB** as ordered
**4 MiB blocks**, with a shorter final block when needed. Smaller files and files above that
range keep the existing whole-file object layout. Empty files also remain whole-file objects.

This is local backup storage. Syncthing still handles continuous synchronization; direct
transfer chunks use their own [resumable protocol](doc:transfers).

## Choose a policy {#choose-a-policy}

```sh
cysynck project add Assets /srv/assets --mode backup --storage auto
cysynck project set PROJECT_ID --storage whole
cysynck project set PROJECT_ID --storage auto
cysynck project set PROJECT_ID --storage cdc
cysynck backup PROJECT_ID
cysynck verify PROJECT_ID
```

Existing profiles use `auto` when no policy is stored. The desktop Settings view, project creation
and Web console offer **Backup storage** with fixed blocks, whole files or content-defined blocks.
The JSON project setting accepts `backup_storage: "auto"`, `"whole"` or `"cdc"`.
Changing the policy affects
future snapshots. Existing snapshots keep their original objects and block recipes.

For example, a file containing two 4 MiB blocks and a 97-byte tail initially writes 8 MiB plus
97 bytes. Changing one byte in the second block writes 4 MiB on the next capture and reuses
the first block and tail. The old version remains restorable. Identical blocks are also reused
within one file and between separate projects.

In `auto` mode, boundaries are fixed offsets. Inserting bytes near the start can shift subsequent
blocks and reduce reuse. Every policy still reads and hashes the source. Content-defined storage
can reduce retained data for shifted content; it does not promise faster scans or compression.

## Content-defined storage (0.22) {#content-defined-storage-022}

Choose `cdc` explicitly for files from **8 MiB through 64 GiB**. The native C++ core cuts
content-defined blocks with a minimum of 1 MiB and maximum of 8 MiB, plus a shorter final tail.
Smaller, empty or larger files use whole-file storage. Each recipe contains at most 65,536 blocks.
The lower file-size ceiling ensures this count bound even at the minimum block length.

The deterministic `gear64-v1` splitter uses an unsigned 64-bit fingerprint. Start with `h = 0`;
for each byte `b`, compute `h = (h << 1) + G[b]` with arithmetic modulo 2^64. After at least
1 MiB, cut when the low 22 bits are zero, or force a cut at 8 MiB. Reset `h` after each cut.
The 4 MiB mask scale is not a promise of an exact average chunk length. End-of-file publishes
any nonempty tail; empty files have no chunk recipe.

`G` has 256 unsigned entries, generated once from seed `0x435953594e434b31`. For each entry,
add `0x9e3779b97f4a7c15` to the seed, then mix that value with successive xor-right-shifts
by 30, 27 and 31; multiply after the first two shifts by `0xbf58476d1ce4e5b9` and
`0x94d049bb133111eb`, respectively. All intermediate values wrap modulo 2^64. This specification
defines CySynck's own versioned splitter and does not introduce another tool or runtime.

An insertion or deletion can allow subsequent boundaries to realign with old content. Reuse
depends on the actual bytes and edit position; repetitive data can force maximum-length blocks
and gain little from this policy. The splitter selects boundaries only. SHA-256 identifies and
verifies the actual block bytes and the complete reconstructed file.

Entries use `storage: "blocks-cdc-v1"` with the same ordered `{sha256, size}` block list below.
Sizes must cover the file exactly; nonfinal blocks must be 1–8 MiB and the final block 1 byte
through 8 MiB. Readers validate these size bounds and full-file coverage, then verify all bytes;
restoration does not require recalculating the splitter's boundaries. The buffer is limited to
one 8 MiB chunk plus the existing streaming/hash buffers, independent of total file size.

CDC and fixed blocks share `STATE/blocks/SHA256`, so identical block bytes may be reused across
policies and projects. Snapshots retain their own recipes when settings change. Readers before
0.22 reject CDC recipes; use 0.22 or later to restore or clean up a profile containing them.

## Format and verification {#format-and-verification}

Version 2 manifests keep the full SHA-256, size and simple permissions of each file. Large-file
entries add an ordered recipe:

```json
{
  "sha256": "FULL_FILE_SHA256",
  "size": 8388705,
  "permissions": 420,
  "storage": "blocks-v1",
  "blocks": [
    {"sha256": "FIRST_BLOCK_SHA256", "size": 4194304},
    {"sha256": "SECOND_BLOCK_SHA256", "size": 4194304},
    {"sha256": "TAIL_SHA256", "size": 97}
  ]
}
```

Whole-file objects remain in `STATE/objects/SHA256`; block objects live separately in
`STATE/blocks/SHA256`. All actual digests are 64 lowercase hexadecimal characters. Recipes
are bounded to 65,536 blocks per file and manifests to 64 MiB. Unsupported formats, layouts,
invalid sizes, unsafe paths and inconsistent summaries are rejected before retention or cleanup.

Capture atomically stores each block and publishes its manifest only after the source size and
modification time remain stable. A changing file is retried up to three times. Interrupted or
failed captures can leave unreferenced blocks for explicit cleanup. A new capture of authentic
bytes repairs an existing damaged block atomically, including older snapshots sharing it.

Verification checks block sizes, every block digest and the complete concatenated file digest.
The final check catches reordered blocks even when every individual block is authentic. Repeated
file recipes share verification results; payload buffering stays bounded rather than loading the
entire large file into memory. Verification is integrity checking, not an authenticated signature.

Restoration verifies the snapshot before creating its destination, then streams and checks each
file again. Destination files are created exclusively, and the resulting files are checked before
their stored permissions are applied. Files from manifests without a format marker remain fully
supported. New block-backed snapshots require CySynck 0.11 or later to restore.

The same [restoration exclusions and partial-output behavior](doc:backups) still apply. Snapshot
diffs keep the 512 KiB text preview limit. For a larger file, `expected_sha256` identifies the
manifest digest; `sha256` stays null in that bounded preview. Use `verify` for content integrity.

## Statistics and cleanup {#statistics-and-cleanup}

Snapshot results include `block_storage`: `chunked_files`, `block_size`, `new_blocks`,
`reused_blocks`, `new_block_bytes` and `reused_block_bytes`. The byte counters describe the
successful capture's block writes and reuse, including repairs; they are not an increase in
filesystem usage. The desktop and Web snapshot views display these counters.

`block_storage.content_defined_files` counts CDC file versions in the snapshot. Its `block_size`
remains the 4 MiB mask scale for CDC, and the exact length for fixed-block captures. The actual
size of each CDC block is recorded in its recipe.

`storage status` includes both namespaces in the existing object totals and adds:

| Field | Meaning |
| --- | --- |
| `block_objects`, `block_stored_bytes` | Recognized physical block files and their byte total |
| `chunked_files` | File versions stored as block recipes across retained snapshots |
| `content_defined_files` | File versions stored as CDC recipes across retained snapshots |
| `referenced_bytes` | Physical bytes of recognized objects still referenced by snapshots |
| `deduplicated_bytes` | Logical retained file bytes minus referenced physical bytes |
| `deduplication_complete` | Whether missing objects or catalog errors prevent that calculation |

These statistics exclude manifests, filesystem overhead, received files and independent recovery
copies. Inventory does not hash object contents; use `verify` for integrity. Deduplicated bytes
are reported as zero with `deduplication_complete: false` when the catalog is incomplete.

Cleanup remains explicit, with a fresh preview token. It follows references from every retained
manifest, including unlisted projects and mixed old/new layouts. It preserves unknown files,
temporary files and every referenced block, rejects linked namespaces, and never recurses into
unexpected directories. Capture, restore and cleanup share the profile's process/disk locks.
