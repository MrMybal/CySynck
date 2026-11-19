"""Exercise the shipped console entry points; all state is private temporary test data."""
import json
import os
from pathlib import Path
import queue
import re
import subprocess
import sys
import tempfile
import threading

exe = Path(sys.argv[1]).resolve()
checks = 0


def check(value, message):
    global checks
    if not value:
        raise AssertionError(message)
    checks += 1


with tempfile.TemporaryDirectory(prefix="cysynck-assistant-cli-", dir=exe.parent.parent) as temporary:
    root = Path(temporary).resolve()
    state = root / "profile"
    project = root / "project café % #"
    project.mkdir()
    (project / "hello.txt").write_text("real application fixture", encoding="utf-8")

    def cli(*args, code=0, environment=None):
        result = subprocess.run([str(exe), "--state", str(state), *args],
                                capture_output=True, text=True, encoding="utf-8",
                                env=environment, timeout=30)
        check(result.returncode == code,
              f"CLI code {result.returncode}, expected {code}: {result.stderr[:500]}")
        if code:
            check(bool(json.loads(result.stderr).get("error")), "Failures are JSON diagnostics on stderr")
            check(not result.stdout.strip(), "Failed commands do not pollute stdout")
            return None
        check(not result.stderr.strip(), "Successful CLI has clean stderr")
        return json.loads(result.stdout)

    check("project add" in cli("help")["help"], "Legacy help remains available")
    check(cli("project", "list") == [], "Legacy project listing remains available")
    check(cli("docs", "assistant", "--language", "fr", "--print")["url"].startswith("file:///"), "French contextual help resolves locally")
    check("tools list" in cli("tools", "help")["help"], "New CLI has usage help")
    check(len(cli("tools", "list")) > 60, "CLI discovers the entire real operation catalogue")
    schema = cli("tools", "describe", "cysynck.project.create")
    check("parameters" in schema["inputSchema"]["required"], "CLI describes a validated operation schema")
    check(cli("tools", "info")["sdk_version"] == "0.7.0", "CLI reports source-built SDK provenance")
    actions = cli("tools", "list")
    check(len(actions) >= 125, "CLI includes engine, VPN, transfer and desktop actions")
    cli("tools", "call", "cysynck.settings.set", '{"parameters":{"ui_language":"fr"}}', "--allow-write")
    check(cli("tools", "call", "cysynck.settings.get", "{}")["result"]["ui_language"] == "fr", "CLI saves native desktop preferences")
    cli("tools", "call", "cysynck.engine.start", "{}", "--allow-write", code=2)
    cli("tools", "call", "cysynck.receiver.start", "{}", "--allow-write", code=2)
    cli("tools", "call", "cysynck.ui.configure", '{"parameters":{"ui_theme":"navy"}}', "--allow-write", code=1)
    arguments = root / "create.json"
    arguments.write_text(json.dumps({"parameters": {"name": "CLI fixture", "root": str(project), "mode": "backup"}}), encoding="utf-8")
    cli("tools", "call", "cysynck.project.create", "@" + str(arguments), code=3)
    check(not cli("project", "list"), "Denied CLI mutation leaves native project state unchanged")
    created = cli("tools", "call", "cysynck.project.create", "@" + str(arguments), "--allow-write")["result"]
    project_id = created["id"]
    check(cli("project", "list")[0]["id"] == project_id, "Registry mutation is visible through the legacy CLI")
    backup = cli("backup", project_id)
    inventory_arguments = json.dumps({"project_id":project_id,"snapshot":backup["id"]})
    cli("tools", "call", "cysynck.project.snapshot-inventory", inventory_arguments, code=2)
    blobs = [p for p in (state / "objects").iterdir() if p.is_file()]
    check(len(blobs) == 1, "Fixture backup creates one independently verifiable object")
    original = blobs[0].read_bytes()
    try:
        blobs[0].write_bytes(b"corrupted test object")
        failed = subprocess.run([str(exe), "--state", str(state), "tools", "call", "cysynck.project.verify", json.dumps({"project_id": project_id})],
                                capture_output=True, text=True, encoding="utf-8", timeout=30)
        check(failed.returncode == 4 and json.loads(failed.stdout)["result"]["ok"] is False,
              "A failed business verification returns exit 4 with its complete JSON report")
        check(json.loads(failed.stderr)["error"]["code"] == "BusinessCheckFailed", "Business failures carry a structured stderr diagnostic")
    finally:
        blobs[0].write_bytes(original)
    cli("tools", "invalid-command", code=2)
    cli("tools", "call", "cysynck.project.snapshots", json.dumps({"project_id":"missing-project"}), code=1)
    check(cli("tools", "call", "cysynck.overview", "{}")["result"]["projects"][0]["id"] == project_id,
          "Inline JSON arguments execute against the actual application")

    class Stdio:
        def __init__(self, writes=False):
            args = [str(exe), "--state", str(state), "mcp", "--stdio", "--full"]
            if writes:
                args.append("--allow-write")
            self.process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE, text=True, encoding="utf-8")
            self.output = queue.Queue()
            threading.Thread(target=lambda: [self.output.put(line) for line in self.process.stdout], daemon=True).start()
            self.serial = 0

        def call(self, method, params):
            self.serial += 1
            self.process.stdin.write(json.dumps({"jsonrpc": "2.0", "id": self.serial, "method": method, "params": params}) + "\n")
            self.process.stdin.flush()
            result = json.loads(self.output.get(timeout=20))
            check(result.get("id") == self.serial, "Stdio stdout contains only correctly correlated JSON-RPC")
            return result

        def close(self):
            self.process.stdin.close()
            try:
                self.process.wait(timeout=15)
            finally:
                if self.process.poll() is None:
                    self.process.kill()
                    self.process.wait(timeout=5)
            check(self.process.returncode == 0, "Stdio exits normally after EOF")
            check(not self.process.stderr.read().strip(), "Stdio keeps diagnostics clean on success")

    for writable in (False, True):
        connection = Stdio(writable)
        try:
            response = connection.call("initialize", {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "CySynck test", "version": "1"}})
            check("result" in response, "An external legacy-compatible MCP client initializes")
            tools = []
            params = {}
            while True:
                page = connection.call("tools/list", params)["result"]
                tools.extend(page["tools"])
                if "nextCursor" not in page:
                    break
                params = {"cursor": page["nextCursor"]}
            check(len(tools) > 60, "Full MCP discovery follows actual tool pagination")
            response = connection.call("tools/call", {"name": "cysynck.overview", "arguments": {}})["result"]
            check(not response.get("isError", False), "External stdio reads native state")
            response = connection.call("tools/call", {"name": "cysynck.project.update", "arguments": {"project_id": project_id, "parameters": {"name": "MCP changed"}}})["result"]
            check(response.get("isError", False) == (not writable), "Stdio enforces the same mutation policy as the CLI")
        finally:
            connection.close()
    check(cli("project", "list")[0]["name"] == "MCP changed", "Authorized external stdio mutation persists in the real application")

    server = subprocess.Popen([str(exe), "--state", str(state), "mcp", "--port", "0"],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
    lines = queue.Queue()
    threading.Thread(target=lambda: lines.put(server.stderr.readline()), daemon=True).start()
    try:
        line = lines.get(timeout=20)
        port = re.search(r"127\.0\.0\.1:(\d+)/mcp", line)
        check(bool(port), "HTTP server reports its bound loopback endpoint on stderr")
        endpoint = "http://127.0.0.1:" + port.group(1)
        check(len(cli("tools", "list", "--endpoint", endpoint)) > 60, "CLI discovers the exact running instance")
        check(cli("tools", "call", "cysynck.overview", "{}", "--endpoint", endpoint)["result"]["projects"][0]["id"] == project_id,
              "CLI invokes the running instance without opening a window")
        retained = cli("tools", "call", "cysynck.project.snapshot-inventory", inventory_arguments, "--endpoint", endpoint)
        resource = cli("tools", "read-resource", retained["resource"]["uri"], "--endpoint", endpoint)
        content = resource["contents"][0]["text"]
        check(json.loads(content)["path"] == "hello.txt", "CLI retrieves the complete retained export from its running host")
        cli("tools", "call", "cysynck.project.create", "@" + str(arguments), "--endpoint", endpoint, code=3)
        wrong = dict(os.environ, CYSYNCK_API_TOKEN="incorrect-token")
        cli("tools", "list", "--endpoint", endpoint, code=3, environment=wrong)
    finally:
        server.terminate()
        server.wait(timeout=10)
    check(len(cli("project", "list")) == 1, "Remote denials do not create duplicate projects")
    server = subprocess.Popen([str(exe), "--state", str(state), "mcp", "--port", "0", "--allow-write"],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
    lines = queue.Queue()
    threading.Thread(target=lambda: lines.put(server.stderr.readline()), daemon=True).start()
    try:
        line = lines.get(timeout=20)
        port = re.search(r"127\.0\.0\.1:(\d+)/mcp", line)
        check(bool(port), "Writable host exposes a persistent endpoint")
        endpoint = "http://127.0.0.1:" + port.group(1)
        cli("tools", "call", "cysynck.transfers.configure", '{"parameters":{"file_limit_mib":1,"inbox_quota_mib":2,"outbox_quota_mib":2}}', "--endpoint", endpoint)
        check(cli("tools", "call", "cysynck.transfers.limits", "{}", "--endpoint", endpoint)["result"]["file_limit_mib"] == 1,
              "Running MCP instance applies actual native transfer limits")
        check(not cli("tools", "call", "cysynck.receiver.status", "{}", "--endpoint", endpoint)["result"]["running"], "Remote receiver status reflects its owning instance")
    finally:
        server.terminate()
        server.wait(timeout=10)

print(f"PASS {checks} real CLI/MCP entry-point checks; temporary profiles removed")
