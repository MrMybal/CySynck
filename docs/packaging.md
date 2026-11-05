# Platform packages (0.39)

The same C++20 core powers the desktop, CLI and SDK. Syncthing, Git and WireGuard remain
external tools. Native builds are preferred for distribution; the optional Zig toolchain
also compiles the headless Linux core and CLI from Windows.

## Build a native package

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cpack --config build/CPackConfig.cmake -C Release -B dist/packages
```

Use a new output directory for each package variant. CI checks a directory containing exactly
one archive. Each run extracts it to a new directory, executes its CLI, and builds and runs
separate C++ and pure C consumers from the included examples. It also compiles an independent
native plugin with the extracted `CySynck::PluginSDK`, then loads and configures it through
the C consumer. Older ABI v1 plugins remain covered by the native unit fixtures.
The C example checks diagnostics, service planning, UTF-8 paths,
reviewed first-import Git integration of 33 files, exact bytes and receipt verification,
and JSON error handling. Snapshot checks cover metadata
browsing, exact saved-version patches and token-checked partial restoration of whole and CDC
files, with an unselected file kept out of the output. The example also composes
conflict-region choices and applies the resulting edited text through the packaged C ABI.
LFS checks materialize a selected committed pointer from the verified cache, keep a second
uncached pointer unchanged, preserve HEAD/index and
the recovery original, then check and acknowledge a simulated interrupted receipt.
An additional interruption moves the working asset back to verified staging; the extracted
C consumer previews and resumes publication, then verifies exact bytes, the index and original
pointers through the receipt checker.
A second interruption leaves the exact working pointer in place with no prepared asset.
The extracted C consumer previews the required cache copy and preserved pointer, recreates
staging and completes publication while checking the original bytes and unchanged index.
The C consumer then removes the fixture's asset cache object, previews pointer restoration,
preserves the expanded asset in recovery and verifies exact pointer bytes, unchanged index,
an unselected file, absent cache and the schema-2 receipt.
It follows this preparation with a real fast-forward to a different LFS pointer, verifying
the new branch/working pointer and the retained old expanded asset in recovery storage.
The fixture regenerates its known tracked text files and refreshes their index stat metadata
before these working-file checks, avoiding leftover host CRLF conversions from initial setup.
Cache refill checks also verify the receipt's planned objects, then acknowledge a simulated
interruption whose valid immutable copy outlived its last journal update. The original
incomplete state and empty installed list must remain preserved.
An independent receiver also imports a bundle and applies an explicit LFS pointer first import
through the C ABI, checking exact pointer bytes, preserved local data, receipt verification
and absence of newly created asset cache objects.
The LFS recovery and 33-file Git integration/merge blocks allow 120 seconds per consumer call;
other response checks retain their 30-second limit. The materialization and SHA-1/SHA-256 LFS
resume suites allow 900 and 1200 seconds respectively; the pointer-restoration suite allows
600 seconds. These budgets accommodate heavily loaded local machines and are test deadlines,
not throughput guarantees. A timeout should be diagnosed separately from an assertion failure.

| Platform | Archive | Desktop entry point |
| --- | --- | --- |
| Windows | ZIP, including the per-user installation script | `bin/cysynck-desktop.exe` |
| Linux desktop | `.tar.gz`, preserving executable permissions | `bin/cysynck-desktop` |
| Linux server | `.tar.gz` with a `-server` suffix | CLI only, `bin/cysynck` |
| macOS | ZIP with an app bundle, CLI and SDK | `CySynck.app` |

The target architecture is included in Linux and macOS archive names. A package contains
only the architecture selected by the compiler; it is not a universal macOS binary.

## Linux

Extract the archive with `tar -xzf ARCHIVE.tar.gz`. Keep `bin`, `lib` and `share` together.
The CLI and core use a relative `$ORIGIN/../lib` search path. The Web console loads its
assets from `share/cysynck/web`, independently of the working directory.

The desktop package includes `share/applications/io.mrmybal.cysynck.desktop`. Its launcher
uses `cysynck-desktop` from `PATH`; a custom prefix needs its `bin` directory on `PATH`
before registering this entry with the user's desktop environment. Installation does not
automatically enable login startup or a system service.

The user-service template is installed as `share/cysynck/examples/systemd/cysynck.service`.
It assumes an installation in `~/.local/opt/cysynck` and reads optional configuration from
`~/.config/cysynck/service.env`. Review the paths before copying it into the user's systemd
directory. Keep the API on loopback and use an SSH tunnel for remote administration.

## macOS

Open the installed bundle with `open dist/CySynck.app`. The app can be moved separately:
it contains the core in `Contents/Frameworks` and Web assets and dependency licenses in
`Contents/Resources`. The CLI and SDK remain in the package's `bin`, `lib` and `include`
directories. Keep that prefix together when using the CLI or SDK.

The desktop registers its actual bundle location when login startup is explicitly enabled.
The installed CLI locates the sibling `CySynck.app` before constructing startup arguments.
After moving the app, refresh any existing login registration from its Application view.

Installation applies local ad hoc signatures after updating library search paths. These
signatures need no developer credentials and allow local code-integrity checks; they are
not Developer ID signatures or notarization. Public distribution still needs an Apple
signing and notarization workflow. See Apple's documentation on
[embedding libraries in app bundles](https://developer.apple.com/documentation/xcode/embedding-nonstandard-code-structures-in-a-bundle)
and [notarizing macOS software](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution).

The native CI checks the bundle's property list and signature, copies only the app to a
different directory with a space in its name, and launches that copy for a short render.
Linux uses Xvfb for the equivalent desktop smoke check. Those checks run on the next push
or manual workflow dispatch; adding them does not itself confirm a successful run.

## Cross-compile a Linux server with Zig

Requirements: CMake 3.24+, Ninja, Zig, and `llvm-ar` / `llvm-ranlib` on `PATH`. The local
Windows validation used Zig 0.17.1, its Clang/libc++ toolchain, and the default
`x86_64-linux-gnu.2.31` target.

```sh
cmake -S . -B build-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/zig-linux.cmake \
  -DCYSYNCK_DESKTOP=OFF -DCYSYNCK_TESTS=OFF
