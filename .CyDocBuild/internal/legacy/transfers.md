# Resumable direct files (0.12)

Direct files use the existing WireGuard peer identity and project permissions. Start a receiver
on the destination (`chat serve` or the desktop Network view) and set the same random
`CYSYNCK_MESH_TOKEN` on both devices. Transfer HTTP runs inside the VPN; it does not add TLS,
NAT traversal, an Internet relay or offline delivery.

## Send and resume

```sh
cysynck send-file PROJECT_ID PEER_ID relative/path.bin
cysynck transfers list PROJECT_ID
cysynck transfers resume PROJECT_ID TRANSFER_ID
cysynck transfers cancel PROJECT_ID TRANSFER_ID --direction out
```

Sending streams a stable source into one private outgoing snapshot before contacting the
receiver, using a 64 KiB copy buffer and SHA-256. It does not create a backup object or load the
whole file into memory. An interrupted attempt returns `delivered: false`, its `id` and the last acknowledged
byte count. Resume uses that ID and frozen copy, even if the original file changes or disappears.
Running `send-file` again creates a **new** transfer. Resume is explicit; there is no background
retry loop. Only one outgoing attempt runs per state profile at a time.

The receiver verifies each 1 MiB block, writes it atomically, then records its SHA-256 in the
profile journal. On resume it verifies its stored prefix before returning the next block index.
An identical repeated block is accepted; a changed or out-of-order block is refused. The receiver
may be ahead of the sender when an acknowledgement was lost. Resume reconciles that difference.
Final assembly checks the complete size and SHA-256 before publishing an inbox payload and a
durable receipt. Retrying after publication, before receipt persistence, validates the already
published bytes. Retrying a delivered transfer does not create another history message.

The desktop Network view and Web console offer send, progress refresh, resume and local removal
in English and French. Progress is the last saved acknowledgement, rather than a live network
speed estimate. A received transfer is exported and verified through the existing
[received files interface](inbox.md).

## Local removal and limits

```sh
cysynck transfers cancel PROJECT_ID TRANSFER_ID --direction in
```

Removal deletes only the selected local snapshot or partial blocks and its transfer journal.
It preserves received files and receipts, and does not contact the other peer. Removing an
incoming partial makes the sender restart that ID from block zero on its next resume. Removing
an outgoing entry leaves any receiver partial for that receiver to remove locally. Delivered
outgoing entries can also be removed to free catalog space.

- Default file limit: **1 GiB**, configurable from **1 MiB to 4 GiB**; empty files are supported.
- Default incoming quota: **4 GiB per profile**, counting received payloads and the full reserved size
  of every incoming partial, including transfers from older clients.
- Default outgoing snapshot quota: **4 GiB per profile** for pending transfers.
- Each quota is configurable from **1 MiB to 64 GiB**, and must cover the configured file limit.
- At most **32 pending incoming and 32 pending outgoing transfers** per profile. Empty files
  count too. Catalogs are limited to 10,000 records per project.
- Assembly temporarily requires another file-sized allocation. These quotas bound logical
  payloads; leave space for metadata and temporary assembly. Sender capture needs one frozen copy.

Limits are local to the state profile and apply across all projects. Profiles without saved
limits use these defaults. Set compatible limits on both devices before sending a larger file:

```sh
cysynck transfers limits
cysynck transfers configure --file-limit-mib 4096 --inbox-quota-mib 8192 --outbox-quota-mib 8192
```

The desktop Direct transfers view and Web console expose the same fields in English/French.
Values must be whole MiB. A quota reduction below received bytes plus incoming reservations,
or below pending outgoing snapshots, is refused. Lowering the per-file limit affects new
transfers; existing partials and receipts remain resumable, verifiable and exportable.
Usage reports `inbox_bytes`, `reserved_incoming_bytes` and `reserved_outgoing_bytes`.
Failed capture removes its own unpublished snapshot; directories left by a process crash
remain preserved for local review. Cleanup checks exact block filenames in linear time.

Successful delivery frees partial blocks and the outgoing snapshot. A crash can leave cleanup
work behind; `cleanup_pending: true` reports a delivered payload whose partial cleanup needs
local review. Unexpected files, links, damaged chunks and unindexed directories are preserved
and refused rather than recursively deleted. Partial transfers never expire automatically.
This protocol handles process interruption and retry; it is not a guarantee against disk failure.

## Compatibility and API

Old receivers can be selected explicitly:

```sh
cysynck send-file PROJECT_ID PEER_ID relative/path.bin --legacy true
```

That sends one request with the original 100 MiB limit and no resume. No automatic fallback
creates a second transfer. The receiver still accepts `/file` and `/message`; existing inbox
receipts and exports remain compatible. A 404 from a resumable endpoint is reported in the
outgoing journal with an upgrade or legacy-mode instruction.
Larger version-1 transfers require a receiver from 0.12 or later with adequate limits;
older resumable receivers retain their 100 MiB cap and refuse them. Wire chunk size stays 1 MiB.
Failed attempts also include a stable `error_code`: `interrupted`, `unreachable`,
`unsupported_protocol` or `peer_refused`. Interfaces translate their user-facing guidance.

Local API routes require the normal local API token:

| POST route | JSON body |
| --- | --- |
| `/api/network/send-file` | `{"project":"ID","peer":"PEER","path":"relative.bin","legacy":false}` |
| `/api/network/transfers` | `{"project":"ID"}` |
| `/api/network/transfer-resume` | `{"project":"ID","transfer":"TRANSFER_ID"}` |
| `/api/network/transfer-cancel` | `{"project":"ID","transfer":"TRANSFER_ID","direction":"in"}` |
| `/api/network/transfer-configure` | `{"file_limit_mib":4096,"inbox_quota_mib":8192,"outbox_quota_mib":8192}` (fields may be omitted) |

GET `/api/network/transfer-limits` returns effective limits, supported maxima and profile usage.
These routes also work through the C/C++ SDK and remote CLI. Legacy receives share the same
inbox quota and configured per-file limit, capped at 100 MiB for their single request.

Peer protocol version 1 uses POST `/transfer/begin` (JSON metadata), `/transfer/chunk` (raw binary)
and `/transfer/finish` (JSON project and ID). All require the mesh Bearer token and
`X-CySynck-Peer`; the receiver replaces any JSON peer with the authenticated header identity
and rechecks the source VPN address and project trust on every request. Begin includes `protocol`,
`id`, `project`, `name`, `bytes` and `sha256`. Chunk headers carry `X-CySynck-Project`,
`X-CySynck-Id`, zero-based `X-CySynck-Chunk` and `X-CySynck-Chunk-SHA256`. Acknowledgements
include `protocol`, `id`, `next`, `acknowledged_bytes` and `delivered`. No local snapshot path,
token or raw peer error body is exposed in the transfer list.
