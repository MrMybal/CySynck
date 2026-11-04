# Dependencies and references

Dependency licenses are installed under dist/share/cysynck/licenses.

| Component | CMake reference | License |
| --- | --- | --- |
| CyUI | `a228501d9418550ce68d2d67b70cebbb8fa3ee2a` | MIT |
| Dear ImGui (docking) | `1ab953d75a760d5102eb2d5c4c6c4e65df6b29f3` | MIT |
| GLFW | `3.4` | zlib/libpng |
| nlohmann/json | `v3.12.0` | MIT |
| cpp-httplib | `5814e121dfb5049f72a5c3956c3c8961b40da78b` | MIT |
| PicoSHA2 | `161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29` | MIT |
| TinyXML2 | `321ea883b7190d4e85cae5512a12e5eaa8f8731f` (10.0.0) | zlib |

CyUI also uses Lexilla/Scintilla in its Code module; their licenses accompany the desktop distribution. Redistributed MSVC runtime files are subject to the Microsoft redistribution terms associated with the compiler installation.

The desktop uses Shell32/User32/GDI32 on Windows and AppKit on macOS. On Linux, tray support optionally links to system GTK3 (LGPL 2.1+) and Ayatana AppIndicator (LGPL 2.1 or 3) libraries. Those libraries are not included in the Windows archive. The headless server does not depend on these graphical libraries.

Syncthing, Git and WireGuard are external programs installed separately. CySynck uses their APIs and command-line tools and does not redistribute their binaries in this version. CyRevision and SyncTrayzor are functional references; their source files are not incorporated into this project.

Technical references:

- [CyUI integration guide](https://github.com/MrMybal/CyUI/blob/main/docs/AI_GUIDE.md)
- [Syncthing REST configuration](https://docs.syncthing.net/rest/config.html)
- [Syncthing versioning](https://docs.syncthing.net/users/versioning.html)
- [Syncthing connections](https://docs.syncthing.net/rest/system-connections-get.html)
- [Official WireGuard guide](https://www.wireguard.com/quickstart/)
- [WireGuard limitations](https://www.wireguard.com/known-limitations/)
