---
id: diffs
title: Review file differences
description: Compare snapshots, project files and Syncthing conflict copies without writing them.
section: Review
order: 20
revision: 1
---

# Review file differences {#revue-des-differences}

The C++ core supplies comparisons to CyUI, the CLI and API. The Web console displays and copies the text patch.
Comparison reads project files without merging or deleting versions. [Explicit resolution](doc:resolutions) is a separate previewed operation.

## Choose a comparison {#choisir-une-comparaison}

```sh
cysynck diff PROJECT_ID notes.txt --snapshot SNAPSHOT_ID
cysynck compare PROJECT_ID notes.txt alternative.txt
cysynck conflicts PROJECT_ID
cysynck conflict-diff PROJECT_ID notes.sync-conflict-20261104-120000-ABCDEFG.txt
```

`diff` compares a snapshot with the working file, defaulting to the latest snapshot. It verifies the backup object's hash when reading it for preview.
A missing/corrupt object raises an error; `verify` checks the full snapshot, including files beyond preview limits.
`compare` requires two existing ordinary files under the project root. The patch changes the left file to the right content and keeps the left filename; it does not describe a rename.
`conflict-diff` compares the current original with its conflict copy, targeting the original filename without applying the choice.
An absent original is shown as `/dev/null` in a creation patch.

## Syncthing copies {#copies-syncthing}

Recognition follows [Syncthing's documented conflict names](https://docs.syncthing.net/users/syncing.html#conflicting-changes): date, time and short device ID.
Extensionless, compound and hidden filenames work. The filename does not record a timezone and does not prove origin or author identity.
This view lifts only the default `*.sync-conflict-*` exclusion. Other project exclusions, including rules targeting the original filename, still apply.
`.git`, `.stfolder`, `.stversions` and `.stignore` remain excluded. Walks skip links/junctions, direct comparisons reject them and invalid names appear in `errors`.
At most 10,000 valid copies are returned; `count` and `truncated` disclose the limit.

Copies remain ordinary files on disk for Syncthing. This catalog does not list case conflicts from Syncthing's index and is not a three-way or Git conflict merger.
Git mode has [its own explicit review and integration workflows](doc:git-merge-review).

## Patches and bounds {#patches-et-limites}

Text patches use Git headers and unified hunks with three context lines, `/dev/null` for additions/deletions and missing-final-newline markers.
Git-style path quoting preserves spaces, Unicode and special characters. Empty-file creation/deletion is represented; statistics count added and removed lines.
Patch application can depend on the receiving repository's attributes and EOL settings. Patches transport content, not ACLs or extended attributes.

Text previews read at most 512 KiB and 10,000 lines per side. NUL-containing or non-UTF-8 data has no text patch.
A fully read file can expose SHA-256; oversized content leaves `identical:null` and no invented hash.
The algorithm removes a common prefix/suffix first. Above two million comparison cells, it replaces the entire remaining block,
producing a valid patch marked `approximate:true`, without promising minimal edits. CyUI may group lines differently from the copied patch.
Size/mtime changes during reading cause refusal and require another comparison. Concurrent writers are not frozen into a transactional snapshot.
