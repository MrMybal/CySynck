"""Drive the real hidden window through Free and Protected MCP profiles."""
import json
import os
from pathlib import Path
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import urllib.error
import urllib.request

desktop, cli = map(lambda p: Path(p).resolve(), sys.argv[1:3])
checks = 0


def check(value, message):
    global checks
    if not value:
        raise AssertionError(message)
    checks += 1


with tempfile.TemporaryDirectory(prefix="assistant-desktop-fixture-", dir=desktop.parent.parent) as temporary:
    root = Path(temporary)
    state = root / "profile"
    project = root / "project"
    project.mkdir()
    capture = root / "native.bmp"

    def command(*args):
        result = subprocess.run([str(cli), "--state", str(state), *args], capture_output=True,
                                text=True, encoding="utf-8", timeout=20)
        check(result.returncode == 0, result.stderr)
        return json.loads(result.stdout)

    project_id = command("tools", "call", "cysynck.project.create", json.dumps({"parameters": {
        "name": "Native UI fixture", "root": str(project), "mode": "backup", "backup_interval_minutes": 0
    }}))["result"]["id"]
    process = subprocess.Popen([str(desktop), "--state", str(state), "--no-tray", "--mcp-port", "0",
                                "--view", "Assistant", "--language", "en",
                                "--frames", "600", "--capture", str(capture)],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
    lines = queue.Queue()
    threading.Thread(target=lambda: lines.put(process.stderr.readline()), daemon=True).start()
    try:
        line = lines.get(timeout=20)
        match = re.search(r"127\.0\.0\.1:(\d+)/mcp", line)
        check(bool(match) and "access: free" in line, "Native desktop defaults to Free: " + line)
        endpoint = "http://127.0.0.1:" + match.group(1)

        def call(name, parameters=None):
            # No Authorization header, token file, write flag or UI grant.
            request = urllib.request.Request(endpoint + "/v1", data=json.dumps({
                "method": "invoke", "name": name, "arguments": {"parameters": parameters or {}}
            }).encode(), headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(request, timeout=20) as response:
                result = json.load(response)
            check("error" not in result, "Free native action succeeds without confirmation")
            return result["result"]

        initial = call("cysynck.ui.status")
        check(initial["ui_language"] == "en" and initial["ui_view"] == "Assistant" and initial["live"],
              "Status reports the real open window")
        changed = call("cysynck.ui.configure", {"ui_language": "fr", "ui_theme": "navy", "ui_view": "Overview", "ui_project": project_id})
        check(changed["ui_language"] == "fr" and changed["ui_theme"] == "navy" and changed["ui_project"] == project_id,
              "MCP changes actual language, theme and selected project")
        check(call("cysynck.ui.status")["ui_view"] == "Overview", "Live actions continue outside the Assistant view")
        changed = call("cysynck.settings.set", {"ui_theme": "violet", "ui_view": "Assistant"})
        check(changed["ui_theme"] == "violet" and changed["live"], "Saved-settings tool applies through the UI thread")
        check(not call("cysynck.receiver.status")["running"], "Desktop and assistant share one receiver")
        check(isinstance(call("cysynck.startup.status"), dict), "Startup inspection uses the native desktop controller")
        # Leave a French rendered window and exit naturally, verifying that the
        # application's shutdown persistence retains externally applied settings.
        process.wait(timeout=35)
        check(process.returncode == 0 and capture.read_bytes()[:2] == b"BM", "Native renderer captures and exits cleanly")
        settings = command("tools", "call", "cysynck.settings.get", "{}")["result"]
        check(settings["ui_language"] == "fr" and settings["ui_theme"] == "violet" and settings["ui_project"] == project_id,
              "Desktop shutdown preserves MCP-applied preferences")
        evidence = os.environ.get("CYSYNCK_ASSISTANT_CAPTURE")
        if evidence:
            shutil.copyfile(capture, Path(evidence))
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)

    protected = subprocess.Popen([str(desktop), "--state", str(state), "--no-tray", "--mcp-port", "0",
                                 "--mcp-access-profile", "protected", "--view", "Assistant",
                                 "--frames", "500", "--capture", str(root / "protected.bmp")],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
    lines = queue.Queue()
    threading.Thread(target=lambda: lines.put(protected.stderr.readline()), daemon=True).start()
    try:
        line = lines.get(timeout=20)
        match = re.search(r"127\.0\.0\.1:(\d+)/mcp", line)
        check(bool(match) and "access: protected" in line, "Explicit Protected selection survives native startup")
        endpoint = "http://127.0.0.1:" + match.group(1)

        def protected_call(name, parameters=None, token=None):
            headers = {"Content-Type": "application/json"}
            if token:
                headers["Authorization"] = "Bearer " + token
            request = urllib.request.Request(endpoint + "/v1", data=json.dumps({
                "method": "invoke", "name": name, "arguments": {"parameters": parameters or {}}
            }).encode(), headers=headers)
            try:
                with urllib.request.urlopen(request, timeout=20) as response:
                    return response.status, json.load(response)
            except urllib.error.HTTPError as error:
                return error.code, None

        check(protected_call("cysynck.ui.status")[0] == 401, "Real Protected desktop rejects address-only reads")
        check(protected_call("cysynck.ui.configure", {"ui_theme": "navy"})[0] == 401,
              "Real Protected desktop rejects address-only writes")
        check(protected_call("cysynck.ui.status", token="incorrect-token")[0] == 401,
              "Protected desktop rejects incorrect tokens")
        token = (state / "api-token").read_text(encoding="utf-8").strip()
        status, current = protected_call("cysynck.ui.status", token=token)
        check(status == 200 and current["result"]["ui_theme"] == "violet", "Valid Protected token reads the unchanged live UI")
        status, denied = protected_call("cysynck.ui.configure", {"ui_theme": "navy"}, token=token)
        check(status == 403, "Protected retains its operation policy after authentication")
        check(json.loads((state / "assistant-options.json").read_text(encoding="utf-8"))["access_profile"] == "protected",
              "Explicit native profile choice is persisted")
        protected.wait(timeout=35)
        check(protected.returncode == 0, "Protected native window closes cleanly")
    finally:
        if protected.poll() is None:
            protected.terminate()
            protected.wait(timeout=10)

print(f"PASS {checks} live Free/Protected native desktop/MCP checks; fixture removed")
