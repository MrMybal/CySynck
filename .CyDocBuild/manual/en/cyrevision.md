---
id: cyrevision
title: Signed CyRevision Git and LFS exchange
description: Connect an explicitly trusted CyRevision transport to a Git exchange project.
section: Git
order: 32
parent: git-lfs
revision: 1
---

# Signed CyRevision Git and LFS exchange {#signed-cyrevision-exchange}

The C++ adapter reads and writes the signed Git/LFS transport of CyRevision 0.1.34
(reference commit `9332a13d88e9c1ea939af8eef4f779ec52116fb3`). It uses the existing
Git exchange project and a separate shared directory. Each participant keeps its
own repository, working files and private state. Syncthing transports the exchange
directory. See [Git setup](doc:git-setup) and [the native exchange](doc:git-lfs).

This adapter needs Git and an external OpenSSL executable supporting ECDSA P-256.
Set `CYSYNCK_OPENSSL` to its full path if it is not available on PATH. Windows also
looks in Git for Windows' `mingw64/bin` and `usr/bin`; macOS checks the usual
Homebrew OpenSSL 3 directories. .NET is only needed for developer reference tests.
Git LFS tracking tools remain useful when authoring assets; CySynck itself reads
canonical pointers and copies verified objects without executing filters or hooks.

## Connect a project {#connect-a-project}

Create or select a `git-bundles` project with an existing local repository and a
separate exchange directory. Obtain its **CyRevision project GUID**, not its
CySynck project ID, from the project administrator. The association is immutable:
use another CySynck project/profile for another remote project.

```sh
cysynck cyrevision identity --name "Alice workstation"
cysynck cyrevision configure game-team 12345678-1234-4234-8234-123456789abc
cysynck cyrevision status game-team
```

The first command creates a private P-256 signing key in the local profile and
prints only the public identity and its SHA-256 SPKI fingerprint. Repeating it
preserves the existing identity and key. Keep the profile private and backed up;
publish only the public JSON. Syncthing's device ID is a different identity.

For an **unmodified Windows CyRevision receiver**, append
`--signature-format windows-legacy` when configuring. For an unmodified Linux/macOS
receiver use `lf` (the default). CyRevision's original indented canonical serializer
uses OS-dependent newlines: its old Windows and Unix receivers cannot both verify
the same newly signed payload. CySynck verifies both encodings. Developer tooling
prepares and tests a narrow CyRevision compatibility patch that signs LF and
verifies both encodings, without modifying the reference checkout. The patch is
not automatically applied to another installed application.

## Approve publishing devices {#approve-publishing-devices}

Obtain the other device's public `DeviceIdentity` JSON, then independently verify
its fingerprint with that person. Do not copy an unverified fingerprint from the
same shared folder. The fingerprint is lowercase SHA-256 of the decoded SPKI DER.

```sh
cysynck cyrevision trust game-team peer-public.json --fingerprint VERIFIED_SHA256
cysynck cyrevision revoke game-team aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee
```

`peer-public.json` can contain the identity itself or CySynck's public
`{"identity": {...}, "fingerprint": "..."}` response. Trust pins the device GUID,
user GUID and public key for this project. An existing pinned identity must be
explicitly revoked before replacement. Revocation stops new imports and keeps
already imported review branches. It does not erase data or revoke another
application's membership certificates.

CyRevision group admission and roles remain managed by CyRevision. A public
CySynck identity is not automatically a member of an existing CyRevision group.
Its authorized-device collection must include that identity before CyRevision
accepts its publication. Transport interoperability does not bypass group admission
or implement the complete CyRevision membership/role UI.

## Publish, import and review {#publish-import-and-review}

```sh
cysynck cyrevision export game-team
cysynck cyrevision inbox game-team
cysynck cyrevision import game-team TRANSACTION_GUID
cysynck git branches game-team
```

Export signs all local branches and, when the project's Git LFS option is enabled,
publishes their available LFS history and a signed inventory. Objects at branch
tips have current priority; older objects have archive priority. A CyRevision
receiver's own LFS transfer policy determines whether it retrieves archive data.
Missing/corrupt local export objects cause refusal. Metadata is published only
after the bundle and object data exist. Private signing keys never enter the
exchange folder.

Import verifies the project, pinned author, P-256 signature, transaction directory,
bundle checksum and exact signed branch list. Git reads a private copy of the
bundle. Branches go to `refs/remotes/cysynck/cr-FULL_DEVICE_GUID_WITHOUT_DASHES/`.
The update/prune transaction affects only that publishing device's review refs.
Git HEAD, the index and working files are preserved. Use the existing
[review](doc:git-review), [extraction](doc:git-extraction) and
[integration](doc:git-integration) tools afterward.

Signed inventories authorize LFS copies only for objects referenced by the
imported history. SHA-256 and size are checked again on the copied bytes. Absent
objects are reported as `lfs_deferred`; repeat the same import after they arrive
to refill the cache. Import does not expand working assets. Use
[materialization](doc:git-materialization) after the normal review/integration.

Nonzero publication sequences must increase. Stale/conflicting publications are
refused; an identical already imported publication is idempotent. Editing this
adapter's review refs externally causes a refusal rather than silently overwriting
them. An inbox reports incomplete, unauthorized or invalid publications separately.

## Recover an interrupted import {#recover-an-interrupted-import}

An intent is recorded before Git's atomic ref transaction. If execution stops
before the receipt is saved, inspect its publishing device:

```sh
cysynck cyrevision recover game-team DEVICE_GUID
cysynck cyrevision recover game-team DEVICE_GUID --apply true --plan CURRENT_TOKEN
```

The preview distinguishes unchanged refs, the exact committed result, and refs
changed elsewhere. Applying a fresh matching preview records the exact completed
import or clears an intent that never committed. It does not alter refs or working
files. Changed refs require inspection; no automatic recovery overwrites them.

## Interfaces, API and limits {#interfaces-api-and-limits}

Open **Git exchange → CyRevision exchange** in the desktop or Web interface.
The panel exposes identity creation, configuration, pinned-device approval and
revocation, publication, import and checked recovery. Its Help button opens this
page in the selected language.

| Operation | Method and JSON route |
| --- | --- |
| Read/create public identity | `GET`/`POST /api/cyrevision/identity` (`name` for creation) |
| Read settings / signed inbox | `GET /api/projects/ID/cyrevision-status` / `cyrevision-inbox` |
| Associate project | `POST /api/projects/ID/cyrevision-configure` (`project_id`, `signature_format`) |
| Approve/revoke publishing device | `POST .../cyrevision-trust` (`identity`, `fingerprint`) / `cyrevision-revoke` (`device`) |
| Publish / import | `POST .../cyrevision-export` / `cyrevision-import` (`transaction`) |
| Recovery preview/application | `POST .../cyrevision-recover` (`device`, optional `apply`, `plan`) |

Metadata is bounded to 1 MiB, 32 JSON levels, 1,024 branches and 4,096 LFS objects.
The native LFS limits are 4 GiB per object and 16 GiB total per publication; bundles
are limited to 16 GiB. An exchange accepts at most 4,096 transaction directories
and 1,024 inventory entries before asking for archival. CySynck's native export
cleanup does not delete CyRevision transactions. No automatic group membership,
LFS request fulfillment, transport garbage collection or partial Git-bundle resume
is claimed. The developer reference test verifies actual bidirectional Git/LFS
imports with CyRevision; platform and release validation is described in
[availability](doc:availability).
