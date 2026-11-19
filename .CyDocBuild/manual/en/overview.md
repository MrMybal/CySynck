---
id: overview
title: CySynck
description: Synchronize projects, preserve backups and review changes.
section: Start
order: 0
revision: 2
---

# CySynck {#cysynck}

CySynck groups synchronization, versioned backups, file comparisons and explicit Git exchanges by project.
The C++20 core is shared by the command line, the CyUI desktop and the Web/API console.
The desktop targets Windows, macOS and Linux; Linux servers can use the CLI without a graphical environment. The current development preview supplies a validated Windows x64 binary; see [availability](doc:availability) for the scope of other platform checks.
The [native assistant](doc:assistant) shares 130 operations with the JSON CLI and authenticated local MCP, including project, engine, network and desktop actions.

## Start here {#start-here}

1. [Install the application](doc:install) and the external tools you need.
2. Follow the [first-project tutorial](doc:quickstart).
3. Choose [a project mode](doc:projects) before sharing its folder.
4. Learn [backup and restore](doc:backups), [file review](doc:diffs) or [Git exchange](doc:git-setup).
5. Set up the [assistant and MCP](doc:assistant) when you want conversational or CLI automation.

## One core, several interfaces {#one-core-several-interfaces}

```diagram
{"type":"flow","title":"CySynck interfaces and external tools","nodes":[{"id":"ui","label":"CyUI desktop / CLI / Web API","x":0,"y":0},{"id":"core","label":"C++20 shared core","x":300,"y":0},{"id":"tools","label":"Syncthing / Git / WireGuard","x":600,"y":0}],"edges":[{"from":"ui","to":"core"},{"from":"core","to":"tools"}]}
```

Syncthing provides file synchronization. Git provides repository operations; each contributor keeps a local repository.
WireGuard provides the optional VPN tunnel. These tools are installed separately. CySynck does not replace their network protocols.
The original Syncthing Web interface remains available from the Engine view.

## Language, help and license {#language-help-and-license}

English is the default language; French is the second complete language. The engine, API keys and CLI identifiers use English.
The desktop and Web language controls translate their interface. The manual has its own language selector and keeps the current section when switching.
Use **Help / F1** in the desktop or `cysynck docs` to open this local manual. See [offline help](doc:help) for server use.

CySynck is licensed under MIT. Dependency notices accompany the application and this manual.
See [current availability and limits](doc:availability) before using a workflow on valuable data.
External reference links are optional and need Internet access; the manual itself does not.
