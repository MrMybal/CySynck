"""Opt-in Linux kernel test. All links, routes and tunnels live in owned namespaces.

Run as root on a disposable Linux runner with iproute2, iputils-ping, wireguard-tools
and OpenSSL installed. No host route, firewall rule or workstation tunnel is changed.
Only the sanitized JSON report is retained; ephemeral private keys are removed.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import random
import re
import secrets
import selectors
import shutil
import stat
import subprocess
import sys
import time
import uuid


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


class ScopedAppArmorReadAccess:
    """Supported local include; exact test configurations only; retain confinement."""
    def __init__(self, run, tag, rules):
        self.run, self.path, self.profile = run, None, None
        self.block = ("\n# BEGIN CySynck isolated CI " + tag + "\n" + rules +
                      "# END CySynck isolated CI " + tag + "\n").encode()
        self.original = None
        self.applied = False

    def replace_include(self, data):
        require(not self.path.is_symlink(), "Preserve linked AppArmor overrides")
        info = self.path.stat() if self.path.exists() else None
        temporary = self.path.parent / (".cysynck-ci-" + uuid.uuid4().hex)
        try:
            with os.fdopen(os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600), "wb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
                if info:
                    os.fchown(stream.fileno(), info.st_uid, info.st_gid)
                    os.fchmod(stream.fileno(), stat.S_IMODE(info.st_mode))
            require(not self.path.is_symlink(), "Preserve a changed AppArmor override")
            os.replace(temporary, self.path)
        finally:
            if temporary.exists():
                temporary.unlink()

    def prepare(self):
        inventory = Path("/sys/kernel/security/apparmor/profiles")
        if not inventory.exists():
            return {"prepared": False, "reason": "AppArmor profile inventory absent"}
        active = inventory.read_text().splitlines()
        if not any(line in ("wg-quick (enforce)", "/usr/bin/wg-quick (enforce)") for line in active):
            return {"prepared": False, "reason": "wg-quick is not in enforcing mode"}
        candidates = (Path("/etc/apparmor.d/wg-quick"), Path("/etc/apparmor.d/usr.bin.wg-quick"))
        self.profile = next((p for p in candidates if p.is_file() and not p.is_symlink()), None)
        require(self.profile is not None, "Cannot locate the installed enforcing wg-quick profile")
        match = re.search(r"include(?:\s+if\s+exists)?\s+<local/(wg-quick|usr\.bin\.wg-quick)>", self.profile.read_text())
        require(match is not None, "Installed wg-quick profile has no supported local include")
        self.path = self.profile.parent / "local" / match[1]
        require(self.path.parent.is_dir() and not self.path.parent.is_symlink(), "Preserve linked AppArmor include directories")
        require(not self.path.is_symlink(), "Preserve linked AppArmor local overrides")
        if self.path.exists():
            require(self.path.is_file() and self.path.stat().st_size <= 1024 * 1024, "Preserve unrelated AppArmor content")
            self.original = self.path.read_bytes()
        self.replace_include((self.original or b"") + self.block)
        self.applied = True
        self.run(["apparmor_parser", "-r", self.profile])
        return {"prepared": True, "profile": str(self.profile), "exact_configurations": 2,
                "permissions": "read-only, no wildcard, no disable/complain mode"}

    def close(self):
        if not self.applied:
            return
        require(not self.path.is_symlink(), "Preserve a changed AppArmor include")
        current = self.path.read_bytes()
        require(current.count(self.block) == 1, "AppArmor CI block changed; preserve concurrent changes")
        cleaned = current.replace(self.block, b"", 1)
        if self.original is None and not cleaned:
            self.path.unlink()
        else:
            self.replace_include(cleaned)
        self.run(["apparmor_parser", "-r", self.profile])
        self.applied = False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True, help="New absolute cysynck-wg-* directory")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--prepare-apparmor", action="store_true",
                        help="Temporarily add exact read-only test configs via the installed profile's local include")
    args = parser.parse_args()
    require(sys.platform == "linux" and os.geteuid() == 0, "Use a disposable Linux runner as root")
    cli = args.cli.resolve(strict=True)
    work = args.work
    require(work.is_absolute() and work.name.startswith("cysynck-wg-") and
            not work.exists() and not work.is_symlink(), "Reserve a new absolute cysynck-wg-* directory")
    parent = work.parent.resolve(strict=True)
    work = parent / work.name
    work.mkdir(mode=0o700)
    report = {"kernel": os.uname().release, "checks": [], "success": False}
    namespaces, children = [], []
    apparmor = None
    tag = "csci-" + uuid.uuid4().hex[:10]
    environment = {k: v for k, v in os.environ.items() if not k.startswith("CYSYNCK_")}
    environment.update(CYSYNCK_WG=shutil.which("wg") or "wg",
                       CYSYNCK_WG_QUICK=shutil.which("wg-quick") or "wg-quick",
                       CYSYNCK_OPENSSL=shutil.which("openssl") or "openssl",
                       CYSYNCK_MESH_TOKEN=secrets.token_hex(32))
    # Preserve the actual wg-quick failure in a bounded, key-redacted report. The
    # application still performs the activation and receives the real exit status.
    diagnostic = work / "activation.json"
    wrapper = work / "wg-quick-capture"
    wrapper.write_text("""#!/usr/bin/python3
