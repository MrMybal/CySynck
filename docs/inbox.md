# Received files

The direct receiver stores incoming files in private state. In 0.8, the desktop, Web
console, CLI and C/C++ API can list them, verify their SHA-256 and export a copy.
Receiving still requires a trusted peer, matching VPN source address and pairing token.
This feature does not change the WireGuard setup or add NAT traversal.

## List, verify and export

```sh
cysynck received list PROJECT_ID
cysynck received verify PROJECT_ID TRANSFER_ID
cysynck received export PROJECT_ID TRANSFER_ID /srv/new-export
```

On the desktop, open **Network → Open received files**. Select a file, verify it,
then enter a new separate destination directory and export. The Web console has a
**Received files** section after selecting a project. Both interfaces default to English
and also support French.

The export directory must not exist. It must be separate from every registered project,
Git exchange folder and the state directory, and its path must not contain links or junctions.
The resulting layout is:

```text
new-export/
  receipt.json
  payload/
    original-name.ext
```

The receipt contains the transfer ID, project, sender, receipt time, filename, byte count
and SHA-256. The payload is copied without overwriting a destination, then checked again.
Exports are private to the local user at creation. The received source is preserved.
An integrity failure found before export creates no directory. A later disk or filesystem
failure can leave an incomplete export; inspect it before retrying with a new destination.

## Catalog and integrity

Payloads remain at `STATE/inbox/PROJECT_ID/TRANSFER_ID.received`. New receipts are retained
in `registry.json` under `received_files`, independently of the 2,000-entry chat history.
Listing checks regular-file availability and byte count; it does not hash every payload.
**Verify** and **Export** read and check the full SHA-256. An availability indicator is
therefore not a successful integrity check.

An exact replay of a received transfer verifies the stored copy before acknowledging it.
Reusing its ID with different content, name or sender is refused. Incoming filenames must
be portable single filenames, with valid UTF-8 and without reserved Windows device names.
Stored paths are derived from validated IDs rather than accepted from receipt fields.

The payload quota remains **1 GiB per profile**, with **100 MiB per file**. Pending resumable
transfers reserve their full size within that quota; see [direct transfers](transfers.md).
The durable
catalog accepts up to **10,000 receipts per project**. This version does not automatically
delete received payloads or receipts. Normal backup object cleanup does not touch the inbox.

## Older profiles

Receipts from 0.7 are read from retained incoming-file messages when no durable receipt
exists. The next received file migrates those retained receipts into the persistent catalog.
Files whose old messages have already expired cannot recover their original receipt this way.
They appear as **unindexed** and are preserved for manual review. They cannot be exported
through a verified operation without a receipt. Malformed receipts and missing files are
reported without suppressing valid entries.

## API

Authenticated local HTTP and `Core::request` expose these POST routes:

| Route | JSON body |
| --- | --- |
| `/api/projects/ID/received-files` | `{}` |
| `/api/projects/ID/received-verify` | `{"transfer":"TRANSFER_ID"}` |
| `/api/projects/ID/received-export` | `{"transfer":"TRANSFER_ID","destination":"/srv/new-export"}` |

The list result contains `files`, `errors` and `unindexed`. Each valid file includes
`available`; its receipt fields include `id`, `project`, `peer`, `created`, `name`, `bytes`
and `sha256`. Listing and exporting do not require the sender or tunnel to remain online.
