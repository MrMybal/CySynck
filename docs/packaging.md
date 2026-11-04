# Platform packages (0.24)

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

Use a new output directory for each package variant. The CI runs package smoke checks
against a directory containing exactly one archive, extracts it to a new directory, then
executes its CLI and builds and runs separate C++ and pure C SDK consumers from the examples
inside that extracted package. The C example also checks diagnostics, service planning,
UTF-8 paths, reviewed first-import Git integration and JSON error handling.

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
