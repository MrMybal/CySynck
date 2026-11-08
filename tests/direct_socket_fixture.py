"""Driver-test helper: count attempted plaintext, never store credentials/body."""
import hashlib
import json
import selectors
import subprocess
import sys
import time


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


class DirectSocketFixture:
    def __init__(self, cli, work, spaces, profiles, children, run, app):
        self.cli, self.work, self.spaces = cli, work, spaces
        self.profiles, self.children, self.run, self.app = profiles, children, run, app
        self.count = 0

    def listener(self, device, echo=False):
        self.count += 1
        counter = self.work / ("direct-count-" + str(self.count) + ".json")
        program = """import json,socket,sys
from pathlib import Path
counter=Path(sys.argv[1])
def store(connections,total):
 temporary=counter.with_suffix('.tmp');temporary.write_text(json.dumps({'connections':connections,'bytes':total}));temporary.replace(counter)
store(0,0)
s=socket.socket(socket.AF_INET,socket.SOCK_STREAM);s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
s.setsockopt(socket.SOL_SOCKET,socket.SO_BINDTODEVICE,sys.argv[2].encode()+b'\\0')
s.bind((sys.argv[3],int(sys.argv[4])));s.listen(4)
print('ready',flush=True);connections=0;total=0
while True:
 c,a=s.accept();connections+=1;c.settimeout(2)
 store(connections,total)
 try:
  data=c.recv(16384)
  if sys.argv[5]=='echo':
   while len(data)<len(b'CySynck direct probe'):
    part=c.recv(128)
    if not part:break
    data+=part
  total+=len(data)
  c.sendall(data if sys.argv[5]=='echo' else b'HTTP/1.1 403 Forbidden\\r\\nContent-Length: 0\\r\\nConnection: close\\r\\n\\r\\n')
 except (TimeoutError,OSError):pass
 store(connections,total);c.close()
"""
        child = subprocess.Popen(["ip", "netns", "exec", self.spaces[1], sys.executable, "-c", program,
                                  str(counter), device, self.profiles["b"]["address"],
                                  "8486" if echo else "8485", "echo" if echo else "http"],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.children.append(child)
        selector = selectors.DefaultSelector()
        selector.register(child.stdout, selectors.EVENT_READ)
        ready = selector.select(timeout=5)
        check(ready and child.stdout.readline().strip() == "ready", "Owned direct transport listener did not start")
        selector.close()
        return child, counter

    @staticmethod
    def stop(child):
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=5)

    def no_plaintext(self, device, all_senders=True):
        child, counter = self.listener(device)
        try:
            commands = [("chat", "send", "wireguard-ci", self.profiles["b"]["peer_id"], "Guard plaintext probe")]
            if all_senders:
                (self.work / "a/project/guard.txt").write_text("Direct guard frozen bytes", encoding="utf-8")
                commands += [("send-file", "wireguard-ci", self.profiles["b"]["peer_id"], "guard.txt", "--legacy", "true"),
                             ("send-file", "wireguard-ci", self.profiles["b"]["peer_id"], "guard.txt")]
            for command in commands:
                result = self.run([self.cli, "--state", self.work / "a/state", *command],
                                  namespace=self.spaces[0], check=False, timeout=20)
                if result.returncode == 0:
                    check(not json.loads(result.stdout).get("delivered", False), "A plaintext destination received the direct operation")
            time.sleep(0.1)
            observed = json.loads(counter.read_text())
            check(observed == {"connections": 0, "bytes": 0}, "A direct operation connected or sent credentials/payload over a plain veth")
        finally:
            self.stop(child)

    def plain_interfaces(self):
        logical = {side: "cs-" + hashlib.sha256(self.profiles[side]["peer_id"].encode()).hexdigest()[:12]
                   for side in ("a", "b")}
        for index, side in enumerate(("a", "b")):
            address = self.profiles[side]["address"]
            other = self.profiles["b" if side == "a" else "a"]["address"]
            self.run(["ip", "address", "add", address + "/32", "dev", "lan-" + side], namespace=self.spaces[index])
            self.run(["ip", "route", "add", other + "/32", "dev", "lan-" + side], namespace=self.spaces[index])
        for renamed in (False, True):
            if renamed:
                for index, side in enumerate(("a", "b")):
                    self.run(["ip", "link", "set", "lan-" + side, "name", logical[side]], namespace=self.spaces[index])
            # Check the real receiver before the spy occupies its port. A successful
            # unsafe bind would block here and is killed by subprocess.run's deadline.
            rejected = self.run([self.cli, "--state", self.work / "b/state", "chat", "serve"],
                                namespace=self.spaces[1], check=False, timeout=3)
            check(rejected.returncode != 0, "Direct receiver accepted an ordinary interface as WireGuard")
            self.no_plaintext(logical["b"] if renamed else "lan-b")
        for index, side in enumerate(("a", "b")):
            other = self.profiles["b" if side == "a" else "a"]["address"]
            self.run(["ip", "link", "set", logical[side], "name", "lan-" + side], namespace=self.spaces[index])
            self.run(["ip", "route", "del", other + "/32", "dev", "lan-" + side], namespace=self.spaces[index])
            self.run(["ip", "address", "del", self.profiles[side]["address"] + "/32", "dev", "lan-" + side],
                     namespace=self.spaces[index])

    def overlapping_routes(self, interfaces):
        try:
            for index, side in enumerate(("a", "b")):
                other = self.profiles["b" if side == "a" else "a"]["address"]
                self.run(["ip", "route", "replace", other + "/32", "dev", "lan-" + side,
                          "src", self.profiles[side]["address"]], namespace=self.spaces[index])
            self.no_plaintext("lan-b", all_senders=False)
        finally:
            for index, side in enumerate(("a", "b")):
                other = self.profiles["b" if side == "a" else "a"]["address"]
                self.run(["ip", "route", "replace", other + "/32", "dev", interfaces[side]], namespace=self.spaces[index])

    def unprivileged(self, probe, uid, gid, interfaces):
        check(uid != 0 and probe.is_file(), "Use the preparing unprivileged account and compiled direct socket probe")
        child, counter = self.listener(interfaces["b"], echo=True)
        try:
            result = self.run(["setpriv", "--reuid", str(uid), "--regid", str(gid), "--clear-groups",
                               probe, self.profiles["a"]["address"], self.profiles["a"]["peer_id"],
                               self.profiles["b"]["address"], "8486"], namespace=self.spaces[0])
            check("PASS actual interface-bound TCP" in result.stdout, "The unprivileged production binding probe did not finish")
            deadline = time.monotonic() + 2
            observed = json.loads(counter.read_text())
            while observed["bytes"] < len("CySynck direct probe") and time.monotonic() < deadline:
                time.sleep(0.01)
                observed = json.loads(counter.read_text())
            check(observed["connections"] == 1 and observed["bytes"] == len("CySynck direct probe"),
                  "The unprivileged bound socket did not exchange the actual kernel peer bytes")
        finally:
            self.stop(child)
