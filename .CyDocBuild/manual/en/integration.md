---
id: integration
title: Integrate the C++ core or C ABI
description: Usage, commands and limits for Integrate the C++ core or C ABI.
section: Developers
order: 71
parent: architecture
revision: 2
---

# Integrate the C++ core or C ABI {#integrate-the-c-core-or-c-abi}

The package contains a C++20 shared library, headers and a relocatable CMake package.
Both desktop and headless builds provide the same APIs. See the [C++ request contract](doc:cpp-api), [C ABI reference](doc:c-api) and [native plugin contract](doc:plugin-api), extracted from the distributed public headers. The index contains include/symbol references rather than implementation listings or inferred C++ overloads. SDK examples are included in
`share/cysynck/examples/consumer` and `share/cysynck/examples/c-consumer`.

## C++20 {#c20}

```cmake
find_package(CySynck CONFIG REQUIRED)
target_compile_features(MyApp PRIVATE cxx_std_20)
target_link_libraries(MyApp PRIVATE CySynck::Core)
```

```cpp
#include <cysynck/core.hpp>
cysynck::Core core(profile_path);
auto report = core.request("GET", "/api/doctor");
```

Build C++ consumers with a compatible compiler, runtime and standard-library ABI. In
particular, a Zig/libc++ cross-build is not interchangeable with a native libstdc++ C++
consumer. The C ABI avoids exposing those C++ types across the library boundary.

The C++ consumer uses a wide Windows entry point so non-ASCII profile paths remain intact. On POSIX, command-line path bytes are interpreted as UTF-8. Host code should pass a native `std::filesystem::path`, or convert UTF-8 explicitly rather than treating locale-encoded Windows `char*` arguments as UTF-8.

## C ABI, version 1 {#c-abi-version-1}

Core 0.30 adds `cysynck_load_plugin(handle, library_path_utf8, &result_json)` while retaining
ABI version 1 for existing entry points. The function requires a 0.30+ core. The header-only
`CySynck::PluginSDK` target and packaged standalone example support native extension authors;
see [plugins and typed per-project options](doc:plugins) for ownership, metadata and routes.

Version 0.29 adds snapshot file browsing, selective restoration and snapshot-to-snapshot
comparison through the existing JSON request entry point. No ABI change is required.
See [snapshot workflows](doc:snapshot-workflows) for routes, freshness tokens and limits.

Include only `cysynck/c_api.h`. The opaque handle and UTF-8 JSON API work from C and can
also be called from Unreal or other C++ applications without sharing C++ standard-library
types. Check `cysynck_abi_version()` before opening a profile.

```c
char *result = NULL;
cysynck_handle core = cysynck_open(state_path_utf8, &result);
if (!core) {
    /* result may contain an allocated JSON error; handle allocation failure too. */
    cysynck_free(result);
    return 1;
}
int status = cysynck_request(core, "GET", "/api/doctor", "{}", &result);
/* status == 0 means success; a nonzero status reports a failure. */
/* Parse or display result before freeing it; it may be NULL on allocation failure. */
cysynck_free(result);
cysynck_close(core);
```

Free every returned string with `cysynck_free`, including JSON errors. Never free it with
the host application's allocator. Keep the handle alive until its calls complete, and do
not close it concurrently with a request. Keep the library loaded until all handles and
returned buffers are released. Relative project paths and API identifiers follow the same
rules as CLI requests; errors are returned as JSON rather than C++ exceptions across the ABI.

The pure C example supports `STATE [METHOD [API_PATH [JSON_BODY]]]`. Its Windows entry
point converts wide command-line arguments to UTF-8, including non-ASCII state paths.
It enables only C in CMake and links through `CySynck::Core` with the C linker.

```sh
cmake -S PREFIX/share/cysynck/examples/c-consumer -B build-c-consumer \
  -DCMAKE_PREFIX_PATH=PREFIX -DCMAKE_BUILD_TYPE=Release
cmake --build build-c-consumer --config Release
```

