---
id: services
title: Prepare a headless user service
description: Usage, commands and limits for Prepare a headless user service.
section: Operations
order: 60
revision: 1
---

# Prepare a headless user service {#prepare-a-headless-user-service}

The desktop and Web **Services** views prepare the same configurations as the CLI and API.
Changing an input invalidates the displayed preview. Export destinations refer to the machine
running the core/server, rather than the browser's local filesystem.

CySynck can prepare a systemd user unit for Linux or a LaunchAgent for macOS. Preparation
and export are available on any host, including Windows when staging a Linux installation.
They do not install, enable or start services. Windows desktop login registration remains
available through `desktop startup`.

Start with an installed CLI and choose its absolute path and state directory **on the target
machine**. Keep the CLI's `bin`, `lib`, `share`, `Documentation/` and offline ZIP together. Configure and test that profile
there with `doctor --probe true`; enable managed Syncthing auto-start if it should run with
the service. Allocate a different API port to each running profile.

```sh
cysynck service plan --platform linux --executable /opt/cysynck/bin/cysynck \
  --target-state /srv/cysynck/team --port 8484
cysynck service export /tmp/team-service --platform linux \
  --executable /opt/cysynck/bin/cysynck --target-state /srv/cysynck/team \
  --port 8484 --plan TOKEN_FROM_THE_PREVIEW
```

Replace `linux` with `macos` for a LaunchAgent. On a POSIX host, omitting `--target-state`
uses the CLI's selected `--state` profile. On Windows, explicitly give the target POSIX
state path. Target paths must be absolute, without dot components or control characters.
Systemd also disallows quotes, backslashes and glob characters in the executable path;
these are rejected before preview. Spaces and literal percent/dollar characters are supported.
The API port must be an integer from 1024 through 65535. Binding stays on `127.0.0.1`;
use an SSH tunnel for remote administration.

The preview contains the exact service text, launch arguments, filename and instructions.
Its token covers the target platform, paths, port and generated content. Changing those
values requires a new preview. The stable filename identifies the target state profile;
changing the executable or port updates that same service identity.

Export claims a new local directory with an existing parent, outside all active projects,
exchange directories and the current state profile. It writes the reviewed service,
`README.md` with target-specific install/removal commands, and `service-export.json` with
SHA-256 hashes. Existing directories are never overwritten. Partial output is preserved if
a disk error interrupts export. Neither keys nor backup/project data are exported.

Read the generated README on the target before executing its commands. Linux uses the
user's systemd manager; operation after logout depends on the administrator's lingering
policy. macOS LaunchAgents run in the logged-in user's session. Desktop and headless server
should not run the same profile simultaneously; stop the existing owner first.

Systemd uses journal logging and stops the service's process group, including its managed
engine. The LaunchAgent writes stdout/stderr logs under the target state directory, which
the generated instructions create before bootstrap. Rotate those logs as needed. Both
formats restart on failure and restrict newly created files with a private umask.

API: `POST /api/service/plan` and `POST /api/service/export`. Both accept `platform`,
`executable`, optional `target_state` and `port`. Export also needs `plan` and a local
`destination`. The C ABI exposes these through the generic request function.

Activation under native systemd/launchd remains to be tested on those operating systems.
Format references: [systemd command lines](https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html#Command%20lines),
[systemd quoting](https://www.freedesktop.org/software/systemd/man/latest/systemd.syntax.html#Quoting),
and [Apple's LaunchAgent guide](https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPSystemStartup/Chapters/CreatingLaunchdJobs.html).