cmake --build build-linux --parallel
cpack --config build-linux/CPackConfig.cmake -B dist/linux-packages
```

In PowerShell, put the configuration command on one line or use PowerShell's continuation
syntax. If the tools are not on `PATH`, pass `CYSYNCK_ZIG_EXECUTABLE`, `CMAKE_AR`,
`CMAKE_RANLIB` and `CMAKE_MAKE_PROGRAM` as absolute executable paths. Keep Zig's caches in
writable directories using `ZIG_GLOBAL_CACHE_DIR` and `ZIG_LOCAL_CACHE_DIR` when needed.
`CYSYNCK_ZIG_TARGET` also accepts x86_64/aarch64 GNU or musl targets; those other variants
have not been validated here.

Cross-built C++ consumers must use the same compiler and C++ standard-library ABI as the
core. Use the versioned C ABI for integrations built with another C++ toolchain. The native
CI builds its C++ SDK consumer with the same native toolchain as the core.

This produces Linux ELF files, not Windows programs. Enable `CYSYNCK_TESTS` to compile the
test binaries too, but execute them on Linux. The local Windows checks cover compilation,
linking and archive contents; they do not validate Linux system calls, tray integration,
services or runtime behavior. Native Linux and macOS execution remains a release requirement.

CPack running on Windows cannot preserve Unix executable permissions in a cross-built
archive. Extract that archive on Linux with a normal `022` umask, without tar's
`--same-permissions` option, then run `chmod 755 PREFIX/bin/cysynck` before starting the
server. Native Linux packaging preserves executable permissions and its CI smoke check
executes the extracted CLI directly.

The extracted-package smoke also previews proposed backup exclusions through the C ABI, saves
them with their checked plan, previews the saved scope and creates a checked snapshot through CLI.

The extracted C consumer also inspects and refills a missing LFS object from a verified
exchange source, reads its receipts and checks unchanged working files, index and HEAD.
