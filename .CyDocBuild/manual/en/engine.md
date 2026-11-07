---
id: engine
title: Connect Syncthing
description: Supervise an isolated engine or connect an existing local instance and share projects.
section: Start
parent: overview
order: 5
revision: 1
---

# Connect Syncthing {#engine}

Install Syncthing separately; the executable is not included. CySynck can supervise its own isolated instance or connect an existing local one.

## Managed instance {#managed}

In **Engine**, select **Managed instance**, enter the executable path, save and start it.
CySynck creates its own identity, API key and configuration under the private profile, without taking over another instance.
Enable **Start with CySynck** for automatic managed startup with the desktop or service.

```sh
cysynck engine configure --executable /opt/syncthing/syncthing --port 8385 --listen-port 22001 --auto-start true
cysynck engine run
```

`engine run` stays in the foreground; Ctrl+C stops its child. Desktop/service supervise their own child.
A crash triggers a delayed restart attempt. Ownership locks prevent competing CySynck controllers and only the owned child is stopped.
Use different ports for independent engines. The supervisor probes supported command flags to handle Syncthing 1.x/2.x CLI differences.

## Existing local instance {#existing}

Enter the instance's loopback URL and REST key in the local desktop, or supply its key through the client environment:

```sh
cysynck engine connect http://127.0.0.1:8384
cysynck engine status
```

The local CLI reads `CYSYNCK_CONNECT_API_KEY`. Keep the key private; status routes do not expose it.
Executable/connection changes require the local CLI/desktop and are refused through HTTP, including `/api/cli`.
Connecting does not transfer ownership of the external process.

## Authorize and share {#share}

Use the **same project ID** on all devices. Add devices on both sides, register the project and share its folder with the authorized peer.

```sh
cysynck engine add-device DEVICE_ID --name Laptop --address dynamic
cysynck register PROJECT_ID
cysynck engine share PROJECT_ID DEVICE_ID
cysynck sync PROJECT_ID
```

Only explicitly registered `cysynck-<project_id>` folders are managed on an external engine.
Registration creates folders paused while required ignores are installed, then applies the project's pause setting.
Registration alone authorizes no peer. The original Syncthing Web interface remains accessible from **Engine**.
An active `.git` directory is refused for ordinary file synchronization; use [Git exchange](doc:git-lfs).
Backup exclusions and Syncthing transfer ignores are independent. A manual `sync` requests a scan; Syncthing transfers continuously afterward.
A backup before a scan does not cover every incoming transfer. Syncthing versioning protects remotely replaced files; scheduled snapshots also capture local edits.

## Inspect failures {#diagnostics}

Use `engine status`, `engine devices`, `engine log`, or **Diagnostics** with explicit probes.
Managed logs are rotated; an unavailable engine blocks operations needing its exact folder identity or pause state.
Keep the full profile private and avoid editing engine configuration concurrently with CySynck requests.
