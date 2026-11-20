# Local MCP access profiles — validation

Date: 2026-11-20. Local Windows x64 Release validation; no GitHub workflow was launched.

This report records the integration validation before its later publication. The
[November 20 Windows preview](https://github.com/MrMybal/CySynck/releases/tag/v0.41.0-preview.20261120)
contains this SDK/profile update; local-only statements below describe the original validation stage.

## Integrated dependency

- Requested minimum: `34d4bcae126bc83d1a2925b5047dac36e3d09d92`.
- Integrated: **CyMCPSDK 0.7.2**, commit
  **`e562c731a146d599e3404718ea8414bb5b96b66d`**.
- Both existing desktop and headless build trees compile directly from
  **`D:/_Project/CyMCPSDK`**. Both CMake caches and `cysynck tools info` confirm it.
- The local SDK advanced from the requested commit during compilation. The integrated
  commit immediately follows it and changes the version/documentation, retaining the
  same native access-profile API. Git ancestry was verified; the SDK checkout is clean
  and was not edited by this integration. CMake's fallback Git pin uses this revision.

## Behavior

`Options::access_profile` defaults to `LocalAccessProfile::free`. The host applies
`local_access_settings()` to both its registry and its HTTP transport:

| Profile ID | Token required | Registry authorization |
| --- | --- | --- |
| `free` | No | `Policy::allow_all()` |
| `local_controlled` | No | Existing read/preview policy plus explicit write grant |
| `token_only` | Yes | `Policy::allow_all()` |
| `protected` | Yes | Existing read/preview policy plus explicit write grant |

Free and Local controlled use the SDK's explicit `UnauthenticatedLoopback`
constructor. Its listener binds `127.0.0.1`; its Host/Origin checks remain active.
Token profiles use the token constructor. All accepted local requests map to the same
application principal before SDK dispatch, including private resource ownership.
The Free registry has no additional principal/write-approval gate. All 130 operations
remain accessible through compact discovery or the optional full catalogue.

Argument schemas, native plan tokens, quotas, destination protections, pinned-key inputs,
process/tunnel ownership and OS permissions remain enforced. Free authorizes the
operations; it does not replace their native business contracts.

## Configuration and migration

The private `assistant-options.json` stores `access_profile`. Missing files, missing
selection, null selection and empty selection migrate to Free and persist that choice.
Other settings are preserved. Explicit valid profile selections remain unchanged,
including the `protected_access` alias. Unknown/invalid profiles fail rather than
silently selecting Free; failed validation leaves the original file unchanged.

Explicit older access controls (`require_token`, `control_operations`,
`mcp_without_token`, `allow_changes`) retain their combined authentication/policy
meaning when migrating. Performance-only settings are not treated as an access choice.

The native selector saves the next-launch choice and explains the required application
restart. It is disabled while MCP/chat is active; a pending choice blocks starting a
connection under the old profile. Startup options apply an explicit choice immediately:

```sh
cysynck --state ./profile mcp --port 0 --access-profile free
cysynck --state ./profile mcp --port 0 --access-profile protected
cysynck-desktop --state ./profile --mcp-port 0 --mcp-access-profile protected
```

`--allow-write` / `--mcp-allow-write` retain their controlled-profile behavior. They do
not restrict Free or Token only. The CLI accepts either a loopback origin or the displayed
`/mcp` URL and initially sends no token unless one is explicitly supplied. For legacy
token profiles, an HTTP 401 challenge can be retried with the selected profile's token;
that transport rejection happens before application dispatch. Business errors/mutations
are not automatically replayed. Remote callers cannot change the running host's profile.

## Verification performed

Desktop: **six CTest suites passed**. Headless: **five suites passed**.

| Suite | Desktop | Headless |
| --- | --- | --- |
| SDK/provider/chat integration | 43 checks | 27 checks |
| SDK/native HTTP access profiles and migrations | 60 checks | 60 checks |
| Existing CLI, stdio and authenticated MCP workflows | 123 checks | 123 checks |
| Actual CLI profile/default/authentication paths | 61 checks | 61 checks |
| Native application action integration | 106 checks | 106 checks |
| Actual native desktop through Free/Protected HTTP | 25 checks | Not applicable |

- Default and migrated Free hosts accept actual SDK `McpClient` and native `/v1`
  requests without bearer headers. Real project creation/update reaches the native
  application and persists. Invalid arguments fail without another project mutation.
- A separate CLI profile connects to Free using only the displayed `/mcp` address.
  Explicit profiles persist across later invocations. Token-only/Protected reject
  missing and incorrect tokens; valid credentials read successfully. Local controlled
  and Protected still deny mutations until the local write grant is enabled.
- The real hidden desktop changes language, theme, view and selected project through
  raw HTTP with **no token, no write flag and no UI confirmation**. Shutdown preserves
  those changes. Restarting it with Protected rejects unauthenticated reads/writes
  with **HTTP 401**; a valid token reads, and ungranted writes receive **HTTP 403**.
- Both tokenless profiles retain Host/Origin refusal tests. Direct Free registry calls
  do not inherit the controlled profile's extra principal gate.
- The existing deterministic local API/SSE chat suite still covers streaming,
  cancellation, errors, durable history and tool errors. Its added Free scenario creates
  a real project through the SDK chat with no write-grant call; exactly one tool mutation
  and two provider requests are observed, without confirmation/retry.
- The CyDoc build validates **92 localized pages**, shared EN/FR IDs/anchors, command
  listings and translation revisions. **10 reader tests passed**. The native French
  Free-profile capture was inspected. README/version/dependency notes distinguish this
  source update from the previously published Windows preview.

Temporary CLI, SDK, GUI, project and profile fixtures are removed automatically. Private
logs and the small native render are retained under `.local/mcp-access-*.log` and
`.local/mcp-access-validation/`.

## Reproduce

```sh
cmake -S . -B build -DCYSYNCK_CYMCP_SOURCE_DIR=D:/_Project/CyMCPSDK
cmake --build build --config Release --target cysynck cysynck-desktop cysynck-assistant-tests cysynck-assistant-actions-tests cysynck-assistant-access-tests
ctest --test-dir build -C Release -R '^cysynck-assistant(-access(-cli)?|-actions|-cli|-desktop)?$' --output-on-failure
node .CyDocBuild/update-cli.mjs
npm run build --prefix .CyDocBuild
npm test --prefix .CyDocBuild
```

For headless validation use the existing `build-headless` with `CYSYNCK_DESKTOP=OFF`
and `CYSYNCK_TESTS=ON`; omit the desktop target/test. Native Linux/macOS builds, real
external model accounts and privileged VPN operations were not exercised for this
SDK update. No push or binary release publication is part of this local validation.