Keep the core DLL beside the executable on Windows; the example copies it automatically.
POSIX examples use the library location recorded by CMake. Distribution of your own app
still needs its own library loading/deployment strategy. Test against the actual target OS.

The Unreal bridge includes asynchronous FIFO Blueprint requests and a staging helper;
see [Unreal integration](doc:architecture#autres-applications-c-et-unreal) for lifetime,
queue, shutdown and target-validation requirements.

Reviewed first-import and fast-forward branch integration uses the same request interface; see
[Git integration](doc:git-integration) for preview tokens, working-tree restrictions and receipts.
The same C ABI also lists integration receipts and verifies or acknowledges interrupted
operations through `git-integrations`, `git-integration-check` and `git-integration-acknowledge`.
These JSON routes require no ABI version change.

From 0.23, `git-merge-review` and `git-merge-file` expose three-way committed comparison and
bounded per-file proposals through the same C/C++ request interface. Pin both commits, the
local ref and common ancestor; no apply action exists. See [three-way Git review](doc:git-merge-review).

From 0.24, `git-merge` adds reviewed divergent branch integration through the same C ABI version 1.
Preview returns the complete result tree and identity; explicit application uses its plan token
and returns a two-parent `result_commit` with a recovery receipt. See [reviewed merge](doc:git-divergent-merge).

From 0.25, a `resolutions` array chooses whole `local`/`peer` committed files for supported text
conflicts, including a missing side's deletion. Each item contains only `path` and `side`.
Preview and application send the same array; `result_ready` distinguishes a complete candidate
from unresolved conflicts. Receipt checks recompute these choices through the same C ABI.

From 0.26, an item can use `{"path":"file.cpp","side":"edited","content":"combined text\n"}`.
UTF-8 text, bounds, marker and permission checks apply before any source objects are published.
The same edited bytes are required for application and retained privately for receipt checks.
No C ABI version change is needed; see [manual editing](doc:git-divergent-merge#editing-conflict-text-026).

From 0.27, `git-merge-file` exposes structured conflict regions. Submit its `region_token` and
ordered `region_choices` (`local`, `peer` or `both`) to regenerate a bounded `composed_text` draft.
The operation remains read-only. Pass the reviewed text as an `edited` resolution to `git-merge`
for normal preview/application and receipt checking. No public C/C++ signature or ABI changes.
See [conflict regions](doc:git-divergent-merge#choosing-individual-conflict-regions-027).

From 0.28, `git-materialize` previews and materializes exact committed LFS pointers using only
verified local cache bytes. `git-materializations`, `git-materialization-check` and
`git-materialization-acknowledge` list, verify and acknowledge its private recovery receipts.
The current HEAD and raw index are preserved. No new public C/C++ signature or C ABI is needed.
See [LFS working-tree materialization](doc:git-materialization).

Backup storage policies also use the existing ABI: create or update a project with
`{"backup_storage":"cdc"}`, then use `backup`, `verify`, `restore-plan` and `restore`.
CDC recipes require core 0.22 or later. See [backup storage](doc:block-backups) for bounds,
mixed-format history and storage statistics.

Core 0.31 exposes [backup scope previews and checked exclusion updates](doc:backup-scope)
through the existing JSON request entry point; no C ABI change is required.

Core 0.32 adds [offline LFS cache inventory, refill and receipts](doc:git-cache) through the
existing C/C++ JSON request entry point, retaining C ABI version 1.

Core 0.33 adds `git-lfs-recovery-check` and `git-lfs-recovery-acknowledge` for full receipt
object verification and fresh-token acknowledgement of interruptions. Original incomplete
status/installed counts are preserved; see [receipt checks](doc:git-cache#checking-and-reviewing-recovery-receipts-033).

Core 0.34 accepts `lfs_policy:"pointers"` on `git-integrate` for explicit pointer-only
fast-forward/first-import checkouts, with `lfs_before`/`lfs_after` counts. The C ABI stays at
version 1. See [LFS pointer integration](doc:git-lfs-integration).
