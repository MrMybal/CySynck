# CyRevision interoperation work

This is developer evidence, outside the exported manual. The signed transport is implemented;
the final native target CI and full application admission integration remain separate checks.

## Verified reference

CyRevision 0.1.34 at commit `9332a13` exposes `GitPeerBundleManifest`,
`SignedGitPeerBundle`, ECDSA P-256 SPKI public keys and SHA-256 signatures in .NET's
IEEE P1363 format. Publications live in `transactions/GUID_N/transaction.json` with
`repository.bundle`; LFS objects live in `lfs/objects/aa/bb/SHA256` and have separate
signed inventories. No CyRevision implementation sources are embedded in the native core.

The test harness compiles its real Git/Security/Core projects with artifacts redirected
inside this workspace. The original CyRevision checkout remains unchanged. The committed
fixtures contain only generated public keys/signatures and synthetic metadata; no private key.
They cover all ASCII escaping, Unicode/surrogates, dictionary order, dates/offsets, optional
sequence fields and .NET P1363-to-OpenSSL DER conversion.

## Existing newline defect

`CanonicalBytes` currently serializes indented JSON without fixing its line endings.
The reference Windows fixture has 17 CRLF pairs in its 883-byte payload. Replacing these
with LF changes the signed bytes, although the parsed metadata is identical. The native
reader explicitly checks the pinned key and verifies either canonical LF or legacy CRLF.
This does not accept unsigned field changes or skip ECDSA validation.

Exports for an unmodified Windows CyRevision peer need its legacy CRLF mode. Linux/macOS
peers use LF. A mixed group of unmodified CyRevision receivers needs an upstream correction;
one signature cannot bind two different byte strings. Prepare a narrow source patch that
always signs LF and verifies both LF and existing CRLF signatures. Do not change the user's
other checkout or claim the counterpart is fixed before that patch is applied and verified.

## Implemented and locally verified

- Immutable project GUID association, separate private signing identity and explicit key fingerprints.
- Signature validation before bundle use; a private bundle snapshot is checked against the signed hash/heads.
- Owned full-GUID review namespaces, atomic compare-and-update/prune, monotonic sequences and checked interruption recovery.
- Signed LFS inventories, verified content-addressed copies and delayed-object retries.
- Actual .NET to C++ and C++ to .NET Git/LFS imports: original Windows reference,
  isolated corrected LF reference, and corrected reference accepting legacy Windows exports.
  Each uses an independent 2,097,424-byte asset, matching hashes and unchanged receiving worktrees.
- Three native codec/crypto/transport tests include hostile metadata, stale publications,
  revoked devices, ref ownership, nested Windows paths and receipt recovery.
- CLI, native CyUI/Web panels, bilingual local help and four Web DOM tests for project isolation,
  independent fingerprint input, recovery token invalidation and untrusted-name rendering.

## Boundaries and remaining target evidence

CyRevision group admission/certificates and role administration stay in CyRevision. The reference
harness supplies its authorized-device collection explicitly; do not present those tests as an
end-to-end test of its desktop membership UI. The independent C++ transport is usable between
CySynck peers through explicit pins. LFS request fulfillment and transport garbage collection
are not implemented. The compatibility correction is generated/tested in an owned copy only;
the original user checkout remains unchanged and its AGPL source is not an application dependency.

The latest full Windows native suite and final Linux/macOS CI still need to complete after this
transport change. DOM tests do not establish native browser rendering or file-origin storage behavior.

External OpenSSL is used only for ECDSA operations; C++ owns the protocol, validation and
orchestration. .NET is used only by the independent reference test harness, not at application runtime.
