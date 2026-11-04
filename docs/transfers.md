# Resumable direct files (0.10)

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

Sending captures a stable source and stores a private outgoing snapshot before contacting the
receiver. An interrupted attempt returns `delivered: false`, its `id` and the last acknowledged
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

- File limit: **100 MiB**, including the final partial block; empty files are supported.
- Incoming quota: **1 GiB per profile**, counting received payloads and the full reserved size
  of every incoming partial, including transfers from older clients.
- Outgoing snapshot quota: **1 GiB per profile** for pending transfers.
- At most **32 pending incoming and 32 pending outgoing transfers** per profile. Empty files
  count too. Catalogs are limited to 10,000 records per project.
- Assembly temporarily requires another file-sized allocation. The sender's initial backup
  object is separate from its frozen snapshot and can later be reclaimed by storage pruning.
  These quotas bound logical payloads; leave space for metadata, temporary assembly and captures.

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
Failed attempts also include a stable `error_code`: `interrupted`, `unreachable`,
`unsupported_protocol` or `peer_refused`. Interfaces translate their user-facing guidance.

Local API routes require the normal local API token:

| POST route | JSON body |
| --- | --- |
| `/api/network/send-file` | `{"project":"ID","peer":"PEER","path":"relative.bin","legacy":false}` |
| `/api/network/transfers` | `{"project":"ID"}` |
| `/api/network/transfer-resume` | `{"project":"ID","transfer":"TRANSFER_ID"}` |
| `/api/network/transfer-cancel` | `{"project":"ID","transfer":"TRANSFER_ID","direction":"in"}` |

Peer protocol version 1 uses POST `/transfer/begin` (JSON metadata), `/transfer/chunk` (raw binary)
and `/transfer/finish` (JSON project and ID). All require the mesh Bearer token and
`X-CySynck-Peer`; the receiver replaces any JSON peer with the authenticated header identity
and rechecks the source VPN address and project trust on every request. Begin includes `protocol`,
`id`, `project`, `name`, `bytes` and `sha256`. Chunk headers carry `X-CySynck-Project`,
`X-CySynck-Id`, zero-based `X-CySynck-Chunk` and `X-CySynck-Chunk-SHA256`. Acknowledgements
include `protocol`, `id`, `next`, `acknowledged_bytes` and `delivered`. No local snapshot path,
token or raw peer error body is exposed in the transfer list.
