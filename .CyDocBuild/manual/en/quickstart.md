---
id: quickstart
title: Your first project
description: Create and verify a local backup, then connect synchronization.
section: Start
order: 1
parent: overview
revision: 1
---

# Your first project {#your-first-project}

## Create a backup project {#create-a-backup-project}

Start with a small ordinary folder. Keep the state profile outside every project and exchange directory.
The profile contains private tokens, backups and received files. A profile on the same disk is not an independent disaster-recovery copy.

```sh
cysynck project add Notes /absolute/path/to/notes --id notes --mode backup
cysynck project list
cysynck backup notes
cysynck snapshots notes
cysynck verify notes
```

Replace the example path with an existing folder; quote paths containing spaces. On Windows use a path such as `D:\Projects\Notes`.
Read `ok` in the verification response. An exit code of zero can accompany an integrity report with `ok:false`.
In the desktop choose **New project**, select the same folder and backup mode, then open **Backups**.

## Restore a separate copy {#restore-a-separate-copy}

Copy a snapshot ID from the list. The destination must be absent and outside all projects, exchange folders and the state profile.

```sh
cysynck restore-plan notes SNAPSHOT_ID /absolute/new/restored-notes
cysynck restore notes SNAPSHOT_ID /absolute/new/restored-notes
```

The preview does not create the destination. Restore verifies content again and creates a new directory.
Inspect the result before using it. A copy interrupted by a disk error may remain partial; the source project is preserved.

## Add synchronization {#add-synchronization}

Install Syncthing separately. Use **Engine** in the desktop to select a managed executable or connect an existing local instance.
The local CLI supports `engine configure` and `engine connect`; use [the CLI reference](doc:cli) for their exact options.
For an existing instance, supply its private REST key through `CYSYNCK_CONNECT_API_KEY`, rather than an argument saved in shell history.

Create a synchronization project with `sendreceive`, `sendonly` or `receiveonly`, register it, then authorize devices and share the folder on both devices.
Registration alone does not authorize remote devices. The original Syncthing Web interface remains accessible for advanced engine settings.

```sh
cysynck project add Shared /absolute/path/to/shared --id shared --mode sendreceive
cysynck register shared
cysynck engine devices
cysynck sync shared
```

Never synchronize an active `.git` directory. Choose [Git exchange mode](doc:git-lfs) for a repository.
Use [backup exclusions](doc:backup-scope) and [storage policies](doc:block-backups) for large projects.
