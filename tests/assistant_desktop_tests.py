"""Drive the real hidden native window through its authenticated MCP endpoint."""
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
    }}), "--allow-write")["result"]["id"]
    process = subprocess.Popen([str(desktop), "--state", str(state), "--no-tray", "--mcp-port", "0",
                                "--mcp-allow-write", "--view", "Assistant", "--language", "en",
                                "--frames", "600", "--capture", str(capture)],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
    lines = queue.Queue()
    threading.Thread(target=lambda: lines.put(process.stderr.readline()), daemon=True).start()
    try:
        line = lines.get(timeout=20)
        match = re.search(r"127\.0\.0\.1:(\d+)/mcp", line)
        check(bool(match), "Native desktop publishes its authenticated endpoint: " + line)
        endpoint = "http://127.0.0.1:" + match.group(1)

        def call(name, parameters=None):
            return command("tools", "call", name, json.dumps({"parameters": parameters or {}}),
                           "--endpoint", endpoint)["result"]

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

print(f"PASS {checks} live native desktop/MCP checks; fixture removed")
