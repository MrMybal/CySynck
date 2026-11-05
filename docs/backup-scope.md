# Backup scope and exclusion rules (0.31)

The native **Backup scope** view and Web section explain which project files a backup would
include. Edit the exclusion draft, preview its impact, then explicitly save the reviewed rules.
Previewing a draft does not change project settings, invoke plugin callbacks, read file payloads,
create backup objects or publish a snapshot. After saving rules, preview again to enable
**Back up reviewed scope** against the saved settings. Interfaces start in English and offer French.

## Rules and pruning

Rules retain CySynck's existing semantics. `*` matches zero or more bytes, including `/`, and
`?` matches one byte. A rule matches either the complete relative path or any path component.
Rules are case sensitive except for the existing Windows handling of the built-in `.git`,
`.stfolder`, `.stversions` and `.stignore` component names. Unicode filenames are supported;
`?` counts UTF-8 bytes rather than Unicode characters. There is no Git-ignore negation,
comment syntax, escaping or special trailing-slash syntax. A literal `!` or `#` stays literal.

The first matching rule is reported with its zero-based `rule_index`. Excluded directories
appear once and are not traversed: their children, sizes and counts are not inventoried.
Links/junctions and special files are skipped. `.cysynck-recovery` components are always
protected, even with an empty rule list. Other default rules can be removed explicitly.
These project rules also affect local change lists and existing file-selection workflows;
they do not edit Syncthing's `.stignore` or Git configuration.

Rules are UTF-8 strings, with no NUL/newlines, 1–1,024 bytes each, at most 256 rules and 16 KiB
of encoded JSON. Empty UI lines are omitted. Spaces are significant. Bulk project POST/PUT
shares this validation; those existing settings routes do not require an editor preview token.
Profiles with older excessive rules must reduce them before using the new preview/editor.

## CLI and API

```sh
cysynck backup-preview PROJECT
cysynck backup-preview PROJECT --excludes rules.json --limit 100
cysynck backup-excludes PROJECT rules.json --plan TOKEN
cysynck backup-preview PROJECT
cysynck backup PROJECT --plan NEW_TOKEN
```

`rules.json` is an array such as `[".git", "*.tmp", "build"]`. Omit `--excludes` to preview the
stored rules. POST `/api/projects/ID/backup-preview` accepts optional `excludes`, `offset`,
`limit` and `inventory_hash`. Limits are 1–200 entries per page (default 100), offsets 0–100,000,
100,000 visited filesystem entries and 32 MiB of inventory metadata. Unreadable entries,
unstable file metadata, unsafe paths or exceeded bounds fail the preview; partial inventories
are not presented as complete.

Results contain `excludes`, `stored_excludes`, `rules_changed`, included `files` and `bytes`,
`skipped`, `visited`, `content_verified:false`, `inventory_hash`, `plan`, `total`, `offset`,
`limit`, `has_more` and `entries`. Entries contain `path`, `kind`, `included` and `bytes`.
Included files add permissions, opaque clock ticks as a string and predicted `storage`
(`whole`, `blocks-v1` or `blocks-cdc-v1`). Skipped rows add `reason` (`rule`, `protected`,
`link` or `special`) and applicable rule metadata; their bytes remain null.

Pages sort by UTF-8 relative path. Supply the first page's `inventory_hash` on continuation
pages to reject changed inventories. Every page rescans the bounded tree. The plan binds the
profile, complete saved project settings, candidate rules and observed inventory. It remains
identical across page sizes/offsets for the same state.

PUT `/api/projects/ID/backup-excludes` accepts `{"excludes":[...],"plan":"TOKEN"}`. It recomputes
the proposed preview before saving, and returns `saved:true` and `excludes`. No project write
occurs when validation or freshness checks fail. POST `/api/projects/ID/backup` optionally
accepts `{"plan":"TOKEN"}` from a preview of the currently saved rules. A draft with different
rules cannot authorize that backup. Stale, cross-project and cross-profile plans are refused
before normal backup callbacks/capture. Preview/save/run use the same C++ routes through the
existing C/C++ JSON API; the C ABI version remains 1.

## Scope of freshness

The inventory reads included file sizes, timestamps and permissions, without hashing contents.
It cannot detect edits that preserve all those metadata or guarantee a filesystem snapshot.
Excluded subtree changes do not invalidate its inventory. Ordinary backup capture still reads
and hashes payloads, checks concurrent changes and verifies manifests. A file changed after
the initial plan check, including by a plugin callback, follows those normal capture rules.
Quiesce writers when a consistent application-level backup is needed.

Bytes are logical included file sizes. Storage predictions apply the current whole/fixed/CDC
size thresholds; they do not estimate deduplication savings, allocated disk space or temporary
space. Automatic/scheduled/before-sync backups and `backup PROJECT` without `--plan` keep their
existing behavior. No persisted preview or implicit backup is created by opening the editor.
