---
id: install
title: Install CySynck
description: Install, update or remove the Windows package; run portable Linux and macOS builds.
section: Start
order: 2
parent: overview
revision: 3
---

# Windows installation {#installation-windows}

The package includes the CLI, desktop, SDK, offline manual and `Install-CySynck.ps1`.
The application/core use C++20; PowerShell only handles deployment. There is no signed EXE/MSI installer or automatic update download.
Windows Installed Apps registration is not provided; the Start menu includes an uninstall shortcut.
Install Syncthing, Git, WireGuard and OpenSSL separately as needed. OpenSSL supplies P-256 signatures for CyRevision and VPN endpoints; `CYSYNCK_OPENSSL` can select its executable. Linux/macOS packages can be extracted and run directly:
the CLI is in `bin/`; macOS also includes `CySynck.app`. Keep the entire package together, including libraries and `Documentation/`.
On Linux servers use a headless build and [service export](doc:services). Building from source is covered in the repository's contributor instructions.

## Install {#installer}

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-CySynck.ps1
```

Extract the full archive to a temporary directory and run the script from its root.
The default target is `%LOCALAPPDATA%\Programs\CySynck`. User Start-menu shortcuts **CySynck** and **Uninstall CySynck** are added without administrator access or PATH changes.
Use `-Destination 'D:\Applications\CySynck'` for another target. A first installation requires an empty destination separate from the extracted source.
Installation leaves login startup unchanged. Enable it explicitly in **Application → Enable login startup** for the chosen profile.

## Update {#mettre-a-jour}

Quit CySynck and applications using its SDK. Extract the new package, then run its script against the same destination.
Tracked files are SHA-256 checked before replacement. Modified or locked files stop the update.
Previous copies are retained during replacement for recovery on error. If recovery fails, the backup directory is preserved with its path in a warning.
Untracked personal files stay intact; a new package file colliding with an untracked file blocks the update.

## Portable Linux and macOS updates {#portable-updates}

Updates on Linux/macOS are manual. Stop the desktop, embedded SDK hosts and any
CySynck service using the old binaries. Extract the complete new package into a
new version directory, keeping its libraries, SDK resources, licenses and local
manual together. On macOS preserve the complete `CySynck.app` bundle as well.
Start the new executable with the same separate `--state` profile; changing the
application directory does not migrate or erase that profile.

If a service or login entry points to the old executable, regenerate it for the
new absolute executable path before restarting it. See [service exports](doc:services)
and [desktop startup](doc:desktop). Keep the prior application directory until the
new CLI, desktop, engine connection and local help have been checked on the target.
An application-file rollback does not roll back the private profile or project
data: use a core version supporting their stored formats, and keep independent
recovery copies before a format-changing upgrade. This procedure does not add an
automatic downloader or require a web server for the local manual.

## Uninstall {#desinstaller}

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\Programs\CySynck\Install-CySynck.ps1" -Action Uninstall
```

Use the uninstall shortcut or command. For a custom installation, repeat its `-Destination`.
The script removes startup shortcuts pointing to this installation and its own Start-menu shortcuts.
Unchanged package files are removed individually. Modified/untracked files remain, and the installation directory disappears only when empty; no recursive directory deletion is performed.
Profiles, backups, identities and received data stay intact. `%LOCALAPPDATA%\CySynck` is the usual separate state profile and cannot be an installation target.
The script does not alter Syncthing, Git or WireGuard installations.

## Options {#options}

`-Action Status` describes an installation. `-NoShortcuts` skips Start-menu shortcuts.
`-Portable` skips those shortcuts and login-entry cleanup during uninstall; retain the same mode for updates.
Portable mode does not move state data: use `--state` to select a profile for a portable application.
