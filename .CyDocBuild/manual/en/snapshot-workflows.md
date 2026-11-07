---
id: snapshot-workflows
title: Snapshot browsing, comparison and selective restoration
description: Usage, commands and limits for Snapshot browsing, comparison and selective restoration.
section: Backups
order: 13
parent: backups
revision: 1
---

# Snapshot browsing, comparison and selective restoration {#snapshot-browsing-comparison-and-selective-restoration}

Version 0.29 adds three backup workflows to the C++20 core, CLI, C/C++ SDK and English/French
CyUI and Web views. Open **Backups** in the desktop app or a project's backup section in Web.

## Browse saved files {#browse-saved-files}

Choose a snapshot and **Browse saved files**. An optional folder prefix selects that directory's
descendants without including similarly named siblings. Pages contain at most 200 regular files
(100 by default), in exact path order. Sizes, recorded SHA-256, permissions and storage formats
come from the validated manifest. Browsing neither hashes payloads nor scans the current project.
Missing or corrupt payloads therefore do not prevent inspecting the metadata.

```sh
cysynck snapshot-files PROJECT SNAPSHOT --prefix Content --limit 100 --offset 0
cysynck snapshot-files PROJECT SNAPSHOT --prefix Content --offset 100 --manifest-hash HASH
```

Responses include `files`, `total`, `bytes`, `offset`, `limit`, `has_more`, normalized `prefix`
and `manifest_hash`. Totals cover the whole prefix. `content_verified:false` distinguishes
recorded metadata from an integrity check. Pin `manifest_hash` on subsequent pages to reject
modified manifests. Retention can remove an old version; reload the available snapshots.
Explicit queries read only the named manifest. Manifests retain their 64 MiB validation limit;
pages are limited to 2 MiB, offsets to 1,000,000 and folder prefixes to 4,096 UTF-8 bytes.

## Restore selected files or folders {#restore-selected-files-or-folders}

Click saved paths to add them to **Restore selection**, or enter one relative path per line.
A folder selector must end in `/`: `Content/` includes descendants, while `Content` means an
exact file. Paths are case-sensitive and use forward slashes. Wildcards have no special meaning.
Absolute paths, backslashes and dot components are refused; absent selectors are errors.
Overlapping selectors are deduplicated. Limits are 256 selectors, 4,096 UTF-8 bytes per selector
and 16 KiB of combined text. Newline-containing names cannot be selected individually; whole
restoration remains available.

Leave the interface selection empty for the whole snapshot. API callers omit `paths` for this;
an explicit empty array is rejected. The CLI reads a UTF-8 JSON array file, for example
`["Content/","Source/Game.cpp"]`:

```sh
cysynck restore-plan PROJECT SNAPSHOT /new/copy --paths-file selection.json
cysynck restore PROJECT SNAPSHOT /new/copy --paths-file selection.json --plan TOKEN
```

Preview verifies only selected payloads, including whole-file, fixed-block and CDC storage.
A corrupt unselected payload does not block recovery of selected healthy data. The complete
manifest is still validated for unsafe paths and invalid recipes. Optional `manifest_hash` pins
the browsed version. The returned `plan` binds the profile, project/root, full manifest,
normalized selection and destination. Application rechecks integrity and requires that token
for partial restoration. Changes require a fresh preview. Full restoration remains compatible
with older callers omitting a token; supplying one makes those requests checked as well.

Output uses a separate **new** directory, preserves relative paths and cannot overlap registered
projects, exchange directories or the profile. Links and existing destinations are refused.
Files use exclusive creation and complete output hashes are verified. Existing permission
restoration rules apply. Disk errors, concurrent external writes or a crash can leave partial
output; there is no automatic rollback or resume. The source is preserved. Tokens are freshness
checks, not signatures or filesystem locks.

## Compare two saved versions {#compare-two-saved-versions}

Choose the starting and ending snapshots, then **Compare snapshots**. The paginated metadata
list identifies added, deleted, modified and permission-only changes from recorded hashes,
sizes and permissions. A storage-policy change alone is not a logical file change. Current
working files, Git and the synchronization engine do not participate.

```sh
cysynck snapshot-compare PROJECT OLD_SNAPSHOT NEW_SNAPSHOT --limit 100
cysynck snapshot-compare-file PROJECT OLD_SNAPSHOT NEW_SNAPSHOT Source/Game.cpp
```

Responses provide `left_hash` and `right_hash`; pin both on continuation pages and file requests.
Interfaces do this automatically and discard stale responses. Small file previews stream and
verify both versions, supporting added/deleted and empty files, the CyUI comparison viewer and
Git-compatible patches. Text is limited to 512 KiB and 10,000 lines per side. Binary/non-UTF-8
data has no text patch. Larger files expose metadata and check storage layout without reading
all contents; `identical:null` does not claim verified equality. Use verification or restoration
for full integrity checks. Permission changes are reported separately from text patches.

## API and SDK {#api-and-sdk}

All routes use authenticated POST and C ABI version 1. Prefix them with `/api/projects/PROJECT/`:

| Route | Body |
| --- | --- |
| `snapshot-files` | `{"snapshot":"ID","prefix":"Content/","offset":0,"limit":100}` |
| `restore-plan` | `{"snapshot":"ID","paths":["Content/"],"destination":"/new/copy"}` |
| `restore` | Same body plus `"plan":"TOKEN"` |
| `snapshot-compare` | `{"left":"OLD","right":"NEW","offset":0,"limit":100}` |
| `snapshot-compare-file` | `{"left":"OLD","right":"NEW","path":"Source/Game.cpp"}` |

Optional `manifest_hash` or `left_hash`/`right_hash` pin manifests. The CLI accepts corresponding
`--manifest-hash`, `--left-hash` and `--right-hash` flags. Comparison lists also support `--prefix`.
