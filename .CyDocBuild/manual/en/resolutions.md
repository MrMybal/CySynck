---
id: resolutions
title: Resolve conflicts and recover originals
description: Apply an explicit file choice with verified private recovery copies.
section: Review
order: 21
parent: diffs
revision: 1
---

# Resolve conflicts and recover originals {#resoudre-un-conflit-et-recuperer-les-sources}

File synchronization and backup modes offer current content, the Syncthing conflict copy or manually merged text.
This operation does not apply to Git exchange or plugin modes. The merge is manual and is not a three-way merge.

## Prepare and apply {#preparer-et-appliquer}

```sh
cysynck resolution-plan PROJECT_ID CONFLICT_PATH --choice conflict
cysynck pause PROJECT_ID
cysynck resolve-conflict PROJECT_ID CONFLICT_PATH --choice conflict --plan PREVIEW_TOKEN
cysynck resolutions PROJECT_ID
```

`current` preserves current content or its deletion when absent. `conflict` takes the copy and can recreate an absent original.
`merged` requires CLI `--content TEXT` or JSON `content`. Quote paths containing spaces.
For long text use the desktop editor/API rather than OS-limited command-line arguments.
Preview fully hashes both files up to 100 MiB each. Merged text is UTF-8 without NUL, at most 512 KiB.
CLI/API preserve supplied bytes; desktop/Web editors use LF. The result inherits simple permissions from the original if present, otherwise the copy.
The project must be paused for application. Its Syncthing folder must also be paused at the same root; an unreachable engine blocks application.
Pausing alone keeps a plan valid, but changed content, size, date or permissions invalidates it.
CyUI/Web supply the displayed comparison's fingerprints too, so an edit before preparation is refused.
`modified_ns_exact` carries decimal timestamp text to avoid JavaScript 64-bit rounding; the historical numeric field remains.
Above preview limits only observable state/date can be matched to the comparison; the plan then computes the complete hash.
Before applying, space is estimated conservatively per volume for private copies and temporary output, including separate volumes.
This is an estimate, not a disk reservation.

## What is preserved {#ce-qui-est-conserve}

Each resolution gets a random ID and journal:

```text
STATE/resolutions/PROJECT_ID/RESOLUTION_ID/
  resolution.json
  current.bin       (when the original existed)
  conflict.bin
  merged.bin        (when manually merged)
```

Private copies are reread/verified before moving sources and are independent of deduplicated storage/snapshot retention.
The actual sources are then renamed on the same filesystem:

```text
PROJECT/.cysynck-recovery/RESOLUTION_ID/current
PROJECT/.cysynck-recovery/RESOLUTION_ID/conflict
```

This retains real files, including a still-open POSIX inode. The recovery directory is protected when created; moved files keep original permissions, while state copies remain private.
An ownership marker reserves it to the profile/project; an unknown existing directory is not reused.
Working sources and recovery must share a filesystem; an internal separate mount can block the operation.
Output is prepared before moving sources, then published without replacing an existing destination.
Windows uses [MoveFileExW without replacement](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw).
Linux/macOS require exclusive hard-link creation and probe support before moving sources; unsupported filesystems refuse. Native target validation remains required.
`.cysynck-recovery` is always excluded from backup/conflict review. Its Syncthing exclusion is installed first, at the start of [ignore rules](https://docs.syncthing.net/rest/db-ignores-get.html).
Do not commit/share it with another tool without review. No resolution copy is deleted automatically.

## Interruption and export {#interruption-et-export}

On failure, CySynck tries to put each source back when its path is free and preserves any newly appeared file.
If that is impossible or a crash interrupts moving, the project needs review and another resolution is blocked.

```sh
cysynck resolutions PROJECT_ID
cysynck resolution-export PROJECT_ID RESOLUTION_ID /srv/recovered-copies
```

Export verifies all private copies before creating a new directory outside projects/state, containing `current/`, `conflict/`, optional `merged/`, relative paths and journal.
An error after creation may leave partial output; sources/private copies remain. Export restores contents, not original metadata.
Also inspect real `.cysynck-recovery/` files: a POSIX editor holding an inode may have written after its move.
Private copies describe the versions verified during preparation, not those later writes.
Restore wanted files or inspect working state, then mark recovery reviewed:

```sh
cysynck resolution-acknowledge PROJECT_ID RESOLUTION_ID
```

This requires a paused project and removes the diagnostic while preserving copies; it does not restore or validate files for the user.
Resume synchronization explicitly. Receive-only changes stay local under that Syncthing mode; CySynck does not change it to two-way.
Publication is not a multi-file transaction or absolute protection against hardware failure or a writer to private directories.
Copies do not replace an independent backup of the project/state disk.
Simple permissions and Windows DACL are copied. Owner/group, POSIX ACL, extended attributes and timestamps are not guaranteed for new output.

## API and C++ SDK {#api-et-sdk-c}

Use `Core::request` or the C ABI:

| POST action under `/api/projects/ID/` | JSON body |
| --- | --- |
| `resolution-plan` | `{"path":"...","choice":"current|conflict|merged","content":"... for merged"}` |
| `resolve-conflict` | Same body plus preview `plan` |
| `resolutions` | `{}` |
| `resolution-export` | `{"resolution":"id","destination":"/new/directory"}` |
| `resolution-acknowledge` | `{"resolution":"id"}` |

Optional `expected_current`/`expected_conflict` copy `before_info`/`after_info` from `conflict-diff` to bind preparation to the displayed review.
The plan checks consistency, not additional authentication. Normal Bearer-token/origin requirements still apply.
