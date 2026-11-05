# Native plugins, typed modes and the extension SDK

CySynck 0.30 adds a loaded-plugin catalog, typed per-project mode options and an additive
C entry point for loading libraries from another application. Plugin ABI v1 remains unchanged.
Legacy plugins do not need the optional descriptor; their IDs and strings pass host validation.

## Load and inspect

Loading executes native code inside the host, with the host's OS permissions. Select libraries
explicitly at startup or through the C++/C API. The Web console lists and configures libraries
already loaded by its host. It provides no library loading or upload endpoint.

```sh
cysynck --plugin /absolute/path/plugin.so plugins
cysynck-desktop --plugin /absolute/path/plugin.so
```

Use `.dll` on Windows or `.dylib` on macOS. The native **Plugins** view and Web catalog show
names, versions, mode descriptions and optional French labels. Advanced details include the
plugin ID, canonical selected path and SHA-256 recorded while loading. This fingerprint
identifies the selected file; it is not a signature or a measurement of dependent libraries.
The loader checks for changes during loading, duplicate libraries/IDs/modes, invalid metadata
and unsupported ABIs before publishing the mode catalog. There are at most 32 libraries and
100 modes per library. Libraries remain loaded until their Core instance is destroyed.
Plugin IDs contain up to 128 ASCII letters, digits, dots, underscores or hyphens, starting with
a letter or digit. Mode IDs use the existing 80-character identifier rules without dots.
Their before/after callbacks retain the existing cross-project event behavior.
Callbacks run synchronously inside the host request. Do not re-enter the same Core instance
or open its profile through another Core while that callback is running.

GET `/api/plugins` returns `abi_version` and `plugins`. `overview.modes` includes a plugin mode's
owner, descriptions, optional French labels, option fields and schema hash. Built-in modes
remain available alongside explicitly loaded extensions. Libraries are not auto-discovered,
persisted as a loading instruction, downloaded or unloaded through a request route.

## Per-project options

Open project **Settings → Mode options** in CyUI, or the project's **Mode options** section in
Web. Supported controls are strings, Booleans and bounded integers. Fields use the plugin's
French labels when supplied and English otherwise. Reload fetches current settings; **Use
defaults** changes the draft, and **Save mode options** validates and persists it explicitly.

```sh
cysynck --plugin /path/plugin.so project mode-options PROJECT
cysynck --plugin /path/plugin.so project mode-options-set PROJECT options.json --plan TOKEN
```

`options.json` is a UTF-8 object of explicit values. Missing fields use declared defaults.
GET `/api/projects/PROJECT/mode-options` returns `available`, `valid`, `plugin`, `fields`,
`values`, `defaults`, `errors`, `schema_hash` and `plan`. PUT to that same route accepts
`{"plan":"TOKEN","values":{"title":"My report","include_names":true}}`.
The token binds the profile, project/root, mode, loaded schema/library identity and current
stored values. Changed settings or a changed plugin require reloading before saving.

The project property `mode_options` also accepts validated values in ordinary project POST/PUT
requests. Those bulk settings calls do not require the editor token. IDs, roots and modes remain
immutable on existing projects. Options are stored locally per project in the private profile.
Resolved defaults and overrides are passed both in `project_json.mode_options` and
`context_json.mode_options` when its mode runs. Context also contains `state` and `plugin`.
Invalid saved options block mode execution before backup-before-sync or event callbacks.
The options inspector exposes defaults and diagnostics for an explicit repair. A missing
plugin preserves the project and saved options; separate backup operations remain available.

Limits: 32 fields per mode, 32 KiB of resolved options, strings of at most 4,096 UTF-8 bytes
(default limit 1,024), and integers within -1,000,000,000 through 1,000,000,000. Values are
not coerced: numbers cannot replace Booleans, fractions cannot replace integers, unknown keys
are rejected, and strings must be valid UTF-8 without NUL. An option's narrower declared
range/byte limit applies to defaults and overrides. Mode result strings are bounded to 1 MiB.

## Optional descriptor export

Keep `cysynck_get_plugin_v1` and the structs from `cysynck/plugin.h`. Optionally export:

```cpp
extern "C" CYSYNCK_PLUGIN_EXPORT
const char *cysynck_get_plugin_descriptor_v1() {
    return R"({"schema":1,"name":"Report extension","version":"1.0.0",
      "modes":{"my-mode":{"label_fr":"Mon mode","fields":[
        {"id":"title","label":"Title","label_fr":"Titre","type":"string",
         "default":"Report","max_length":160},
        {"id":"enabled","label":"Enabled","type":"boolean","default":true},
        {"id":"limit","label":"Limit","type":"integer","default":10,
         "minimum":1,"maximum":100}
      ]}}})";
}
```

Return immutable NUL-terminated UTF-8 JSON, valid for the library lifetime. The host copies
it during loading and does not call `release_string` for this static metadata. The export is
optional; returning NULL from an existing export is an error. Descriptors are limited to 64 KiB
and twelve levels of nesting. Root fields are `schema`, `name`, `name_fr`, `version`,
`description`, `description_fr` and `modes`. The modes object may describe only actual exported
mode IDs, with `fields`, `label_fr` and `description_fr`. English labels/descriptions come from
the existing mode struct. Fields have a unique ASCII `id`, English `label`, optional `label_fr`,
`description`/`description_fr`, `type`, `default` and type-specific constraints. Unknown metadata
keys, duplicate fields, inconsistent constraints and invalid defaults reject the entire library.

Plugin entry points, mode arrays and strings must remain valid while loaded. Callback result
ownership remains ABI v1: the host copies mode results/veto strings and calls `release_string`
when provided, including parse/bound failures. Static returned strings may omit that callback.
Plugins should return valid JSON and keep exceptions within their own implementation; native
code shares the host process. The C request boundary converts unexpected C++ exceptions to JSON
errors but cannot isolate arbitrary native memory faults.

## Build an independent plugin from the installed SDK

The package includes `share/cysynck/examples/plugin`. It links the header-only
`CySynck::PluginSDK` target, without linking the core binary:

```sh
cmake -S PREFIX/share/cysynck/examples/plugin -B plugin-build -DCMAKE_PREFIX_PATH=PREFIX
cmake --build plugin-build --config Release
```

Its `project-report` mode inspects a bounded number of top-level directory entries and reports
regular-file counts and sizes. `include_names` optionally includes filenames; `title` and
`max_entries` are also configurable. It does not hash payloads or traverse subdirectories.
The source demonstrates static bilingual metadata and dynamically allocated results released
by the plugin's own allocator. Build for the host's OS and architecture and match its native
calling convention; the wire interface itself contains C structs and UTF-8 JSON.

## Embed from C or C++

```c
char *result = NULL;
int status = cysynck_load_plugin(handle, library_path_utf8, &result);
/* status == 0: result contains the loaded catalog; otherwise a JSON error. */
cysynck_free(result);
```

This additive export requires core **0.30 or later**, while `cysynck_abi_version()` remains 1
for existing entry points. Use `find_package(CySynck 0.30 CONFIG REQUIRED)` when building a new
consumer of this function. C++ consumers can also call `Core::load_plugin`. Serialize loading
with the lifetime of the host; keep the handle and library alive until requests complete.
The packaged pure C consumer accepts `STATE [--plugin LIBRARY] [METHOD [PATH [JSON]]]`.
Package smoke tests build a fresh plugin from the extracted SDK, load it through that C
consumer, configure a project and execute its mode. Unreal compilation and native Linux/macOS
runtime validation remain separate platform checks.
