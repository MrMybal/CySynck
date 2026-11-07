# Native CI compatibility corrections

The first native build run after publishing MIT sources compiled the Windows, macOS,
Linux desktop and Linux server targets. The Linux jobs exposed the following runtime
and fixture assumptions; these corrections require another native CI run.

The [published 0.39/MIT run](https://github.com/MrMybal/CySynck/actions/runs/42145518437)
finished with these results; its separate secret scan succeeded:

| Job | Compilation | Tests passed | Failures |
| --- | --- | --- | --- |
| Windows | Passed | 34/36 | Two LFS fixtures |
| macOS | Passed | 33/36 | Edited-merge permissions and two LFS fixtures |
| Linux desktop | Passed | 32/36 | Same Git fixtures and GIO startup |
| Linux server | Passed | 32/36 | Same Git fixtures and Syncthing 1.x startup |

The corrections described below are local development work after that push. This table
reports the original run, rather than a successful native rerun of the corrections.

## Syncthing command options

Linux distribution packages can provide Syncthing 1.x, whose port-probing and logging
options differ from 2.x. Before starting a supervised engine, CySynck inspects
`serve --help` and, for a new profile, `generate --help`. It selects the advertised
`--skip-port-probing`/`--no-port-probing` and `--logfile`/`--log-file` spellings, without
guessing from a version string or retrying a failed generation against the same profile.
Unrecognized help or unsupported option names fail before engine launch.

References: [1.x CLI](https://docs.syncthing.net/v1.27.2/users/syncthing.html),
[current CLI](https://docs.syncthing.net/users/syncthing.html).

## Git fixtures

The materialization fixtures deliberately configure failing LFS helpers to detect accidental
execution by CySynck. Their own `update-index` commands now disable those helpers for that
command only: Git can refresh working-file metadata while changing index flags. The fixture
configuration retains the failing helpers during every product operation.

The edited-merge fixture now sets the working file's executable bits when committing an
executable index entry on POSIX, so its following branch checkout sees consistent metadata.

The real-engine fixture allows up to 60 seconds for a conflict copy after reconnecting
paused peers. Its other polling deadlines stay at 25 seconds, and timeout diagnostics
retain the last underlying exception. A local 2.x run observed the conflict only after
the previous 25-second wait had expired; the copy and both versions remained intact.

## Linux login startup

GIO checks the first executable before expanding `%%` escapes. A desktop entry whose
executable path contains a percent sign therefore uses `/usr/bin/env --` as its literal
launcher, followed by the escaped executable and arguments. No shell is involved. Paths
without a percent sign keep the direct launcher. The existing GIO fixture exercises the
actual launch and argument round trip when built on a native Linux desktop host.

References: [desktop-entry Exec specification](https://specifications.freedesktop.org/desktop-entry/latest/exec-variables.html),
[GIO desktop entry loading](https://github.com/GNOME/glib/blob/main/gio/gdesktopappinfo.c).

Windows runtime checks and a headless Linux cross-build support local validation. They do not
replace native Linux/macOS execution of these corrections or the full extracted-package jobs.