import json, os, re, stat, subprocess, sys
from pathlib import Path
config = Path(sys.argv[2])
info = config.stat()
context = {'uid': os.getuid(), 'euid': os.geteuid(), 'file_uid': info.st_uid, 'file_gid': info.st_gid, 'file_mode': oct(stat.S_IMODE(info.st_mode))}
try:
    with config.open('rb') as stream: stream.read(1)
    context['config_readable_before_exec'] = True
except OSError as error:
    context['config_readable_before_exec'] = str(error)
try: context['security_context'] = Path('/proc/self/attr/current').read_text().strip()
except OSError: pass
result = subprocess.run([os.environ['CYSYNCK_TEST_REAL_WG_QUICK'], *sys.argv[1:]], capture_output=True, text=True)
message = re.sub(r'[A-Za-z0-9+/]{43}=', '<redacted-key>', result.stderr[-4000:])
message = message.replace(os.environ['CYSYNCK_MESH_TOKEN'], '<redacted-token>')
path = Path(os.environ['CYSYNCK_TEST_WG_DIAGNOSTIC'])
items = json.loads(path.read_text()) if path.exists() else []
items.append({'action': sys.argv[1], 'exit_code': result.returncode, 'stderr': message, 'context': context})
path.write_text(json.dumps(items[-8:]))
raise SystemExit(result.returncode)
""", encoding="utf-8")
    wrapper.chmod(0o700)
    environment.update(CYSYNCK_TEST_REAL_WG_QUICK=environment["CYSYNCK_WG_QUICK"],
                       CYSYNCK_TEST_WG_DIAGNOSTIC=str(diagnostic), CYSYNCK_WG_QUICK=str(wrapper))

    def run(command, *, namespace=None, check=True, input=None, timeout=90):
        if namespace:
            require(namespace in namespaces, "Use only a reserved namespace")
            command = ["ip", "netns", "exec", namespace] + list(command)
        result = subprocess.run(list(map(str, command)), input=input, capture_output=True,
                                text=True, env=environment, timeout=timeout)
        if check and result.returncode:
            # Never print stdout: wg genkey and other credential-producing tools use it.
            raise RuntimeError("Command failed: " + str(command[0]) + ": " + result.stderr[-3000:])
        return result

    def app(side, *command):
        result = run([cli, "--state", work / side / "state", *command], namespace=tag + "-" + side)
        return json.loads(result.stdout)

    def kernel_peer_transfer(side, interface, key):
        rows = run(["wg", "show", interface, "transfer"], namespace=tag + "-" + side).stdout.splitlines()
        for row in rows:
            fields = row.split()
            if fields[0] == key:
                return sum(map(int, fields[1:]))
        raise RuntimeError("Expected kernel peer is missing")

    try:
        for side in ("a", "b"):
            namespace = tag + "-" + side
            run(["ip", "netns", "add", namespace])
            namespaces.append(namespace)
            run(["ip", "link", "set", "lo", "up"], namespace=namespace)
            (work / side / "project").mkdir(parents=True)
        run(["ip", "link", "add", "probe-wg", "type", "wireguard"], namespace=namespaces[0])
        run(["ip", "link", "delete", "probe-wg"], namespace=namespaces[0])
        # Create both ends inside the test namespaces; no temporary host link is used.
        run(["ip", "-n", namespaces[0], "link", "add", "lan-a", "type", "veth",
             "peer", "name", "lan-b", "netns", namespaces[1]])
        profiles = {}
        for index, side in enumerate(("a", "b"), 1):
            namespace = tag + "-" + side
            run(["ip", "address", "add", f"192.0.2.{index}/30", "dev", "lan-" + side], namespace=namespace)
            run(["ip", "link", "set", "lan-" + side, "up"], namespace=namespace)
            app(side, "project", "add", "Kernel VPN test", str(work / side / "project"),
                "--id", "wireguard-ci", "--mode", "backup", "--interval", "0")
            # The endpoint is this device's externally reachable listener, not the peer's.
            profiles[side] = app(side, "vpn", "configure", "Kernel-" + side, f"10.77.230.{index}",
                                 "--endpoint", f"192.0.2.{index}:{51820 + index}",
                                 "--listen-port", str(51820 + index))
            app(side, "vpn", "publish", "wireguard-ci")
        for side, other in (("a", "b"), ("b", "a")):
            exchange = work / side / "project/.cysynck-network/peers"
            source = work / other / "project/.cysynck-network/peers"
            # This test isolates actual WireGuard/app traffic. Existing real Syncthing
            # tests cover profile transport; the harness copies only public publications.
            for item in source.glob(profiles[other]["peer_id"] + "*.json"):
                shutil.copy2(item, exchange / item.name)
            candidates = app(side, "vpn", "discover", "wireguard-ci")["peers"]
            candidate = next(c for c in candidates if c["profile"]["peer_id"] == profiles[other]["peer_id"])
            signer_der = base64.b64decode(profiles[other]["signing_key"], validate=True)
            fingerprint = hashlib.sha256(signer_der).hexdigest()
            require(candidate["signing_fingerprint"] == fingerprint, "Independent signer fingerprint differs")
            app(side, "vpn", "trust", "wireguard-ci", profiles[other]["peer_id"],
                "--key", profiles[other]["public_key"], "--fingerprint", fingerprint)
        if args.prepare_apparmor:
            rules = ""
            for side in ("a", "b"):
                plan = app(side, "vpn", "apparmor")
                logical = "cs-" + hashlib.sha256(profiles[side]["peer_id"].encode()).hexdigest()[:12]
                configuration = work / side / "state/vpn" / (logical + ".conf")
                expected = f'"{configuration.parent}/" r,\n"{configuration}" r,\n'
                require(plan["supported"] and Path(plan["configuration"]) == configuration and plan["rules"] == expected,
                        "AppArmor export exceeds the two exact owned test configurations")
                rules += plan["rules"]
            apparmor = ScopedAppArmorReadAccess(run, tag, rules)
            report["apparmor"] = apparmor.prepare()
        for side in ("a", "b"):
            app(side, "vpn", "up")
        interfaces = {side: app(side, "vpn", "status")["interface"] for side in ("a", "b")}
        deadline = time.monotonic() + 20
        while True:
            ping = run(["ping", "-n", "-c", "1", "-W", "2", "10.77.230.2"], namespace=namespaces[0], check=False)
            if ping.returncode == 0:
                break
            require(time.monotonic() < deadline, "WireGuard kernel handshake/data did not become reachable")
        for side in ("a", "b"):
            status = app(side, "vpn", "status")
            require(status["tunnel"]["owned"] and any(h["recent"] for h in status["handshakes"]),
                    "No recent owned kernel WireGuard handshake")
        report["checks"].append("real-kernel-handshake-and-ping")
        before = kernel_peer_transfer("a", interfaces["a"], profiles["b"]["public_key"])

        receiver = subprocess.Popen(["ip", "netns", "exec", namespaces[1], str(cli), "--state",
                                     str(work / "b/state"), "chat", "serve"],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=environment)
        children.append(receiver)
        selector = selectors.DefaultSelector()
        selector.register(receiver.stdout, selectors.EVENT_READ)
        ready = selector.select(timeout=15)
        require(ready and receiver.stdout.readline().startswith("CySynck direct receiver:"),
                "Direct receiver did not bind to its real VPN address")
        selector.close()
        delivered = app("a", "chat", "send", "wireguard-ci", profiles["b"]["peer_id"], "Kernel WireGuard hello")
        require(delivered["delivered"], "Chat was not delivered through WireGuard")
        history = app("b", "chat", "history", "wireguard-ci")
        require(any(m["body"].get("text") == "Kernel WireGuard hello" for m in history), "Receiver chat bytes differ")
        report["checks"].append("authenticated-chat-over-kernel-wireguard")

        rng, digest = random.Random(20261108), hashlib.sha256()
        payload = work / "a/project/payload.bin"
        with payload.open("xb") as stream:
            for _ in range(16):
                block = rng.randbytes(1024 * 1024)
                stream.write(block)
                digest.update(block)
        transferred = app("a", "send-file", "wireguard-ci", profiles["b"]["peer_id"], "payload.bin")
        require(transferred["delivered"], "Real WireGuard file transfer did not finish")
        received = app("b", "received", "verify", "wireguard-ci", transferred["id"])
        require(received["ok"] and received["file"]["sha256"] == digest.hexdigest(), "Received kernel-transfer checksum differs")
        stored = work / "b/state/inbox/wireguard-ci" / (transferred["id"] + ".received")
        with stored.open("rb") as stream:
            require(hashlib.file_digest(stream, "sha256").hexdigest() == digest.hexdigest(), "Stored payload bytes differ")
        growth = kernel_peer_transfer("a", interfaces["a"], profiles["b"]["public_key"]) - before
        require(growth > payload.stat().st_size, "Payload did not increase the real WireGuard encrypted byte counters")
        report.update(payload_bytes=payload.stat().st_size, payload_sha256=digest.hexdigest(), wireguard_bytes_growth=growth)
        report["checks"].append("verified-resumable-protocol-file-over-kernel-wireguard")

        revoked = app("a", "vpn", "revoke", "wireguard-ci", profiles["b"]["peer_id"])
        require(revoked["tunnel_peer_removed"] and not revoked["requires_restart"], "Live kernel revocation failed")
        require(not run(["wg", "show", interfaces["a"], "peers"], namespace=namespaces[0]).stdout.strip(), "Revoked peer remains in kernel")
        config = work / "a/state/vpn" / (interfaces["a"] + ".conf")
        require(profiles["b"]["public_key"] not in config.read_text(), "Revoked peer remains in restart configuration")
        app("a", "vpn", "down")
        app("a", "vpn", "up")
        require(not run(["wg", "show", interfaces["a"], "peers"], namespace=namespaces[0]).stdout.strip(), "Restart resurrected a revoked peer")
        require(run(["ping", "-n", "-c", "1", "-W", "1", "10.77.230.2"], namespace=namespaces[0], check=False).returncode != 0,
                "Revoked peer still exchanges kernel data after restart")
        report["checks"].append("live-revocation-and-service-restart-preservation")

        app("a", "vpn", "down")
        run(["ip", "link", "add", interfaces["a"], "type", "wireguard"], namespace=namespaces[0])
        private_key = run(["wg", "genkey"]).stdout
        foreign_key = work / "foreign-private.key"
        foreign_key.write_text(private_key)
        foreign_key.chmod(0o600)
        foreign_public = run(["wg", "pubkey"], input=private_key).stdout.strip()
        run(["wg", "set", interfaces["a"], "private-key", foreign_key], namespace=namespaces[0])
        refused = run([cli, "--state", work / "a/state", "vpn", "up"], namespace=namespaces[0], check=False)
        require(refused.returncode != 0, "CySynck activated a foreign kernel tunnel")
        require(run(["wg", "show", interfaces["a"], "public-key"], namespace=namespaces[0]).stdout.strip() == foreign_public,
                "CySynck replaced a foreign kernel identity")
        report["checks"].append("foreign-kernel-interface-preserved")
        report["success"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        if diagnostic.exists():
            report["wireguard_activation"] = json.loads(diagnostic.read_text())
        for child in children:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=5)
            child.communicate(timeout=5)
        for namespace in reversed(namespaces):
            require(re.fullmatch(re.escape(tag) + r"-[ab]", namespace), "Refuse foreign namespace cleanup")
            result = run(["ip", "netns", "delete", namespace], check=False)
            if result.returncode:
                report["success"] = False
                report.setdefault("cleanup_errors", []).append(result.stderr[-1000:])
        if apparmor:
            try:
                apparmor.close()
                if report.get("apparmor", {}).get("prepared"):
                    report["apparmor"]["restored"] = True
            except Exception as error:
                report["success"] = False
                report.setdefault("cleanup_errors", []).append(str(error))
        require(work.parent == parent and work.name.startswith("cysynck-wg-") and not work.is_symlink(), "Refuse foreign work cleanup")
        shutil.rmtree(work)
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        args.report.chmod(0o644)
    print(json.dumps(report))
    return 0 if report["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
