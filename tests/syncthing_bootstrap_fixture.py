"""Public VPN profile transport through two real, isolated managed engines."""
import hashlib
import json
import subprocess
import sys
import time


def require(value, message):
    if not value:
        raise RuntimeError(message)


class SyncthingBootstrapFixture:
    def __init__(self, executable, cli, work, spaces, children, environment, run, app):
        self.executable, self.cli, self.work, self.spaces = executable, cli, work, spaces
        self.children, self.environment, self.run, self.app = children, environment, run, app
        self.devices = {}

    def api(self, side, method, endpoint, data=None):
        # The engine key is read only inside the owned namespace. It never appears
        # in argv, public status, a report or a captured stdout/stderr message.
        program = """import json,sys,urllib.request
from pathlib import Path
key=(Path(sys.argv[1])/'engine/api-key').read_text()
body=None if sys.argv[4]=='null' else sys.argv[4].encode()
r=urllib.request.Request('http://127.0.0.1:8385/rest/'+sys.argv[3],data=body,method=sys.argv[2],headers={'X-API-Key':key,'Content-Type':'application/json'})
with urllib.request.urlopen(r,timeout=5) as response:
 value=response.read();print(value.decode() if value else '{}')
"""
        result = self.run([sys.executable, '-c', program, self.work / side / 'state', method,
                           endpoint, json.dumps(data)], namespace=self.spaces[0 if side == 'a' else 1])
        return json.loads(result.stdout)

    def start(self):
        for index, side in enumerate(('a', 'b')):
            self.app(side, 'engine', 'configure', '--executable', str(self.executable),
                     '--port', '8385', '--listen-port', '22001')
            child = subprocess.Popen(['ip', 'netns', 'exec', self.spaces[index], str(self.cli),
                                      '--state', str(self.work / side / 'state'), 'engine', 'run'],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                     env=self.environment)
            self.children.append(child)
        deadline = time.monotonic() + 40
        for side in ('a', 'b'):
            while True:
                state = self.app(side, 'engine', 'status')
                if state.get('running'):
                    self.devices[side] = state['device_id']
                    break
                require(time.monotonic() < deadline, 'The actual managed Syncthing engines did not start')
                time.sleep(0.2)
            # Namespace LAN has no external route. Disable discovery/relay/NAT
            # explicitly too; the test uses only the other reserved static LAN IP.
            self.api(side, 'PATCH', 'config/options', {
                'globalAnnounceEnabled': False, 'localAnnounceEnabled': False,
                'relaysEnabled': False, 'natEnabled': False,
                'minHomeDiskFree': {'value': 128, 'unit': 'MB'}})
            self.api(side, 'PATCH', 'config/defaults/folder', {'minDiskFree': {'value': 128, 'unit': 'MB'}})
        require(self.devices['a'] != self.devices['b'], 'Managed engine identities are not independent')
        for side, other in (('a', 'b'), ('b', 'a')):
            index = 1 if other == 'a' else 2
            self.app(side, 'engine', 'add-device', self.devices[other],
                     '--address', f'tcp://192.168.239.{index}:22001')
            self.app(side, 'register', 'wireguard-ci')
            self.app(side, 'engine', 'share', 'wireguard-ci', self.devices[other])

    def exchange(self, profiles):
        for side in ('a', 'b'):
            self.app(side, 'sync', 'wireguard-ci')
        deadline = time.monotonic() + 60
        hashes = {}
        for side, other in (('a', 'b'), ('b', 'a')):
            name = profiles[other]['peer_id'] + '.json'
            source = self.work / other / 'project/.cysynck-network/peers' / name
            target = self.work / side / 'project/.cysynck-network/peers' / name
            expected = source.read_bytes()
            while not target.is_file() or target.read_bytes() != expected:
                require(time.monotonic() < deadline, 'The public VPN profile did not arrive through real Syncthing')
                time.sleep(0.2)
            hashes[side] = hashlib.sha256(target.read_bytes()).hexdigest()
            require(profiles[other]['device_id'] == self.devices[other] and profiles[other]['endpoint'] == '',
                    'The synced profile does not bind the actual engine identity with an automatic endpoint')
        return {'transport': 'real managed Syncthing over isolated static LAN TCP',
                'public_profile_sha256': hashes, 'manual_profile_copy': False}

    def verify_resolution(self, profiles):
        for side, other in (('a', 'b'), ('b', 'a')):
            deadline = time.monotonic() + 20
            while True:
                result = self.app(side, 'vpn', 'refresh', 'wireguard-ci')
                require(not result['errors'] and not result['tunnel_errors'], 'Actual Syncthing endpoint refresh failed')
                resolved = next(p['resolved'] for p in result['peers'] if p['peer'] == profiles[other]['peer_id'])
                if resolved['source'] == 'syncthing-direct-ip':
                    break
                require(time.monotonic() < deadline, 'A real connected engine did not supply its LAN hint')
                time.sleep(0.2)
            index = 1 if other == 'a' else 2
            require(resolved['endpoint'] == f"192.168.239.{index}:{profiles[other]['listen_port']}" and
                    not resolved['endpoint'].endswith(':22001'),
                    'The actual TCP sync port replaced the pinned WireGuard UDP listener')
