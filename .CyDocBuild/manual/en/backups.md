---
id: backups
title: Backups and storage
description: Verify snapshots, restore a separate copy and reclaim unused backup objects.
section: Backups
order: 10
revision: 1
---

# Backups and storage {#sauvegardes-et-stockage}

Browse saved files, compare snapshots and restore selected paths with a manifest/destination-bound preview in the [snapshot workflow guide](doc:snapshot-workflows).
The default `auto` policy stores eligible large files as reusable 4 MiB blocks. `whole` retains whole-file objects and `cdc` uses content-defined blocks.
Existing manifests remain readable. See [block storage](doc:block-backups) for exact size ranges, compatibility and counters.

Each snapshot has a complete manifest and SHA-256 content objects deduplicated across the profile's projects.
After publishing a successful snapshot, `backup_keep` removes the oldest manifests. Unused objects remain until explicit cleanup.

## Verify and restore {#verifier-et-restaurer}

```sh
cysynck verify PROJECT_ID
cysynck verify PROJECT_ID --snapshot SNAPSHOT_ID
cysynck restore-plan PROJECT_ID SNAPSHOT_ID /srv/restored-copy
cysynck restore PROJECT_ID SNAPSHOT_ID /srv/restored-copy
```

Verification reads objects, checks sizes/SHA-256 and the full digest of files reconstructed from blocks.
Identical recipes share results. `ok`, `issues`, `objects_checked` and `checked_at` describe the report.
`ok:false` is a valid report, not a command failure; scripts must inspect it. Invalid manifests, permissions and arguments fail the command.
Verification is not a signature: a writer with access to the state can change both objects and manifests.

A new backup capturing authentic bytes repairs a damaged object atomically. Older snapshots referencing that content become usable again.
If the original content is no longer available, recover it from another copy; verification cannot reconstruct it.

`restore-plan` verifies integrity and lists entries without creating a destination. `restore` rechecks before exclusively creating a new directory.
The destination cannot overlap any project, exchange directory or state profile. Links/junctions, unsafe manifest paths and Windows naming conflicts are rejected.
Copied content is verified too. An integrity error before copying leaves the destination absent.
Disk/permission errors, external changes or interruption during copying can leave partial output: restoration is not transactional.
The source project stays intact. Only content and simple permissions are restored; empty directories, links, ACLs, extended attributes and original timestamps are excluded.

## Reclaim space {#liberer-lespace}

```sh
cysynck storage status
cysynck storage prune
cysynck storage prune --apply true --plan PREVIEW_TOKEN
```

Without `--apply true`, no object is removed. Preview returns:

| Field | Meaning |
| --- | --- |
| `snapshots` | Manifests on disk, including unregistered projects |
| `logical_bytes` | Logical file sizes across retained versions |
| `stored_bytes` | Recognized object-file sizes |
| `missing_objects` | Referenced SHA-256 objects absent from disk |
| `unused_objects`, `reclaimable_bytes` | Unreferenced objects and reclaimable bytes |
| `ignored_entries` | Unrecognized entries, preserved |
| `errors`, `can_prune` | Catalog errors and whether application is possible |
| `plan` | Preview fingerprint required for application |

Inventory does not hash payloads; use `verify` for integrity. It excludes logs, received files, Git, Syncthing versions and independent resolution copies
in `resolutions/` and `.cysynck-recovery/`. Referenced corrupt objects remain protected.
Invalid manifests, unexpected snapshot catalog entries and junctions block cleanup. Unknown/temporary object entries remain intact.
There is no recursive removal. Capture, restoration and pruning share the cross-process operation lock.

The token must still match the profile, manifests and object inventory when applying. Otherwise request a new preview before deletion.
Results include `removed_objects`, `reclaimed_bytes` and `failures`. A deletion error can leave partial cleanup; references remain protected and a new inspection can continue.
Cleanup is manual: it does not run at startup, after backups or in the background.
