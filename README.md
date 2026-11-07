# CySynck

English · [Français](README.fr.md)

A project-based synchronization and backup companion in **C++20**, powered by [Syncthing](https://github.com/syncthing/syncthing) and [CyUI](https://github.com/MrMybal/CyUI).
One shared core serves the native Windows/macOS/Linux desktop, CLI, local Web console and integration SDK. Linux servers can build without GUI dependencies.

**Version 0.40.0 — active development.** Windows x64 is tested locally; native Linux/macOS and the synchronous Unreal/Blueprint bridge need target-platform validation. Syncthing, Git and WireGuard are external installations.

## Documentation

The complete offline **CyDoc** manual includes **English (default)** and **French**, stable contextual links, search, bookmarks and reading positions. It works directly from `file://` without a server, CDN or Internet connection.

- Installed/build package: open `Documentation/Documentation.html` with its complete `DocData/` directory.
- Native desktop: **Help (F1)** opens the current view's topic.
- CLI: `cysynck docs`, or `cysynck docs git-materialization --language fr --print` for a local URL without launching a browser.
- Web: contextual Help offers local opening on a loopback server and the complete offline archive for a remote workstation. Extract the archive before opening its reader.
- Repository: [English manual sources](.CyDocBuild/manual/en/overview.md) · [French manual sources](.CyDocBuild/manual/fr/overview.md) · [documentation regeneration](.CyDocBuild/README.md).

The reader uses no optional media modules and no Three.js. Architecture diagrams compile to static SVG. Public C++/C/plugin reference pages come from the three distributed headers; private implementation is not published in the reader.

## Features

| Area | Available workflows |
| --- | --- |
| Projects and synchronization | Two-way, send-only, receive-only, backups, Git exchange and trusted native plugin modes |
| Desktop | CyUI overview, diff/recovery, backups, Git/LFS, network, tray, per-profile login startup and command panel |
| Backups | Scheduling, exclusions with preview, retention, whole/fixed-block/CDC storage, verification and selective restoration |
| File review | Bounded side-by-side/unified differences, Git-compatible patches, explicit conflict resolution and preserved recovery copies |
| Git collaboration | Setup, verified bundle/LFS exchange, branch/commit/three-way review, separate extraction and reviewed fast-forward/divergent merges |
| LFS working files | Cache inspection/refill, selected materialization, interruption checks/resume, pointer restoration and export of preserved assets |
| Network | WireGuard pairing, direct chat, verified resumable files, profile quotas and received-file exports |
| SDK and plugins | C++20 core, C ABI v1 with UTF-8 JSON, typed plugin options and buildable C/C++/plugin examples |
| Operations | Diagnostics, reviewed systemd/LaunchAgent export and explicit storage cleanup |

See the manual for exact limits, preview/application steps and recovery behavior. WireGuard NAT traversal/packet relay, offline chat delivery, signed installers/updates and CyRevision protocol interoperability are not implemented. Multi-file operations are not filesystem transactions; keep independent backups.

## Build and package

Requirements: **CMake 3.24+**, **C++20 compiler**, Git, **Node.js 22+** and npm. Node is only a build tool for the manual. CMake downloads native dependencies; the pinned CyDoc archive is included so downstream builds need no access to its private repository.
Windows uses Visual Studio 2022 C++ tools; macOS uses Xcode Command Line Tools. Linux desktop needs GLFW's OpenGL/X11/Wayland development libraries; `libayatana-appindicator3-dev` enables its tray.

```sh
npm ci --prefix .CyDocBuild
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 3
ctest --test-dir build -C Release --output-on-failure
npm test --prefix .CyDocBuild
cmake --install build --config Release --prefix dist
cpack --config build/CPackConfig.cmake -C Release -B dist/packages
```

Use `-DCYSYNCK_DESKTOP=OFF` for a headless server. The scripts `scripts/build.ps1` and `scripts/build.sh` prepare dependencies, build, test and install. Packages contain the complete manual and `CySynck-Documentation.zip`; only the exported reader is distributed, not `.CyDocBuild/`.

Run `dist/bin/cysynck-desktop.exe` on Windows, `dist/bin/cysynck-desktop` on Linux, or `open dist/CySynck.app` on macOS. The Windows archive also includes the per-user `Install-CySynck.ps1` installer. Keep all package directories together when relocating.

```sh
cysynck project add Notes /absolute/path/to/notes --id notes --mode backup --interval 60 --keep 30
cysynck backup notes
cysynck verify notes
cysynck docs quickstart
```

Use an existing folder, quote paths with spaces, inspect the verification report's `ok` field, and restore only to a new separate directory. `--state DIR` selects a private profile. Background scheduling requires an active desktop or `cysynck serve` process. Run `cysynck help` for command syntax and `cysynck doctor --probe true` for explicit tool/connection checks.

## Contributor references

Original publishing, packaging, cross-compilation and milestone records are preserved in [.CyDocBuild/internal/legacy](.CyDocBuild/internal/legacy). Small `docs/*.md` compatibility pointers retain old repository links; they are no longer a shipped help system.
Native CI compiles/tests/packages all targets. The secret-scan workflow checks Git history with Gitleaks. For real two-peer tests, configure `-DCYSYNCK_TEST_SYNCTHING=/path/to/syncthing` before building and running CTest.

## License

CySynck is licensed under [MIT](LICENSE). Dependency notices are listed in [THIRD_PARTY.md](THIRD_PARTY.md) and included in packages. Syncthing, Git and WireGuard retain their own licenses; their source/binaries and CyRevision source are not incorporated into this project.
