# Offline Git LFS cache inspection and refill (0.32)

The native **LFS cache** view and the Web Git panel inspect the LFS objects referenced by a
committed branch's reachable history. They can refill missing immutable cache objects from an
explicit peer's existing Syncthing exchange directory. These operations do not contact an LFS
server, run Git filters, change Git configuration, check out files or move Git refs/index.
The CLI and existing C/C++ JSON request API expose the same operations; C ABI version stays 1.

## Inspect and verify

```sh
cysynck git lfs-cache PROJECT
cysynck git lfs-cache PROJECT --verify true
cysynck git lfs-cache PROJECT --peer IMPORTED_PEER --branch main --offset 100 --limit 100
```

POST `/api/projects/ID/git-lfs-cache` accepts optional `peer` and `branch` together to select an
existing `refs/remotes/cysynck/PEER/BRANCH`. Without them it uses local HEAD, including detached
commits. An unborn branch requires an imported branch selection. The full reachable history is
included, so deleted/older assets remain represented; repeated content appears once by SHA-256.
This is an object inventory rather than a current-filename browser.

Default inspection uses presence, size, modification ticks and permissions. `verify:true`
streams and hashes every present correctly sized object. States are `missing`, `present`
(unverified), `verified`, `corrupt`, `size_mismatch` and `unsafe`. Missing objects and damaged
paths are reported separately. `verification_requested` records the requested policy;
`content_verified` is true only when verification was requested and no object is missing/damaged.

Results include the pinned revision/context, approved cache path, counts/bytes, source peer
names, `inventory_hash`, and a page of `objects`. `offset` is 0–4,096 and `limit` is 1–200
(default 100). Pass the first page's `inventory_hash` on continuation pages. Keep the same
verification/revision options: each page reconstructs the complete bounded inventory, including
full hashes when requested. Local metadata/revision/configuration changes invalidate it.

The existing limits apply: 100,000 reachable Git objects, 4,096 distinct LFS assets, 4 GiB per
asset and 16 GiB total. Inspecting history or hashing large caches can take time. SHA-1/SHA-256
Git repositories, canonical LFS pointers and explicitly approved custom `lfs.storage` directories
use the existing [offline LFS rules](git-lfs.md). There is no implicit download or cache pruning.

## Refill only available missing objects

```sh
cysynck git lfs-recover PROJECT SOURCE_PEER
cysynck git lfs-recover PROJECT SOURCE_PEER --apply true --plan PREVIEW_TOKEN
# Optional exact subset: a JSON array of committed LFS SHA-256 identities.
cysynck git lfs-recover PROJECT SOURCE_PEER --objects objects.json
```

POST `/api/projects/ID/git-lfs-recover` accepts `source_peer`, optional revision `peer`/`branch`,
and optional nonempty `objects` (up to 4,096 unique committed identities). An unknown or duplicate
identity is rejected. Sources are derived only from
`EXCHANGE/peers/SOURCE_PEER/lfs/objects/aa/bb/SHA256`. The cache catalog enumerates at most 1,000
peer-directory entries. A source object need not retain its original export manifest: its full
size/SHA-256 must match an identity derived from the selected local Git history.

Preview reads and verifies candidate sources, without creating cache objects or a receipt.
It lists eligible `objects`, `unavailable` sources, preserved existing objects and eligible bytes.
Only missing local objects with verified sources become eligible. Existing entries are preserved
even if they have wrong sizes, corrupt bytes or unsafe paths. A partial set can be eligible;
the interfaces disclose its count/bytes and the unavailable count before explicit application. Recovery previews are bounded to 8 MiB of JSON.
They do not promise to recover every missing object or replace damaged local files.

Application adds `apply:true` and the preview's `plan`. Repeat exactly the same source, revision
and optional object selection. The token binds profile/project settings, Git context/configuration,
cache observations, source identities/metadata and selection. Fresh previews and an exclusive
cache-directory lock precede writes. Every copied asset is verified again and published through
the existing no-replacement hard-link path; concurrent unexpected objects are never overwritten.
The filesystem must support hard links. Cache writes are immutable additions, rather than a
transaction that can roll back already copied assets after failure.

