# Opt-in streamed large workloads

This developer-only test uses real nonrepeating data beyond the signed 32-bit file-size
boundary. It is not part of the exported user manual or the default CTest suite.

```sh
cmake -S . -B build -DCYSYNCK_LARGE_WORKLOAD_TESTS=ON
cmake --build build --config Release --parallel 3
ctest --test-dir build -C Release -R '^cysynck-large-workload$' --output-on-failure
```

Prepare the normal documentation dependencies first. An existing build keeps its other
options. Allow at least 18 GiB free on the work volume. The test reserves a fresh
`large-workload-RANDOM` child of the build directory, refuses links and retains only
`report.json` on success. Failure preserves its isolated data for inspection. The
executable also accepts another existing absolute work parent as its sole argument.

The workload writes 2,304 MiB plus 123 bytes using a fixed-seed `mt19937_64`, little-endian
words and a 1 MiB buffer. The first CDC capture must store every byte as new content,
preventing an all-zero/repeated-block fixture from producing misleading deduplication.
The production HTTP receiver is stopped after at least 32 MiB is acknowledged. Both
cores reopen persisted state, the source is edited, and resume must deliver the original
frozen bytes with the original SHA-256 without overwriting the edit. It then captures a
real CDC backup, inserts a 4,097-byte prefix by streaming, checks at least 95% block-byte
reuse, restores the whole shifted file and hashes every restored byte. Measured process
peak resident memory must stay below 512 MiB; file size is never allocated as one buffer.

`large-workload-windows.json` records the actual Windows x64 Release run against core
revision `cb9941f`, with source/binary hashes for provenance. Results:

- Original/resumed payload: **2,415,919,227 bytes**, including a receiver/core restart and source edit.
- Interruption after **34,603,008 acknowledged bytes**.
- CDC insertion reuse: **2,413,796,351 bytes (99.91196%)**; **2,126,973 new block bytes**.
- Peak resident memory: **38,064,128 bytes** (about 36.3 MiB).
- Generation through full restored-byte verification: **252.69 seconds** on this host.

This is a production-protocol test over loopback with deliberately injected private
fixture identities. Production pairing rejects loopback VPN addresses. It does not
establish WireGuard routing, router/NAT interoperability, universal throughput, power-loss
durability or Linux/macOS large-workload performance. Real WireGuard namespace tests
and native target CI are separate evidence. Do not infer a cross-platform performance
guarantee from this single-machine timing.
