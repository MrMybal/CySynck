---
id: help
title: Using offline help
description: Open contextual help, search and retain your reading position without a server.
section: Start
order: 5
parent: overview
revision: 1
---

# Using offline help {#using-offline-help}

## Open the manual {#open-the-manual}

The complete `Documentation/` directory accompanies the application. Open `Documentation.html` directly with a modern browser.
Keep `DocData/` beside it. No Node.js, web server, Internet connection or CDN is needed to read it.
The desktop **Help / F1** action opens the topic for the current view in the selected interface language.

```sh
cysynck docs
cysynck docs git-materialization --language fr
cysynck docs backups --section verifier-et-restaurer --print
```

`--print` returns the escaped local URL without launching a browser, useful on headless servers.
`CYSYNCK_DOC_ROOT` can select an existing complete documentation directory when the normal installed location is unavailable.
The terminal command `cysynck help` continues to display command syntax.

## Web console and remote servers {#web-console-and-remote-servers}

The Web console's help dialog offers an offline archive and the local topic URL.
On a local desktop, **Open on server computer** asks the loopback server to launch its installed local manual.
A remote server cannot launch a browser on your workstation; download and extract the entire archive, then open `Documentation.html` locally.
Paste the supplied route after its filename to reach the same topic and section. Browsers normally block direct `http://` to `file://` navigation.
The documentation is not hosted by the API server. The authenticated archive endpoint only transfers a complete distribution for local extraction.

## Navigate and read {#navigate-and-read}

Use the sidebar, page contents and heading links. **Ctrl+K** opens search; arrows and Enter select a result, Escape closes it.
The narrow layout has a navigation menu. Keyboard focus, touch scrolling, high-contrast and light themes are available in CyDoc.
The initial dark theme matches the desktop; the reader remembers the chosen theme.
The English/French selector keeps the shared page and section IDs. An unsupported locale resolves to complete English.
No incomplete translations are offered. Paths and API literals remain unchanged between languages.

CyDoc stores bookmarks, notes, navigation state and reading positions in the browser's local storage, under `cysynck-help`.
File-origin storage may be restricted by browser policy; moving the manual or changing browsers can create a separate storage area.
Use bookmarks or copied heading URLs when you need a portable reference. Diagrams are static SVG, and this edition has no optional media readers.