Metadata observations cannot freeze external writers or detect content edits that preserve all
observed metadata. Source/copy verification enforces the expected SHA-256 independently. Other
applications must cooperate with the cache lock to avoid concurrent modifications. Root, cache
and exchange links/junctions are refused. A local malicious process is not isolated by this API.

## Local receipts

```sh
cysynck git lfs-recoveries PROJECT
cysynck git lfs-recovery PROJECT RECEIPT_ID
```

POST `git-lfs-recoveries` takes `offset`/`limit` (default 20, maximum 100), returning summaries
and diagnostics for malformed records. POST `git-lfs-recovery` takes `receipt` for details.
Receipts live privately under `STATE/git-lfs-recoveries/PROJECT/ID.json`, with project/profile
identity, creation time, source peer, planned objects, installed identities and complete/incomplete
status. The catalog is limited to 10,000 entries. A durable incomplete receipt precedes copying;
final completion/failure updates it. Process crashes can leave valid cache additions not yet
listed as installed, so inspect the cache again after interruption. Receipt completion describes
that operation's observed result, not a later integrity check. No automatic retry, expiry,
remote deletion or rollback is performed.

CyUI and Web expose English/French inventory, verification, explicit peer refill and paginated
history. The extracted-package smoke exercises inspection, preview, refill and receipts from
the pure C consumer, then checks cache bytes and unchanged working file/index/HEAD. Native
Linux/macOS and Unreal runtime validation remain separate platform work.

## Checking and reviewing recovery receipts (0.33)

```sh
cysynck git lfs-recovery-check PROJECT RECEIPT_ID
cysynck git lfs-recovery-acknowledge PROJECT RECEIPT_ID --plan CHECK_TOKEN
```

POST `git-lfs-recovery-check` accepts `{"receipt":"ID"}`. It checks every planned object,
including valid copies absent from an interrupted receipt's `installed` list. SHA-256/size
identities must belong to the receipt's recorded committed history, which must still be
available locally. Later HEAD or tracking-ref changes do not invalidate that history check.
No exchange peer or network access is required. The currently approved cache location must
match the original location; a relocated cache produces a diagnostic rather than reading an
arbitrary path from the receipt.

The result includes `record`, `receipt_hash`, `revision`, `store`, `objects`, `verified`,
`missing`, `damaged`, `errors`, `position`, `content_verified`, `can_acknowledge` and `plan`.
Positions are `complete` (all planned bytes verified), `partial`, `missing`, `damaged` or
`unknown` (a configuration/history/receipt check failed). Completion of this integrity check
does not rewrite the operation's original completion status. Checking creates no cache entries
and never changes objects, Git refs/index, working files or receipt metadata.

POST `git-lfs-recovery-acknowledge` requires `receipt` and the latest check's `plan`. It
recomputes full hashes and binds the check to profile, project settings, receipt, configuration,
approved cache and observed file metadata. Only unacknowledged incomplete receipts with no
damaged objects or check errors are eligible. A partial or entirely missing cache can be
explicitly reviewed: the missing count remains visible and those objects still need recovery.
An acknowledgement records a timestamp and verified/missing counts. It preserves `status`,
`applied`, `installed`, original error and all bytes; it is neither a retry nor a success claim.
History summaries expose `acknowledged`, `acknowledged_at` and `needs_attention`.

Checks describe observed state, rather than an atomic filesystem snapshot. External writers
can change bytes after checking or acknowledgement; inspect again when needed. Replaced or
unsafe entries remain preserved. Receipts are private local evidence, not signed attestations.
CyUI/Web expose receipt details and explicit verification/review in English and French. CLI
and C/C++ consumers use the same API, with unchanged C ABI version 1.
