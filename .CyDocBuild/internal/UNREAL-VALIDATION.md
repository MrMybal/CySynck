# Actual Unreal validation

The plugin uses C ABI 1 and an owned FIFO worker. The application core and bridge
versions are separate; these are development-source validations, not published
application binaries or an Epic certification.

`unreal-packaged-windows.json` records the complete successful Windows x64 run with
Unreal 5.8.3: five Development Editor tests and five tests in the real Development
game after cook/stage/archive. The script also verifies 34 native/CRT, license and
offline-reader files against their staged SDK sources using SHA-256. Its native DLL
hash and bridge/script source hashes identify the measured artifact. Subsequent
additive core RPCs do not retroactively change that recorded binary hash.

```powershell
./scripts/test-unreal-packaged.ps1 -EngineRoot 'G:/UnrealEngine/UE_5.8' `
  -SdkPrefix 'D:/CySynck/dist' -WorkDirectory 'D:/CySynck/fresh-packaged-test'
```

All work directories must be new. The helper first runs the actual Editor tests,
generates a minimal Runtime game module and an Entry-map project, builds with
`-NoEngineChanges`, cooks without Zen storage, stages/archives, checks loose runtime
resources, then runs game automation hidden with NullRHI and an explicit 60-second
process deadline. It drains both output streams and checks the JSON test report;
launching a GUI executable is not treated as proof that tests finished. UAT logs,
cooker logs and filesystem DDC are redirected into the owned work directory.

The cases cover native ABI/Unicode/error handling and localized embedded help,
asynchronous FIFO/proxy lifetime, queue backpressure, cancellation/close and worker
join/shutdown/restart. The reader URL stays under the packaged plugin's native SDK
without changing the host's working directory or global documentation environment.
No browser is launched. Runtime dependencies use loose NonUFS resources so the
local file reader and loader can access them outside the PAK.

The updated bridge also builds and passes all five tests in **5.7.4 Windows
Development Editor** through `scripts/test-unreal.ps1`. The 5.8.3 script completed
the same Editor checks before the packaged game checks. The normal native CI
validates SDK staging without requiring Epic's installed engines.

Unreal Linux/macOS hosts, other engine versions, Shipping configurations and physical
GPU rendering remain target checks. NullRHI and a Windows Development game do not
establish those capabilities. The later `POST /api/maintenance/tick` RPC is checked
through the real C ABI core tests; the host must schedule it on a worker/async node.
The Unreal ticker dispatches completions and does not silently start maintenance.
