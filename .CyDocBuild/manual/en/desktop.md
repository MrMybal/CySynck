---
id: desktop
title: CyUI desktop
description: Use the native desktop, tray, background session and login startup.
section: Start
order: 3
parent: overview
revision: 1
---

# Desktop application {#application-desktop}

The **Network → Open received files** view lists direct transfers stored privately, checks SHA-256 and exports to a new separate folder.
The catalog survives chat-history retention. See [received files](doc:inbox) for limits and older profiles.
The C++20 interface/session uses the same core as the CLI. The macOS status menu uses a small Objective-C++ AppKit bridge isolated from the core, server and Unreal SDK.
Use **Help (F1)** for the current view; the manual opens in the interface language.

## Differences and conflicts {#differences-et-conflits}

**Changes** compares a **Snapshot** or **Two project files**. For two files enter relative paths and compare.
The view offers statistics, side-by-side display, **Copy patch** and file metadata; turn off side-by-side for one column.
**Conflicts** scans on entry and supports refresh. Selecting a copy shows the current original on the left and Syncthing copy on the right.
An absent original is an addition. Device names are short IDs; displayed dates come from filenames without timezone conversion.
Diagnostics collect unrecognized/inaccessible copies.

Review preserves both files. The patch changes left content into right content.
**Resolve conflict** offers current, conflict or manually merged text. The manual editor uses UTF-8/LF.
Prepare the choice, pause the project and apply the still-current preview. Changes since comparison/preparation are refused.
The application then opens **Recovery**, also available from conflict resolution history.
Export both versions to a separate new directory. The project remains paused; resume explicitly after review.
An interruption is visible in Overview and links to history. **Mark recovery as reviewed** releases the diagnostic for another resolution without deleting saved copies.
See [diff formats](doc:diffs) and [recovery limits](doc:resolutions).

## Integrity and storage {#integrite-et-stockage}

In **Backups**, select a snapshot and verify its SHA-256 objects. Enter a separate destination and prepare restoration to inspect paths and sizes.
Restore is enabled after a valid preview; the core checks again during application. Changing the snapshot/destination requires another preview.
Global **Storage** inspects usage, versions, missing objects and reclaimable bytes. Apply cleanup only while the catalog still matches.
Other backups/operations invalidate the plan. Unreadable manifests block cleanup; unknown files remain.
These core operations run off the window's rendering thread. See [backup commands](doc:backups).

## Close, hide and quit {#fermer-masquer-et-quitter}

The **Application** view stores `ui_close_to_tray` (default true) and `ui_start_hidden` (default false) per profile.
The window hides only while a tray/status icon can reopen it. If the backend detects loss of that access, the window reappears.
Without a tray, the close button exits. **Quit CySynck**, the Application button and Ctrl+Q (Cmd+Q with macOS focus) fully exit.
An active operation finishes before the core and owned engine are released.
Hiding keeps synchronization, scheduled backups and menu actions running but pauses OpenGL rendering.
Presentation waits 100 ms between hidden events; the scheduler uses its own thread.
**Back up all projects** processes active projects, retains per-project failures and continues others.
Results appear in Application; successful snapshots appear in Backups and Activity.

## One session per profile {#session-unique-par-profil}

`desktop.lock` remains held until the core stops. A second launch requests the owner's existing window instead of creating another.
Random private `desktop-requests/` files carry single-use `show` requests; unknown actions and requests older than 60 seconds are ignored.
This channel accepts no CLI commands, file-open paths or shutdown instructions and exposes no additional network port.
Different `--state` profiles can run independent desktops. The CLI server does not take the desktop lock and retains its own engine ownership rules.

## Platforms {#plateformes}

Windows uses `Shell_NotifyIconW`, a native menu and CyUI accent icon; Explorer restart restores the icon. The GUI executable opens no extra console.
macOS uses `NSStatusBar`/AppKit. Linux optionally uses GTK3/Ayatana AppIndicator detected at build time and waits for a StatusNotifier panel before hiding.
Without libraries/panel the window stays visible; some GNOME environments need indicator support. Target-machine tray behavior still needs native validation.
Linux servers build with `CYSYNCK_DESKTOP=OFF` and no graphical dependencies.

Starting hidden does not register login startup. The explicit **Login startup** section registers the current profile and arguments for the next graphical session.
Status describes registration, not a promise that OS policy will run it. Windows uses a user Startup shortcut;
Linux uses `$XDG_CONFIG_HOME/autostart` or `~/.config/autostart`; macOS uses an Aqua-only `~/Library/LaunchAgents` entry without automatic restart after Quit.
Disabling affects later logins and does not kill the current app. No system service is created.

`cysynck desktop startup status|enable|disable` performs the same local-only actions; Web refuses them.
On Windows, `desktop startup cleanup` removes profile registrations pointing to this exact installation without removing data; uninstall uses it.
Other installations stay independent. Paths are absolute and explicitly loaded plugins are retained, but environment secrets are not saved.
After moving the installation or changing plugins, update the registration. Foreign files/links occupying the name are preserved.
Capture mode `--capture` renders offscreen without a tray icon.
