---
id: git-dehydration
title: Restoring committed LFS pointers (0.39)
description: Usage, commands and limits for Restoring committed LFS pointers (0.39).
section: Git
order: 40
parent: git-materialization
revision: 1
---

# Restoring committed LFS pointers (0.39) {#restoring-committed-lfs-pointers-039}

Expanded LFS assets normally block pointer-only Git integration. **Prepare pointers for Git
updates** provides an explicit intermediate step: preserve unchanged expanded assets in local
recovery storage and restore the exact pointer bytes from the current commit. Then preview
[pointer-only integration](doc:git-lfs-integration), apply the branch update, and separately
[materialize](doc:git-materialization) the assets needed from the updated verified cache.
Branch integration and these working-file operations are separate reviewed steps.

```sh
cysynck git dehydrate PROJECT_ID
cysynck git dehydrate PROJECT_ID --paths-file selection.json
cysynck git dehydrate PROJECT_ID --paths-file selection.json --apply true --plan PREVIEW_TOKEN
```

`selection.json` uses the same bounded array of exact relative filenames or folder prefixes
ending in `/` as materialization. Omit it to inspect all committed LFS pointers. Unmatched or
unsafe selectors are refused. Modified, deleted, staged, read-only and specially flagged files
are preserved and reported; already-pointer files are skipped. Other eligible selected assets
can still be processed. Only working bytes matching the committed pointer's exact SHA-256 and
size are eligible. No Git LFS helper, checkout filter, download or cache access is required.

The existing authenticated POST `/api/projects/ID/git-materialize` accepts
`{"operation":"dehydrate","paths":["Assets/"]}` for preview; add `apply:true` and `plan` to apply.
The default `operation:"materialize"` keeps its expansion behavior. Repeat the same operation
and selection during application. Existing C/C++ consumers use the JSON route with ABI v1.

Preview returns `operation`, `can_dehydrate`, `pending`, `pointers` (already-pointer paths),
`skipped`, `excluded`, `bytes` (expanded bytes to preserve), selected file states and an opaque
plan. `can_materialize` is false for dehydration. The plan binds project/profile settings,
HEAD/ref, index bytes and flags, configuration, selection and observed working fingerprints.
An edited source, changed selection, renamed project or different operation requires a new preview.

Application reserves the existing Git integration lock and repeats inspection. It creates
all pointer staging files exclusively under `.cysynck-lfs/OPERATION/after/`, flushes them, and
rechecks Git context. Each unchanged source asset is moved into an absent matching `before/`
path; its exact bytes, timestamp and permissions remain preserved. Pointer publication copies
its permissions and Windows DACL, refuses an existing destination and checks the result.
HEAD, branch, index and Git configuration remain unchanged. Staging needs space only for the
small pointer files; preserved asset moves still require one filesystem supporting publication.

## Receipts and interruptions {#receipts-and-interruptions}

The shared materialization catalog exposes `operation:"dehydrate"`. New dehydration receipts
use schema 2 in `STATE/git-materializations/PROJECT/ID.json`; materialization keeps schema 1.
Use CySynck 0.39 or later to read schema 2; older clients report those receipts as unsupported.
Existing schema-1 receipts remain readable, including those without an `operation` field.

```sh
cysynck git materializations PROJECT_ID
cysynck git materialization-check PROJECT_ID OPERATION_ID
cysynck git materialization-acknowledge PROJECT_ID OPERATION_ID --plan CHECK_TOKEN
```

Checks validate selected metadata against the recorded immutable commit, then hash working
files, preserved expanded assets and known staged pointers. For dehydration, `before` means
expanded assets are still present, and `after` means pointers are present with unchanged
expanded originals in recovery. Unknown entries, corruption and links block verification.
Incomplete receipts can be marked reviewed only at an exact verified before/after state;
their original status and error remain intact. Dehydration does not use publication resume.

There is no automatic restore, rollback, retry, deletion of preserved assets or lock removal.
Inspect an incomplete receipt and retained files before manual recovery. Advancing HEAD later
changes the receipt's original context; it does not remove its preserved assets. Retain these
files until their saved versions are no longer needed. From 0.40, a reviewed
[preserved-asset export](doc:git-asset-export) can copy those versions into a new separate
directory after verification, including after HEAD advances. Replacing working files with
a saved version remains a manual decision after checking the current committed identity.

CyUI and Web expose the same selection, explicit preview/apply controls and operation-specific
receipt results in English and French. Pause external writers during working-file operations;
the CySynck lock does not lock other applications. Native Linux/macOS runtime, power-loss
behavior, larger real-asset performance and interaction with the new GUI controls remain to test.
