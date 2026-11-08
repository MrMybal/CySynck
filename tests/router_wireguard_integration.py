"""Opt-in actual PCP/NAT-PMP NAT forwarding test on a disposable Linux runner.

Two clients and two independent miniupnpd routers live wholly in fresh network
namespaces. No host interface, route, firewall rule or workstation tunnel is used.
Public profiles are copied by this harness; this is not a Syncthing transport test.
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
import shutil
import subprocess
import sys
import time
import uuid

from wireguard_integration import ScopedAppArmorReadAccess, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--prepare-apparmor", action="store_true")
    args = parser.parse_args()
    require(sys.platform == "linux" and os.geteuid() == 0, "Use a disposable Linux runner as root")
    cli = args.cli.resolve(strict=True)
    require(args.work.is_absolute() and args.work.name.startswith("cysynck-wg-nat-") and
            not args.work.exists() and not args.work.is_symlink(), "Reserve a fresh absolute cysynck-wg-nat-* directory")
    parent = args.work.parent.resolve(strict=True)
    work = parent / args.work.name
    work.mkdir(mode=0o700)
    tag = "csnat-" + uuid.uuid4().hex[:10]
    spaces, children, streams = [], [], []
    apparmor = None
    report = {"success": False, "kernel": os.uname().release, "checks": [],
              "topology": "two clients, two miniupnpd routers, three veth pairs, no host network changes"}
    environment = {k: v for k, v in os.environ.items() if not k.startswith("CYSYNCK_")}
    environment.update(CYSYNCK_WG=shutil.which("wg") or "wg",
                       CYSYNCK_WG_QUICK=shutil.which("wg-quick") or "wg-quick",
                       CYSYNCK_OPENSSL=shutil.which("openssl") or "openssl",
                       CYSYNCK_MESH_TOKEN=secrets.token_hex(32))

    def run(command, *, space=None, check=True, input=None, timeout=90):
        if space:
            require(space in spaces, "Refuse an unowned namespace")
            command = ["ip", "netns", "exec", space, *command]
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True,
                                input=input, env=environment, timeout=timeout)
        if check and result.returncode:
            # stdout may contain freshly generated credentials; never log it.
            raise RuntimeError("Command failed: " + str(command[0]) + ": " + result.stderr[-1500:])
        return result

    def app(side, *command):
        return json.loads(run([cli, "--state", work / side / "state", *command], space=tag + "-" + side).stdout)

    def start(command, space, log_name, private_runtime=None):
        require(space in spaces, "Refuse an unowned process namespace")
        if private_runtime:
            # miniupnpd can use a fixed /var/run control socket. A private mount
            # namespace keeps that socket, locks and runtime files off the host.
            private_runtime.mkdir(mode=0o700)
            command = ["unshare", "--mount", "--propagation", "private", "--", sys.executable,
                       "-c", "import os,subprocess,sys;subprocess.run(['mount','--bind',sys.argv[1],'/run'],check=True);os.execvp(sys.argv[2],sys.argv[2:])",
                       private_runtime, *command]
        output = (work / log_name).open("wb")
        streams.append(output)
        child = subprocess.Popen(list(map(str, ["ip", "netns", "exec", space, *command])),
                                 env=environment, stdout=output, stderr=subprocess.STDOUT)
        children.append(child)
        return child

    def stop(child):
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=5)

    def firewall(router, *arguments):
        return run(["iptables", "-w", "5", *arguments], space=tag + "-r" + router)

    def copy_publications(source, destination):
        incoming = work / source / "project/.cysynck-network/peers"
        target = work / destination / "project/.cysynck-network/peers"
        for name in (profiles[source]["peer_id"] + ".json", profiles[source]["peer_id"] + ".endpoint.json"):
            item = incoming / name
            if item.exists():
                require(item.is_file() and not item.is_symlink(), "Refuse linked public fixture material")
                shutil.copy2(item, target / name)

    def await_ping():
        deadline = time.monotonic() + 25
        while run(["ping", "-n", "-c", "1", "-W", "1", "10.77.231.2"],
                  space=tag + "-a", check=False).returncode:
            require(time.monotonic() < deadline, "WireGuard could not pass data through both actual NAT routers")

    def foreign_rule(side):
        rules = firewall(side, "-t", "nat", "-S", "MINIUPNPD").stdout
        return any("--dport 45000 " in line and f"192.168.{61 if side == 'a' else 62}.2:60000" in line
                   for line in rules.splitlines())

    def publication(side):
        path = work / side / "project/.cysynck-network/peers" / (profiles[side]["peer_id"] + ".endpoint.json")
        return json.loads(path.read_text())["metadata"]

    def encrypted_counter():
        interface = app("a", "vpn", "status")["interface"]
        rows = run(["wg", "show", interface, "transfer"], space=tag + "-a").stdout.splitlines()
        row = next(line.split() for line in rows if line.split()[0] == profiles["b"]["public_key"])
        return sum(map(int, row[1:]))

    try:
        for tool in ("wg", "wg-quick", "ip", "iptables", "miniupnpd", "ping", "openssl", "ss", "unshare", "mount"):
            require(shutil.which(tool), "Missing independent test tool: " + tool)
        version = run(["miniupnpd", "--version"], check=False)
        report["router_version"] = (version.stdout + version.stderr).splitlines()[0][:240]
        for suffix in ("a", "b", "ra", "rb"):
            space = tag + "-" + suffix
            run(["ip", "netns", "add", space])
            spaces.append(space)
            run(["ip", "link", "set", "lo", "up"], space=space)
        # Every veth endpoint is created directly inside an owned namespace.
        for side in ("a", "b"):
            run(["ip", "-n", tag + "-" + side, "link", "add", "lan", "type", "veth",
                 "peer", "name", "lan", "netns", tag + "-r" + side])
        run(["ip", "-n", tag + "-ra", "link", "add", "wan", "type", "veth",
             "peer", "name", "wan", "netns", tag + "-rb"])
        routers = {}
        profiles = {}
        for index, side in enumerate(("a", "b"), 1):
            subnet = 60 + index
            client, router = tag + "-" + side, tag + "-r" + side
            for space, device, address in ((client, "lan", f"192.168.{subnet}.2/24"),
                                           (router, "lan", f"192.168.{subnet}.1/24"),
                                           (router, "wan", f"198.18.50.{index}/30")):
                run(["ip", "address", "add", address, "dev", device], space=space)
                run(["ip", "link", "set", device, "up"], space=space)
            run(["ip", "route", "add", "default", "via", f"192.168.{subnet}.1"], space=client)
            run(["sysctl", "-w", "net.ipv4.ip_forward=1"], space=router)
            for table, chain in (("nat", "MINIUPNPD"), ("nat", "MINIUPNPD-POSTROUTING"),
                                 ("filter", "MINIUPNPD"), ("mangle", "MINIUPNPD")):
                firewall(side, "-t", table, "-N", chain)
            firewall(side, "-t", "nat", "-A", "PREROUTING", "-i", "wan", "-j", "MINIUPNPD")
            firewall(side, "-t", "nat", "-A", "POSTROUTING", "-o", "wan", "-j", "MINIUPNPD-POSTROUTING")
            firewall(side, "-t", "nat", "-A", "POSTROUTING", "-s", f"192.168.{subnet}.0/24", "-o", "wan", "-j", "MASQUERADE")
            firewall(side, "-t", "mangle", "-A", "PREROUTING", "-i", "wan", "-j", "MINIUPNPD")
            firewall(side, "-P", "FORWARD", "DROP")
            firewall(side, "-A", "FORWARD", "-m", "conntrack", "--ctstate", "ESTABLISHED,RELATED", "-j", "ACCEPT")
            firewall(side, "-A", "FORWARD", "-i", "lan", "-o", "wan", "-j", "ACCEPT")
            firewall(side, "-A", "FORWARD", "-i", "wan", "-o", "lan", "-j", "MINIUPNPD")
            folder = work / side
            (folder / "project").mkdir(parents=True)
            config = folder / "miniupnpd.conf"
            config.write_text(f"""ext_ifname=wan
