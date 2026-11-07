---
id: projects
title: Projects and modes
description: Choose synchronization, backups, Git exchange or a trusted plugin mode.
section: Start
order: 4
parent: overview
revision: 1
---

# Projects and modes {#projects-and-modes}

## Choose a mode {#choose-a-mode}

| Mode | Purpose |
| --- | --- |
| `sendreceive` | Syncthing folder accepting and sending changes |
| `sendonly` | Syncthing send-only folder |
| `receiveonly` | Syncthing receive-only folder; local changes follow Syncthing's receive-only rules |
| `backup` | Local snapshots without a Syncthing folder |
| `git-bundles` | Synchronize a separate exchange directory containing Git bundles and optional LFS objects |
| Plugin mode ID | Run a mode supplied by an explicitly loaded trusted native extension |

Each project has a stable ID, a display name and an absolute root. A Git exchange directory must remain separate from the repository and profile.
Changing a display name does not change the stable ID used by API paths. Use independent `--state` profiles for separate accounts or environments.

## Configure backups {#configure-backups}

Project settings include `backup_before_sync`, the interval `backup_interval_minutes`, retained manifest count `backup_keep`,
`backup_storage`, and backup exclusions. The scheduler runs while the desktop or normal API server is active.
A one-shot CLI command does not leave a scheduler running after exit. Paused projects do not run synchronization actions.
Retention removes old manifests after a successful snapshot; unused content requires [explicit cleanup](doc:backups).

```sh
cysynck project set PROJECT_ID --interval 30 --keep 20
cysynck project set PROJECT_ID --storage auto
cysynck pause PROJECT_ID
cysynck resume PROJECT_ID
```

## Review and settings {#review-and-settings}

Select a project in the sidebar before opening Changes, Conflicts, Backups, Git exchange or Settings.
Global views such as Storage, Diagnostics and Plugins apply to the profile. An engine folder registration can be required before a synchronization action.
Configure [plugin options](doc:plugins), [Git LFS policies](doc:git-lfs) and [backup scope](doc:backup-scope) through their dedicated views and previews.
Use [the CLI command list](doc:cli) for supported flags rather than editing the registry by hand.
