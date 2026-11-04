# Diagnostics

`cysynck doctor` inspects the selected state profile, available tools, project directories,
Git mode and pending recovery. Missing tools are errors when a configured feature needs
them; otherwise they are optional warnings. Backup-only projects do not require Syncthing.
An external Syncthing connection does not require a local Syncthing executable.

The default inspection does not execute tools, contact Syncthing, start an engine, change
projects or activate a VPN. Run `cysynck doctor --probe true` to explicitly test Git,
Syncthing and WireGuard tool versions and the configured Syncthing API. Version probes
are limited to five seconds each and the connection test uses a short timeout. Activation
tools are located without executing them. Reports omit keys and raw process output.

The same report is available in **Diagnostics** on the CyUI desktop and in the Web console,
with English and French labels. Core messages and CLI output remain in English.

API: `GET /api/doctor` for inspection; `POST /api/doctor` with `probe_tools` and/or
`probe_engine` booleans for explicit tests. GET ignores probe flags even if given a body.
The JSON report includes `ready`, error/warning counts and checks with stable IDs. A true
`ready` means the performed checks found no blocking error; it does not certify network
reachability, backup integrity or VPN traversal. Probe options are reported separately.
