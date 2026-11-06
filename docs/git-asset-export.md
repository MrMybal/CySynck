# Exporting preserved LFS assets (0.40)

Pointer restoration preserves unchanged expanded assets under
`.cysynck-lfs/RECEIPT/before/`. Export copies those saved versions into a new separate
directory, including after a later Git update changes the committed LFS identity.
The original assets, source receipt, working files, HEAD, index and LFS cache stay intact.

```sh
cysynck git materialization-export PROJECT RECEIPT --destination /absolute/new/export
cysynck git materialization-export PROJECT RECEIPT --destination /absolute/new/export --apply true --plan TOKEN
```

Use the token returned by the preview and repeat the same destination and selection.
Optional `--paths-file selection.json` selects exact saved filenames or folder prefixes
ending in `/`, using the same bounded JSON array as materialization. The selection applies
to assets recorded for preservation, rather than every LFS file in the old commit.
CyUI and Web expose preview/apply for all preserved assets in a receipt, in English/French.

The authenticated POST `/api/projects/ID/git-materialization-export` accepts
`{"receipt":"ID","destination":"/absolute/new/export","paths":["Assets/"]}`;
omit `paths` for all preserved assets. Add `apply:true` and `plan` to apply.
The existing C/C++ JSON API supports this route without an ABI change.

## Verification and destination

Only schema-2 pointer-restoration receipts are accepted. Recorded pointer identities and
selection are checked against the original immutable Git commit. Selected preserved files
must match their recorded SHA-256, size, timestamp and permissions. Missing or changed
sources block the export; an explicit selection can omit an unavailable asset. Unknown
entries or links in recovery storage require manual inspection.

The preview returns `files`, `bytes`, `excluded`, `selection`, per-path `errors`, `can_export`
and `plan`. Preview creates no export directory. The token binds profile and project settings,
registered projects, receipt bytes, source fingerprints, selection and destination. Later
HEAD changes are allowed because this operation exports the recorded old version.

Choose an absolute, absent destination with an existing ordinary parent directory. The
destination must be separate from every registered project, exchange directory and the
profile. Existing directories/files, links and junctions are refused. A shared CySynck Git
lock serializes application; pre-existing locks are preserved and require inspection.

The destination is reserved exclusively. Assets are streamed and flushed into `files/PATH`,
verified again, and receive the source permissions and timestamp. This requires space for
the selected expanded copies, up to the existing limits of 4 GiB per asset and 16 GiB total.
No Git LFS helper, checkout filter, network access or local cache is used.

`manifest.json` records provenance, hashes, selection, progress and `status`. Successful
exports return `exported:true`, `status:"complete"` and `files_written`. Errors after the
manifest is created retain an `incomplete` manifest, partial copies and the original sources;
inspect them manually and choose a new destination for another attempt. There is no automatic
cleanup, overwrite, resume or replacement of working files.

The original Git commit must still be readable in the repository. Pause external writers
during export: the CySynck lock does not lock other applications. Native Linux/macOS runtime,
power-loss behavior, large real-asset performance and new GUI interactions remain to validate.
