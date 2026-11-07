---
id: cli
title: Command-line reference
description: Canonical command syntax, profiles and remote API access.
section: Reference
order: 80
revision: 1
---

# Command-line reference {#cli}

## Run a command {#commands}

The core and CLI use English identifiers. The block below is the application's actual help output.
Replace uppercase placeholders, quote paths with spaces and use the guides for each preview/apply workflow.
The standalone executable and the native command panel share Core commands and the local `docs`/`desktop startup` launchers.
The launchers are not remote Core commands. The desktop Help button also opens the current topic.

```text
CySynck 0.40 (C++20)
cysynck [--state DIR] [--plugin LIBRARY] [--server http://127.0.0.1:PORT] COMMAND
  overview | modes | plugins | events | api-token
  docs [PAGE] [--language en|fr] [--section ID] [--print]  (local offline manual)
  project mode-options ID
  project mode-options-set ID OPTIONS.json --plan TOKEN
  doctor [--probe true]  (test tool versions and engine connection only with --probe)
  service plan --platform linux|macos --executable /absolute/bin/cysynck [--target-state /profile] [--port 8484]
  service export NEW_DIRECTORY --platform ... --executable ... --plan PREVIEW_TOKEN [--target-state ...] [--port ...]
  desktop startup status|enable|disable  (local desktop installation only)
  engine configure --executable PATH [--port 8385] [--listen-port 22001] [--auto-start true]
  engine connect http://127.0.0.1:8384   (key from CYSYNCK_CONNECT_API_KEY)
  engine run | engine status | engine stop | engine log | engine devices
  engine add-device DEVICE_ID [--address tcp://host:port] [--name NAME]
  engine share PROJECT_ID DEVICE_ID
  project list
  project add NAME ROOT [--mode sendreceive|sendonly|receiveonly|backup|git-bundles]
      [--id SHARED_ID] [--exchange-path DIR] [--interval MINUTES] [--keep COUNT] [--storage auto|whole|cdc]
  project set ID [--interval MINUTES] [--keep COUNT] [--storage auto|whole|cdc]
      [--git-lfs true|false] [--lfs-storage ABSOLUTE_DIRECTORY]
  backup ID [--plan TOKEN] | snapshots ID | changes ID [--snapshot ID]
  backup-preview ID [--excludes RULES.json] [--offset N] [--limit N] [--inventory-hash HASH]
  backup-excludes ID RULES.json --plan TOKEN
  diff ID RELATIVE_PATH [--snapshot ID]
  compare ID LEFT_RELATIVE_PATH RIGHT_RELATIVE_PATH
  conflicts ID | conflict-diff ID CONFLICT_RELATIVE_PATH
  resolution-plan ID CONFLICT_PATH --choice current|conflict|merged [--content TEXT]
  resolve-conflict ID CONFLICT_PATH --choice current|conflict|merged --plan TOKEN [--content TEXT]
  resolutions ID | resolution-export ID RESOLUTION_ID NEW_DIRECTORY
  resolution-acknowledge ID RESOLUTION_ID  (mark interrupted recovery as reviewed)
  verify ID [--snapshot SNAPSHOT_ID]
  snapshot-files ID SNAPSHOT_ID [--prefix FOLDER --offset N --limit N --manifest-hash HASH]
  snapshot-compare ID LEFT_SNAPSHOT RIGHT_SNAPSHOT [--prefix FOLDER --offset N --limit N]
  snapshot-compare-file ID LEFT_SNAPSHOT RIGHT_SNAPSHOT PATH [--left-hash HASH --right-hash HASH]
  restore-plan ID SNAPSHOT_ID NEW_DIRECTORY [--paths-file JSON_ARRAY]
  restore ID SNAPSHOT_ID NEW_DIRECTORY [--paths-file JSON_ARRAY --plan TOKEN]
  storage status | storage prune [--apply true --plan PREVIEW_TOKEN]
  register ID | sync ID | status ID | pause ID | resume ID
  git status|export|inbox ID  (export: --lfs true includes Git LFS branch history)
  git import ID PEER_ID BUNDLE_ID
  git setup ID [--branch main --object-format sha1|sha256] [--apply true --plan PREVIEW_TOKEN]
  git integrate ID PEER_ID BRANCH [--lfs-policy reject|pointers] [--apply true --plan PREVIEW_TOKEN]
      [--base COMMIT|unborn --head COMMIT --local-ref REF]  (clean tracked files; unrelated local files preserved)
  git merge-review ID PEER_ID BRANCH  (committed three-way file overview; no application)
  git merge-file ID PEER_ID BRANCH PATH [--base COMMIT --head COMMIT --local-ref REF --merge-base COMMIT]
      [--regions CHOICES.json --region-token TOKEN]  (local, peer or both per conflict region; read-only draft)
  git merge ID PEER_ID BRANCH [--apply true --plan PREVIEW_TOKEN]
      [--base COMMIT --head COMMIT --local-ref REF --resolutions FILE.json]
      (reviewed divergent merge; local/peer files or edited UTF-8 conflict text)
  git integrations ID [--offset 0 --limit 20]
  git lfs-cache ID [--peer PEER --branch BRANCH] [--verify true] [--offset N --limit N --inventory-hash HASH]
  git lfs-recover ID SOURCE_PEER [--peer PEER --branch BRANCH] [--objects IDS.json] [--apply true --plan TOKEN]
  git lfs-recoveries ID [--offset N --limit N] | git lfs-recovery ID RECEIPT
  git materialize ID [--paths-file JSON_ARRAY] [--apply true --plan PREVIEW_TOKEN]  (verified local LFS cache; preserves modified files)
  git materializations ID [--offset 0 --limit 20]
  git materialization-check ID RECEIPT_ID
  git materialization-resume ID RECEIPT_ID [--apply true --plan PREVIEW_TOKEN]
  git materialization-export ID RECEIPT_ID --destination ABSOLUTE_DIR [--paths-file JSON_ARRAY] [--apply true --plan PREVIEW_TOKEN]
  git dehydrate ID [--paths-file JSON_ARRAY] [--apply true --plan PREVIEW_TOKEN]
  git materialization-acknowledge ID RECEIPT_ID --plan CHECK_TOKEN  (preserves Git and recovery files)
  git lfs-recovery-check ID RECEIPT_ID  (verify every planned cache object)
  git lfs-recovery-acknowledge ID RECEIPT_ID --plan CHECK_TOKEN  (mark an interruption as reviewed)
  git integration-check ID RECEIPT_ID
  git integration-acknowledge ID RECEIPT_ID --plan CHECK_TOKEN  (preserve receipt and Git files)
  git extract ID PEER_ID BRANCH NEW_DIRECTORY [--commit FULL_OID --lfs true|false]
      [--apply true --plan PREVIEW_TOKEN --base COMMIT|unborn --head COMMIT --local-ref REF]
  git review ID PEER_ID BRANCH
  git branches ID
  git review-file ID PEER_ID BRANCH RELATIVE_PATH [--base COMMIT|unborn --head COMMIT]
  git history ID PEER_ID BRANCH [--limit 50 --offset 0 --base COMMIT|unborn --head COMMIT]
  git commit ID PEER_ID BRANCH COMMIT [--parent 1]
  git commit-file ID PEER_ID BRANCH COMMIT RELATIVE_PATH [--parent 1 --base COMMIT|unborn --head COMMIT]
  Git review/history/commit commands also accept --local-ref refs/heads/BRANCH|detached.
  git storage ID [--exports ID1,ID2] [--apply true --plan PREVIEW_TOKEN]
  vpn configure NAME 10.77.X.Y [--endpoint HOST:UDP_PORT] [--listen-port 51820] [--mesh-port 8485] [--device-id ID]
  vpn status|preview|write-config|up|down
  vpn publish|discover ID
  vpn trust ID PEER_ID --key EXPECTED_PUBLIC_KEY
  vpn revoke ID PEER_ID
  chat send ID PEER_ID TEXT | chat history ID | chat serve
  send-file ID PEER_ID RELATIVE_PATH [--legacy true]
  transfers list ID | transfers resume ID TRANSFER_ID
  transfers cancel ID TRANSFER_ID --direction in|out
  transfers limits
  transfers configure [--file-limit-mib 1024] [--inbox-quota-mib 4096] [--outbox-quota-mib 4096]
  received list ID | received verify ID TRANSFER_ID
  received export ID TRANSFER_ID NEW_DIRECTORY
  serve [--host 127.0.0.1] [--port 8484]
Desktop: cysynck-desktop [--state DIR] [--plugin LIBRARY] [--background|--foreground] [--no-tray]
Secrets: CYSYNCK_SYNCTHING_API_KEY, CYSYNCK_API_TOKEN, CYSYNCK_MESH_TOKEN.
VPN up/down requires administrator/root. No automatic NAT traversal.
```

## Profiles and remote access {#profiles}

`--state DIR` selects a private profile. Defaults: Windows `%LOCALAPPDATA%/CySynck`;
macOS `~/Library/Application Support/CySynck`; Linux `$XDG_STATE_HOME/cysynck` or `~/.local/state/cysynck`.
Keep the profile outside synchronized project/exchange folders. Scheduled backups require an active desktop or normal `serve` process;
`--interval 0` disables the schedule and pausing suspends it.

```sh
cysynck --state /absolute/profile serve --host 127.0.0.1 --port 8484
cysynck --state /absolute/profile api-token
cysynck --server http://127.0.0.1:8484 project list
```

Use an SSH tunnel for a remote server. Set `CYSYNCK_API_TOKEN` in the client environment from the server's private token.
Never put it in synchronized files. Engine setup and tunnel activation remain local-only.
JSON reports can contain `ok:false`, `can_integrate:false` or `delivered:false` without a transport error;
scripts must inspect the operation-specific fields. See [API contracts](doc:architecture) and [offline help](doc:help).
