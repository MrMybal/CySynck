# Extract an imported Git revision

CySynck extracts the complete file tree of an imported peer branch, or a selected commit
reachable from that branch, into a separate new directory. The result is a file snapshot;
Git metadata and history stay in the source repository. Clean fast-forward branch application
has a separate [reviewed integration flow](git-integration.md); divergent merges and rebases
remain manual Git operations. Extraction works before the first local commit and
preserves the source HEAD, staging index and working files, including dirty or staged work.

## Preview, then extract

```sh
cysynck git extract PROJECT PEER BRANCH /absolute/new-output
# Keep the same options and copy the returned plan:
cysynck git extract PROJECT PEER BRANCH /absolute/new-output --apply true --plan TOKEN
# Select a historical commit or explicitly preserve raw LFS pointer bytes:
cysynck git extract PROJECT PEER BRANCH /absolute/another-output --commit FULL_COMMIT_ID --lfs false
```

The default revision is the imported branch head. Commit selection accepts a full SHA-1 or
SHA-256 ID reachable from that imported branch. Abbreviations and revision expressions are
refused. The preview reports the selected `commit`, `files`, `file_count`, expanded `bytes`,
`lfs_files`, `lfs_bytes`, `errors`, `can_extract` and `plan`. Blocked inventories have no plan.
Preview leaves the destination absent. Output cannot overlap any configured project, exchange
directory or the calling profile; an existing destination is preserved even when empty.

The token covers the project, current local/imported refs, selected commit, exact file inventory,
destination and LFS policy/storage. Changing any of those invalidates it. Optional `base`,
`head` and `local_ref` fields pin the original review context; the CLI accepts `--base unborn`
for a null local commit. Both interfaces supply these pins automatically.

In CyUI or Web, review an imported branch, optionally select a history entry, then use
**Extract Git revision**. Choose a new directory, select the LFS policy, preview, inspect any
errors and select **Extract to new directory**. The displayed commit identifies the complete
snapshot being copied, independently of the parent chosen for its diff.

## Bytes, LFS and limits

Regular files retain committed bytes, including binary data, UTF-8 paths and empty files.
The Git executable bit maps to `0755` or `0644` on POSIX. EOL conversion, attributes-based
encoding, checkout filters, hooks, archive attributes and submodule commands are not executed.
The implementation reads [Git trees](https://git-scm.com/docs/git-ls-tree) and
[blob objects](https://git-scm.com/docs/git-cat-file) directly and verifies blob identities.

By default, canonical LFS v1 pointers expand from the verified local cache, including an
explicitly approved matching custom `lfs.storage`. Assets are streamed and the resulting size
and SHA-256 are checked. No asset is fetched from a remote. Missing, linked, inconsistent or
corrupt LFS objects block hydrated extraction. `--lfs false` copies the committed pointer bytes
and requires no LFS cache. The extraction policy is explicit and independent of the automatic
bundle-export policy saved on the project.

- At most 10,000 files and 4,096 distinct LFS assets.
- Ordinary Git blobs: at most 1 GiB each from 0.18 (4 MiB in 0.17), streamed through bounded buffers.
- LFS assets: at most 4 GiB each; complete expanded output: at most 16 GiB.
- Linked files and submodules block extraction and require separate handling.
- Relative paths must be valid UTF-8 and safe on the target platform. Repository metadata
  paths, path escapes, Windows device/stream aliases and platform filename collisions are refused.
  ASCII case-only file or directory variations are refused on every platform for portable output;
  Windows also uses its ordinal Unicode case comparison. Exclusive file creation protects against
  other filesystem aliases encountered during application.
- Complete non-shallow history and no partial-clone/promisor configuration are required.
  Repository-specific `GIT_*` environment redirections must be unset.

The preview verifies LFS availability/integrity. Ordinary blob stdout is consumed in chunks
of at most 16 KiB, with the reviewed size enforced before writing and SHA-256 calculated as
bytes arrive. After closing the file, Git's [`hash-object --no-filters`](https://git-scm.com/docs/git-hash-object)
checks the exact blob identity without writing objects or invoking checkout filters. The
complete output is also read back for SHA-256 verification. A missing or corrupted object can
therefore interrupt application even after a successful metadata preview. Each Git helper has
a 120-second timeout; an error preserves the partial output and incomplete receipt.

## Receipts and interrupted copies

Application claims the new directory and each file exclusively. A private JSON receipt is
created under `STATE/git-extractions/PROJECT/ID.json` before copying. Success records
`status:"complete"`, the pinned revision, file inventory, per-file SHA-256 and `files_written`.
The response includes `receipt` and its absolute `receipt_path`.

An exception preserves partial files and records `status:"incomplete"` and the error. The
diagnostic names the output directory and receipt ID. Process interruption also leaves the
initial incomplete receipt. Completed-file hashes and counters are finalized when the receipt
is saved; they can lag behind output after an abrupt termination. No recursive cleanup, rollback
or automatic resume occurs. Review the partial copy and choose another new destination to retry.
External writers must remain idle during copying; source refs are checked again on completion.

## API and SDK

Authenticated `POST /api/projects/ID/git-extract` accepts:

```json
{"peer":"PEER","branch":"main","destination":"/absolute/new-output","lfs":true}
```

Optional `commit` selects a reachable revision. Application adds `apply:true` and the preview's
`plan` while retaining all original inputs. The established C++ and UTF-8 JSON C ABI expose
the same route without changing the ABI version. The package smoke test exercises preview,
LFS extraction, a regular Git blob larger than 8 MiB and receipt creation through an independent C consumer.
