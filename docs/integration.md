# Integrate the C++ core or C ABI

The package contains a C++20 shared library, headers and a relocatable CMake package.
Both desktop and headless builds provide the same APIs. SDK examples are included in
`share/cysynck/examples/consumer` and `share/cysynck/examples/c-consumer`.

## C++20

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

## C ABI, version 1

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

The package smoke check extracts a fresh archive, builds and runs both included examples,
then calls diagnostics and service planning through the C ABI, exercises a UTF-8 profile
path and checks a JSON error. This is separate from compiling the Unreal bridge example:
`integrations/unreal/CySynck` still needs an actual Unreal build and runtime test.

Reviewed first-import and fast-forward branch integration uses the same request interface; see
[Git integration](git-integration.md) for preview tokens, working-tree restrictions and receipts.
The same C ABI also lists integration receipts and verifies or acknowledges interrupted
operations through `git-integrations`, `git-integration-check` and `git-integration-acknowledge`.
These JSON routes require no ABI version change.

From 0.23, `git-merge-review` and `git-merge-file` expose three-way committed comparison and
bounded per-file proposals through the same C/C++ request interface. Pin both commits, the
local ref and common ancestor; no apply action exists. See [three-way Git review](git-merge-review.md).

Backup storage policies also use the existing ABI: create or update a project with
`{"backup_storage":"cdc"}`, then use `backup`, `verify`, `restore-plan` and `restore`.
CDC recipes require core 0.22 or later. See [backup storage](block-backups.md) for bounds,
mixed-format history and storage statistics.