ext_ip=198.18.50.{index}
ext_allow_private_ipv4=yes
listening_ip=192.168.{subnet}.1/24
enable_upnp=no
enable_natpmp=yes
ipv6_disable=yes
secure_mode=yes
system_uptime=no
min_lifetime=60
max_lifetime=600
lease_file={folder}/router.leases
uuid={uuid.uuid4()}
allow 1024-65535 192.168.{subnet}.2/32 1024-65535
deny 0-65535 0.0.0.0/0 0-65535
""", encoding="utf-8")
            routers[side] = start(["miniupnpd", "-d", "-f", config, "-P", folder / "router.pid"],
                                  router, side + "-router.log", folder / "run")
            deadline = time.monotonic() + 10
            while ":5351" not in run(["ss", "-H", "-lun"], space=router).stdout:
                require(routers[side].poll() is None and time.monotonic() < deadline,
                        "Independent miniupnpd did not bind its namespace-local PCP/NAT-PMP socket")
                time.sleep(0.1)
            # A separate legacy client reserves the desired external port for a
            # different internal UDP listener. CySynck must preserve that mapping.
            reserve = """import socket,struct,json
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.settimeout(3);s.connect((GATEWAY,5351))
s.send(struct.pack('!BBHHHI',0,1,0,60000,45000,600));r=s.recv(64)
assert len(r)==16 and r[:4]==b'\\x00\\x81\\x00\\x00', 'foreign NAT-PMP reservation refused'
assert struct.unpack('!HHI',r[8:])[:2]==(60000,45000), 'foreign external port changed'
print(json.dumps({'reserved':True}))
""".replace("GATEWAY", repr(f"192.168.{subnet}.1"))
            run([sys.executable, "-c", reserve], space=client)
            require(foreign_rule(side), "Independent foreign UDP mapping was not installed in the kernel NAT table")
            app(side, "project", "add", "Router VPN test", str(folder / "project"),
                "--id", "router-ci", "--mode", "backup", "--interval", "0")
            profiles[side] = app(side, "vpn", "configure", "NAT-" + side, f"10.77.231.{index}",
                                 "--listen-port", str(51820 + index))
            app(side, "vpn", "publish", "router-ci")
        for side, other in (("a", "b"), ("b", "a")):
            copy_publications(other, side)
            fingerprint = hashlib.sha256(base64.b64decode(profiles[other]["signing_key"], validate=True)).hexdigest()
            app(side, "vpn", "trust", "router-ci", profiles[other]["peer_id"],
                "--key", profiles[other]["public_key"], "--fingerprint", fingerprint)
        if args.prepare_apparmor:
            rules = ""
            for side in ("a", "b"):
                plan = app(side, "vpn", "apparmor")
                logical = "cs-" + hashlib.sha256(profiles[side]["peer_id"].encode()).hexdigest()[:12]
                configuration = work / side / "state/vpn" / (logical + ".conf")
                require(plan["supported"] and Path(plan["configuration"]) == configuration and
                        plan["rules"] == f'"{configuration.parent}/" r,\n"{configuration}" r,\n',
                        "AppArmor export exceeded the two exact owned configurations")
                rules += plan["rules"]
            apparmor = ScopedAppArmorReadAccess(run, tag, rules)
            report["apparmor"] = apparmor.prepare()
        for index, side in enumerate(("a", "b"), 1):
            app(side, "vpn", "up")
            result = app(side, "vpn", "router", "enable", f"192.168.{60 + index}.1",
                         "--protocol", "pcp" if side == "a" else "nat-pmp",
                         "--lifetime", "60", "--external-port", "45000")
            require(result["active"] and result["external_port"] != 45000 and result["internal_port"] == 51820 + index,
                    "Real router mapping did not preserve the foreign port or use the live WireGuard listener")
            require(result["protocol"] == ("pcp" if side == "a" else "nat-pmp") and not result["publication_errors"],
                    "Selected protocol or signed automatic publication failed")
            report.setdefault("grants", {})[side] = {key: result[key] for key in
                ("protocol", "internal_port", "external_port", "external_address", "expires_at")}
            require(foreign_rule(side), "Enabling CySynck deleted the foreign NAT mapping")
        report["checks"].append("independent-pcp-and-nat-pmp-grants-preserve-foreign-port")
        for side, other in (("a", "b"), ("b", "a")):
            copy_publications(other, side)
            refreshed = app(side, "vpn", "refresh", "router-ci")
            require(not refreshed["errors"] and any(p["resolved"]["source"] ==
                    ("signed-nat-pmp" if other == "b" else "signed-pcp") for p in refreshed["peers"]),
                    "Production signed discovery did not apply the actual router grant")
        await_ping()
        incoming_b = work / "a/project/.cysynck-network/peers" / (profiles["b"]["peer_id"] + ".endpoint.json")
        first_b_record = incoming_b.read_bytes()
        forged = json.loads(first_b_record)
        forged["metadata"]["endpoint"] = "198.18.50.254:45000"
        incoming_b.write_text(json.dumps(forged), encoding="utf-8")
        rejected = app("a", "vpn", "refresh", "router-ci")
        require(rejected["errors"] and all(p["resolved"]["endpoint"] != "198.18.50.254:45000"
                                           for p in rejected["peers"]), "A forged shared endpoint replaced the verified NAT grant")
        incoming_b.write_bytes(first_b_record)
        for side in ("a", "b"):
            require(any(h["recent"] for h in app(side, "vpn", "status")["handshakes"]), "No recent NAT WireGuard handshake")
        report["checks"].append("real-kernel-handshake-through-two-independent-nat-routers")

        # Real scheduled maintenance: no test clock or edited private lease. A
        # normal server must renew the short router grant and publish a new sequence.
        servers = {}
        before, before_sequences, due_delays = {}, {}, []
        for side in ("a", "b"):
            mapping = app(side, "vpn", "status")["router_mapping"]
            require(0 < mapping["expires_at"] - mapping["acquired_at"] <= 120,
                    "Independent router did not grant the short lease required by this bounded maintenance test")
            before[side] = mapping["acquired_at"]
            due_delays.append(max(0, mapping["renew_at"] - time.time()))
            before_sequences[side] = publication(side)["sequence"]
            servers[side] = start([cli, "--state", work / side / "state", "serve", "--host", "127.0.0.1", "--port", "8484"],
                                  tag + "-" + side, side + "-server.log")
        deadline = time.monotonic() + max(55, max(due_delays) + 25)
        renewed = set()
        while len(renewed) != 2:
            for side in ("a", "b"):
                require(servers[side].poll() is None, "Owned maintenance server exited")
                status = app(side, "vpn", "status")["router_mapping"]
                if status.get("acquired_at", 0) > before[side] and status["active"]:
                    renewed.add(side)
            require(time.monotonic() < deadline, "Core::tick did not renew the actual short router leases")
            time.sleep(0.2)
        for side in ("a", "b"):
            stop(servers[side])
            require(publication(side)["sequence"] > before_sequences[side],
                    "Actual maintenance renewed the grant without advancing its signed publication")
        report["checks"].append("real-Core-tick-renewal-and-signed-republication")
        for side, other in (("a", "b"), ("b", "a")):
            copy_publications(other, side)
            require(not app(side, "vpn", "refresh", "router-ci")["errors"], "Renewed signed endpoint was rejected")
        renewed_b_record = incoming_b.read_bytes()
        incoming_b.write_bytes(first_b_record)
        require(app("a", "vpn", "refresh", "router-ci")["errors"], "A stale NAT publication rolled back the durable signed endpoint checkpoint")
        incoming_b.write_bytes(renewed_b_record)
        await_ping()
        report["checks"].append("forged-and-stale-shared-endpoints-preserve-verified-kernel-peer")

        receiver = start([cli, "--state", work / "b/state", "chat", "serve"], tag + "-b", "receiver.log")
        deadline = time.monotonic() + 10
        while ":8485" not in run(["ss", "-H", "-ltn"], space=tag + "-b").stdout:
            require(receiver.poll() is None and time.monotonic() < deadline, "Direct receiver did not start inside the actual NAT VPN")
            time.sleep(0.1)
        delivered = app("a", "chat", "send", "router-ci", profiles["b"]["peer_id"], "Two actual NAT routers")
        require(delivered["delivered"], "Authenticated chat failed through mapped WireGuard")
        counter_before = encrypted_counter()
        rng, digest = random.Random(20261109), hashlib.sha256()
        payload = work / "a/project/router-payload.bin"
        with payload.open("xb") as stream:
            for _ in range(8):
                block = rng.randbytes(1024 * 1024)
                stream.write(block)
                digest.update(block)
        result = app("a", "send-file", "router-ci", profiles["b"]["peer_id"], payload.name)
        require(result["delivered"], "File transfer failed through mapped WireGuard")
        verified = app("b", "received", "verify", "router-ci", result["id"])
        require(verified["ok"] and verified["file"]["sha256"] == digest.hexdigest(), "Actual NAT file checksum differs")
        growth = encrypted_counter() - counter_before
        require(growth > payload.stat().st_size, "Actual NAT file did not increase the kernel's encrypted byte counters")
        report.update(payload_bytes=payload.stat().st_size, payload_sha256=digest.hexdigest(), wireguard_bytes_growth=growth)
        report["checks"].append("authenticated-chat-and-verified-file-through-mapped-wireguard")
        for side in ("a", "b"):
            disabled = app(side, "vpn", "router", "disable")
            require(not disabled["enabled"] and disabled["phase"] == "disabled", "Owned actual router mapping cleanup failed")
            require(foreign_rule(side), "Cleanup deleted the independently reserved foreign port")
            publication = work / side / "project/.cysynck-network/peers" / (profiles[side]["peer_id"] + ".endpoint.json")
            require(not publication.exists(), "Disabled mapping left its local signed endpoint publication")
        report["checks"].append("owned-router-cleanup-preserves-foreign-mapping-and-withdraws-sidecar")
        report["success"] = True
    except Exception as error:
        report["error"] = str(error)
        # Router debug logs can contain PCP nonces; keep them private and remove
        # them with the work directory. Export only bounded error lines with all
        # long hex/base64 tokens removed, never packet dumps or complete logs.
        report["child_exit_codes"] = [child.poll() for child in children]
        messages = []
        for side in ("a", "b"):
            path = work / (side + "-router.log")
            if path.exists():
                for line in path.read_text(errors="replace").splitlines():
                    if re.search(r"error|unknown|invalid|failed|permission|denied|cannot", line, re.I):
                        line = re.sub(r"[0-9a-fA-F]{24,}", "<private-token>", line)
                        line = re.sub(r"[A-Za-z0-9+/]{42,}={0,2}", "<private-token>", line)
                        messages.append(line[:400])
        report["router_errors"] = messages[-8:]
    finally:
        for child in reversed(children):
            try:
                stop(child)
            except Exception:
                report["success"] = False
                report.setdefault("cleanup_errors", []).append("Owned test process did not stop")
        for output in streams:
            output.close()
        for space in reversed(spaces):
            require(re.fullmatch(re.escape(tag) + r"-(a|b|ra|rb)", space), "Refuse foreign namespace cleanup")
            deleted = run(["ip", "netns", "delete", space], check=False)
            if deleted.returncode:
                report["success"] = False
                report.setdefault("cleanup_errors", []).append("Owned namespace deletion failed")
        if apparmor:
            try:
                apparmor.close()
                if report.get("apparmor", {}).get("prepared"):
                    report["apparmor"]["restored"] = True
            except Exception as error:
                report["success"] = False
                report.setdefault("cleanup_errors", []).append(str(error))
        require(work.parent == parent and work.name.startswith("cysynck-wg-nat-") and not work.is_symlink(), "Refuse foreign work cleanup")
        shutil.rmtree(work)
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        args.report.chmod(0o644)
    print(json.dumps(report))
    return 0 if report["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
