# CyRevision reference tests

These tests use the real CyRevision records and Git exchange implementation. They are
development tools, not an application runtime dependency. No private signing key is stored
in the committed fixtures.

Use an installed .NET 8 runtime and a compatible SDK, plus a CyRevision source checkout.
Redirect **all** referenced projects' build artifacts into this workspace:

```sh
dotnet build tests/interop/CyRevisionHarness/InteropHarness.csproj -c Release \
  -p:CyRevisionSource=/absolute/CyRevision --artifacts-path /absolute/CySynck/build-interop
```

The resulting `InteropHarness.dll` supports `escaping`, `vectors`, `inventory-vectors`, `canonical FILE`,
`export REPOSITORY EXCHANGE PROJECT_GUID IDENTITY_DIRECTORY NAME`, and
`import REPOSITORY EXCHANGE PROJECT_GUID STATE_DIRECTORY AUTHORIZED_IDENTITIES_JSON`.
Use only isolated test repositories/state directories. Export generates a test device's
private identity in the supplied directory; do not commit or distribute that directory.

`escaping` and `vectors` produce the data in `tests/fixtures/`. The current known-answer
vectors were generated on Windows from CyRevision 0.1.34; indented canonical payloads use
CRLF there. Review regenerated data and its provenance before replacing a fixture.
The native codec tests also exercise LF publications and reject altered signatures,
unexpected identities, duplicate properties, unsupported fields and invalid metadata.

Run the actual bidirectional Git/LFS test (it creates a new owned fixture directory):

```sh
python tests/interop/roundtrip.py --cysynck /absolute/bin/cysynck \
  --harness /absolute/build-interop/bin/InteropHarness/release/InteropHarness.dll \
  --work-parent /absolute/CySynck/.local
```

The default targets an unmodified Windows CyRevision receiver with `windows-legacy`
signatures. Use `--signature-format lf` for an unmodified Unix reference or the corrected
reference. The harness imports only explicitly authorized public identities; it does not
create CyRevision group membership or exercise its desktop admission UI.

To test the newline correction without editing another checkout:

```sh
python tests/interop/prepare_reference.py --reference /absolute/CyRevision \
  --work-parent /absolute/CySynck/.local
```

This copies only Core/Security/Git projects and their original AGPL license into a fresh
test directory, then produces `cyrevision-canonical-lf.patch`. Build the harness again
against that copy with a **different** `--artifacts-path`; run both `lf` and
`windows-legacy` roundtrips. Existing Windows signatures must remain verifiable. The
copy/patch stays outside application packaging. Never silently apply it to the reference
checkout or redistribute copied AGPL implementation as MIT application code.
