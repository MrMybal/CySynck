# CySynck 0.41.0

[English manual](Documentation/Documentation.html#/en/overview) · [Manuel français](Documentation/Documentation.html#/fr/overview)

This development preview includes the native Assistant and 130 shared CLI/MCP operations.
Open **Assistant** to connect a provider. CyMCPSDK 0.7.2 defaults to **Free** local MCP
access: when the listener is enabled, its address alone authorizes registered operations.
Arguments remain validated; explicit profile choices are preserved. Local controlled,
Token only and Protected remain available. Keep agent/API credentials separate.
The local guide is `Documentation/Documentation.html#/en/assistant`.
On Windows start `bin/cysynck-desktop.exe`; `--mcp-port 0` exposes its
loopback endpoint for the CLI. The package also contains `Install-CySynck.ps1`.

Open `Documentation/Documentation.html` in a recent browser. Keep the whole `Documentation/`
directory together, including `DocData/`. Reading needs no server, CDN, Node.js or Internet.

In the desktop, **Help (F1)** opens the current topic. From the CLI:

```sh
cysynck docs
cysynck docs quickstart --language fr
cysynck docs backups --section verifier-et-restaurer --print
```

`--print` returns the local URL without opening a browser, useful on a headless server.
For another workstation, copy and extract the complete `CySynck-Documentation.zip`, then open
its `Documentation.html`. The Web console's contextual Help also offers this archive.

The C++20 core, desktop, CLI, Web assets and SDK examples use the MIT license. License notices
accompany the package and the manual. Install Syncthing, Git and WireGuard separately when needed.
Start with the manual's installation, first-project and availability pages before using valuable data.
