"""Verify persisted profiles and address-only access against the real executable."""
from pathlib import Path
import json
import os
import queue
import re
import subprocess
import sys
import tempfile
import threading
import urllib.error
import urllib.request

exe = Path(sys.argv[1]).resolve()
checks = 0
environment = dict(os.environ)
environment.pop("CYSYNCK_API_TOKEN", None)


def check(value, message):
    global checks
    if not value:
        raise AssertionError(message)
    checks += 1


def invoke(state, *arguments, code=0, env=None):
    result = subprocess.run([str(exe), "--state", str(state), *arguments], capture_output=True,
                            text=True, encoding="utf-8", env=env or environment, timeout=20)
    check(result.returncode == code, result.stderr)
    return json.loads(result.stdout) if code == 0 else None


def native(origin, operation, arguments=None, token=None, headers=None):
    request_headers = {"Content-Type": "application/json", **(headers or {})}
    if token:
        request_headers["Authorization"] = "Bearer " + token
    payload = json.dumps({"method": "invoke", "name": operation, "arguments": arguments or {}}).encode()
    request = urllib.request.Request(origin + "/v1", data=payload, headers=request_headers)
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        body = error.read()
        return error.code, json.loads(body) if body else None


with tempfile.TemporaryDirectory(prefix="assistant-access-cli-", dir=exe.parent.parent) as temporary:
    root = Path(temporary)
    fresh = root / "fresh"
    info = invoke(fresh, "tools", "info")
    check(info["sdk_revision"] == "e562c731a146d599e3404718ea8414bb5b96b66d" and info["sdk_version"] == "0.7.2",
          "The real executable uses the requested SDK revision built from local sources")
    check(info["access_profile"] == "free" and info["writes_allowed"] and not info["require_token"],
          "Fresh profiles use Free without an explicit grant")
    check(json.loads((fresh / "assistant-options.json").read_text(encoding="utf-8"))["access_profile"] == "free",
          "The default migration is persisted")
    for selected, token_required, controlled in [("free", False, False), ("local_controlled", False, True),
                                                 ("token_only", True, False), ("protected", True, True)]:
        state = root / selected
        info = invoke(state, "tools", "info", "--access-profile", selected)
        check(info["access_profile"] == selected and info["require_token"] == token_required,
              "CLI profile selection is explicit and persistent")
        check(invoke(state, "tools", "info")["access_profile"] == selected,
              "A later invocation preserves the chosen profile")
        process = subprocess.Popen([str(exe), "--state", str(state), "mcp", "--port", "0"],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                   encoding="utf-8", env=environment)
        lines = queue.Queue()
        threading.Thread(target=lambda: lines.put(process.stderr.readline()), daemon=True).start()
        try:
            line = lines.get(timeout=20)
            match = re.search(r"http://127\.0\.0\.1:(\d+)/mcp", line)
            check(bool(match), "The real application publishes its loopback endpoint")
            origin = "http://127.0.0.1:" + match.group(1)
            check(native(origin, "cysynck.overview")[0] == (401 if token_required else 200),
                  "Raw address-only requests follow the selected authentication mode")
            token = (state / "api-token").read_text(encoding="utf-8").strip() if token_required else None
            check(native(origin, "cysynck.overview", token=token)[0] == 200,
                  "Every profile supports a correctly connected native read")
            project = root / (selected + "-project")
            project.mkdir()
            create = {"parameters": {"name": "Address only", "root": str(project), "mode": "backup", "backup_interval_minutes": 0}}
            status, outcome = native(origin, "cysynck.project.create", create, token=token)
            check(status == (403 if controlled else 200) and ("error" in outcome) == controlled,
                  "Free and Token only modify without a grant; controlled profiles enforce policy")
            if not controlled:
                check(invoke(state, "project", "list")[0]["name"] == "Address only",
                      "The real native modification is durable")
                status, outcome = native(origin, "cysynck.project.create", {"parameters": {"name": 123, "root": str(project)}}, token=token)
                check(status == 400 and "error" in outcome and len(invoke(state, "project", "list")) == 1,
                      "Free access retains argument validation and refuses invalid mutations")
            other_client = root / (selected + "-client")
            if token_required:
                invoke(other_client, "tools", "call", "cysynck.overview", "{}", "--endpoint", origin, code=3)
                authenticated = dict(environment, CYSYNCK_API_TOKEN=token)
                check(invoke(other_client, "tools", "call", "cysynck.overview", "{}", "--endpoint", origin + "/mcp", env=authenticated).get("result") is not None,
                      "Protected CLI connections accept an explicit valid token")
            else:
                check(invoke(other_client, "tools", "call", "cysynck.overview", "{}", "--endpoint", origin + "/mcp").get("result") is not None,
                      "A separate CLI client connects with the displayed address alone")
                check(native(origin, "cysynck.overview", headers={"Origin": "https://untrusted.invalid"})[0] == 403,
                      "Tokenless access retains the SDK Origin check")
                check(native(origin, "cysynck.overview", headers={"Host": "untrusted.invalid"})[0] == 403,
                      "Tokenless access retains the SDK Host check")
        finally:
            process.terminate()
            process.wait(timeout=10)
    # An old options file without an access choice is migrated, not interpreted as Protected.
    old = root / "old-options"
    old.mkdir()
    (old / "assistant-options.json").write_text(json.dumps({"compact_discovery": False, "custom": "keep"}), encoding="utf-8")
    check(invoke(old, "tools", "info")["access_profile"] == "free", "Legacy options without an explicit access selection migrate to Free")
    check(json.loads((old / "assistant-options.json").read_text(encoding="utf-8"))["custom"] == "keep", "Migration preserves other settings")

print(f"PASS {checks} real CLI access-profile checks; temporary profiles removed")
