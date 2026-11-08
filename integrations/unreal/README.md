# CySynck Unreal bridge 0.2.0

[Français](README.fr.md)

The runtime plugin embeds the C++ core through C ABI version 1. It does not share STL
types or allocators with Unreal and loads the platform library from its own
`ThirdParty/CySynck/` directory. The application SDK and plugin keep their MIT license;
the installed Unreal Engine retains its own terms.

## Install into a project

Build/install CySynck for the host's target OS and architecture first. From this directory:

```sh
cmake -DCYSYNCK_PREFIX=/absolute/installed/cysynck \
  -DUNREAL_PLUGIN_DIR=/absolute/YourProject/Plugins/CySynck \
  -P StageCySynck.cmake
```

The destination must not exist. The script copies the source plugin, matching native
core, SDK headers, runtime libraries and notices. It also copies the complete offline
manual when present. It refuses an existing destination to preserve project modifications;
stage an update into a new directory and review it before replacing your existing plugin.
The script is also distributed at `share/cysynck/integrations/unreal/` in SDK packages.

Enable the **CySynck** plugin and compile the project. Open the copied manual at
`ThirdParty/CySynck/Documentation/Documentation.html#/en/architecture/autres-applications-c-et-unreal`.
Keep its `DocData/` resources together. Reading uses local files without a web server.
Runtime dependencies also stage the native/CRT libraries, notices, licenses and complete
manual as loose `NonUFS` files in a cooked game. `POST /api/help/location` returns a
validated page/section URL relative to the SDK library without opening a browser or
changing the host's environment. See the SDK manual's integration page.

## Blueprint workflow

1. Call **CySynck → Open** on the game thread with an explicit absolute state directory.
   Use a dedicated profile rather than a profile already open in another application.
2. Use **CySynck Request Async** for requests such as `GET /api/overview` with body `{}`.
   Its **Completed** event returns `Success` and `ResultJson` on the game thread.
3. Inspect returned JSON, including preview/plan tokens. Submit writes only after reviewing
   the result, using the same JSON API contracts as the CLI and standalone application.
4. If the host needs scheduled backups, engine supervision or opted-in VPN maintenance,
   submit `POST /api/maintenance/tick` with `{}` through the async node about every 15 seconds.
   The bridge's ticker dispatches completions; the host schedules maintenance work.
5. Call **Close** when finished. An active request completes before its native handle closes;
   queued requests that have not started receive an explicit failure without executing.

Requests run serially, in FIFO order, on a worker thread. The queue accepts at most 64
outstanding requests, including the active request; excess requests fail explicitly.
The asynchronous proxy stays alive until completion, then releases its game-instance/root
reference. If its supplied context object has expired, no Blueprint event is broadcast.

The original synchronous **Request** remains available for deliberate blocking calls.
Avoid expensive operations in a game-thread tick. **Close** can block while the current
operation finishes; it does not interrupt an applied backup or Git operation. Module
shutdown removes the ticker, drains the worker and releases the library in that order.
Dynamic module reload is disabled while using this native bridge.

## Build and verify

On Windows, the repository helper stages an isolated test project, compiles it with an
already installed engine and runs the plugin's automation tests using NullRHI:

```powershell
./scripts/test-unreal.ps1 -EngineRoot 'C:/UnrealEngine/UE_5.7' `
  -SdkPrefix 'D:/CySynck/dist' -WorkDirectory 'D:/CySynck/ue-smoke'
```

Run that command from the CySynck repository root. The work directory must be new.
The helper forbids engine source changes and retains build logs and the JSON automation
report. Tests cover the real C ABI, Unicode profiles, asynchronous FIFO callbacks, proxy
lifetime, queue backpressure and closing with queued/active work.

Builds and five automation tests pass with **Unreal Engine 5.7.4 and 5.8.3, Windows x64, Development Editor**.
The full **5.8.3 Win64 Development packaged game** also passes five tests after cook,
staging and archive, with 34 native/runtime/license/manual files checked by SHA-256.
Reproduce the complete Editor/game run from the repository root:

```powershell
./scripts/test-unreal-packaged.ps1 -EngineRoot 'C:/UnrealEngine/UE_5.8' `
  -SdkPrefix 'D:/CySynck/dist' -WorkDirectory 'D:/CySynck/ue-packaged-smoke'
```

The work directory must be new. Logs and DDC stay under the test directory; the test
uses a filesystem cook without Zen storage and runs the game hidden with a deadline.
NullRHI validates the native bridge and resource staging; it does not validate GPU rendering.
Linux/macOS Unreal hosts, other engine versions and Shipping builds
still require their own engine builds/runtime checks. The standalone native CI checks
that SDK packages contain a stageable plugin; it does not compile Unreal itself.
