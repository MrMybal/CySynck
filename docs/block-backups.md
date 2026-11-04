# Block backup storage (0.11)

Backups now reuse unchanged portions of large files across snapshots and projects in the same
state profile. The default `auto` policy stores files from **8 MiB through 256 GiB** as ordered
**4 MiB blocks**, with a shorter final block when needed. Smaller files and files above that
range keep the existing whole-file object layout. Empty files also remain whole-file objects.

This is local backup storage. Syncthing still handles continuous synchronization; direct
transfer chunks use their own [resumable protocol](transfers.md).

## Choose a policy

```sh
cysynck project add Assets /srv/assets --mode backup --storage auto
cysynck project set PROJECT_ID --storage whole
cysynck project set PROJECT_ID --storage auto
cysynck backup PROJECT_ID
cysynck verify PROJECT_ID
```

Existing profiles use `auto` when no policy is stored. The desktop Settings view and Web console
offer **Reuse blocks for large files**; disabling it selects `whole`. Changing the policy affects
future snapshots. Existing snapshots keep their original objects and block recipes.

For example, a file containing two 4 MiB blocks and a 97-byte tail initially writes 8 MiB plus
97 bytes. Changing one byte in the second block writes 4 MiB on the next capture and reuses
the first block and tail. The old version remains restorable. Identical blocks are also reused
within one file and between separate projects.

Boundaries are fixed offsets. Inserting bytes near the start can shift subsequent blocks and
reduce reuse. Every capture still reads and hashes the source; this milestone reduces retained
data rather than promising faster scans. There is no compression or content-defined chunking.

## Format and verification

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

The same [restoration exclusions and partial-output behavior](backups.md) still apply. Snapshot
diffs keep the 512 KiB text preview limit. For a larger file, `expected_sha256` identifies the
manifest digest; `sha256` stays null in that bounded preview. Use `verify` for content integrity.

## Statistics and cleanup

Snapshot results include `block_storage`: `chunked_files`, `block_size`, `new_blocks`,
`reused_blocks`, `new_block_bytes` and `reused_block_bytes`. The byte counters describe the
successful capture's block writes and reuse, including repairs; they are not an increase in
filesystem usage. The desktop and Web snapshot views display these counters.

`storage status` includes both namespaces in the existing object totals and adds:

| Field | Meaning |
| --- | --- |
| `block_objects`, `block_stored_bytes` | Recognized physical block files and their byte total |
| `chunked_files` | File versions stored as block recipes across retained snapshots |
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
