"""Disposable native macOS WireGuard userspace/utun and unprivileged socket gate."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import time


def require(value, message):
    if not value:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--disposable-host', action='store_true')
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--direct-probe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    require(sys.platform == 'darwin' and args.disposable_host and os.environ.get('GITHUB_ACTIONS') == 'true' and os.geteuid() == 0,
            'Use an explicitly disposable elevated macOS CI host')
    cli, probe, work = args.cli.resolve(strict=True), args.direct_probe.resolve(strict=True), args.work.absolute()
    require(not work.exists() and work.name.startswith('cysynck-wg-mac-'), 'Use a fresh reserved work directory')
    parent = work.parent.resolve(strict=True)
    require(work.parent == parent and not parent.is_symlink(), 'Refuse redirected work directory')
    wg, quick = shutil.which('wg'), shutil.which('wg-quick')
    require(wg and quick and shutil.which('wireguard-go'), 'Install official WireGuard tools and wireguard-go first')
    environment = dict(os.environ, CYSYNCK_WG=wg, CYSYNCK_WG_QUICK=quick)
    environment.pop('CYSYNCK_WG_RUNTIME_DIRECTORY', None)
    uid = int(os.environ['SUDO_UID'])
    require(uid != 0, 'The real data probe needs the preparing ordinary user')
    user = subprocess.check_output(['id', '-un', str(uid)], text=True).strip()
    state = work / 'profil été 世界'
    logical = None
    profile = None
    active = False
    receipt = None
    saved_receipt = None
    report = {'success': False, 'driver': 'official wireguard-go / macOS utun', 'checks': []}

    def run(command, *, input=None, check=True, timeout=60):
        result = subprocess.run(list(map(str, command)), input=input, capture_output=True, text=True,
                                encoding='utf-8', errors='replace', env=environment, timeout=timeout)
        if check:
            require(result.returncode == 0, 'A disposable macOS driver command failed: ' + Path(command[0]).name)
        return result

    def app(*command, check=True):
        result = run([cli, '--state', state, *command], check=check)
        return json.loads(result.stdout) if check else result

    def unprivileged_probe(check=True):
        return run(['sudo', '-u', user, '--', probe, '--probe', profile['address'], profile['peer_id'], profile['public_key']], check=check)

    try:
        work.mkdir(mode=0o700)
        require('10.77.249.2 ' not in run(['ifconfig', '-a']).stdout, 'Reserved macOS test address already used')
        profile = app('vpn', 'configure', 'Disposable macOS driver gate', '10.77.249.2', '--listen-port', '51829')
        logical = 'cs-' + hashlib.sha256(profile['peer_id'].encode()).hexdigest()[:12]
        receipt = Path('/var/run/wireguard') / (logical + '.cysynck-direct.json')
        require(not receipt.exists() and not receipt.is_symlink(), 'A foreign root authorization occupies the test name')
        active = True
        app('vpn', 'up')
        status = app('vpn', 'status')['tunnel']
        require(status['owned'] and status['present'] and re.fullmatch(r'utun[0-9]{1,5}', status['runtime_interface']),
                'The owned userspace tunnel was not mapped to utun')
        runtime = status['runtime_interface']
        report['checks'].append('real-wg-quick-Unicode-profile-and-verified-utun-mapping')
        info = receipt.stat()
        require(info.st_uid == 0 and stat.S_IMODE(info.st_mode) == 0o644, 'Public authorization permissions differ')
        unprivileged_probe()
        report['checks'].append('unprivileged-actual-utun-interface-bound-TCP-socket')
        original = receipt.read_bytes()
        saved_receipt = original
        proof = json.loads(original)
        for field in ('boot_uuid', 'socket_inode', 'index'):
            stale = dict(proof)
            stale[field] = '00000000-0000-0000-0000-000000000000' if field == 'boot_uuid' else stale[field] + 1
            receipt.write_text(json.dumps(stale), encoding='utf-8')
            require(unprivileged_probe(check=False).returncode != 0, 'An obsolete macOS interface generation was accepted')
        receipt.write_bytes(original)
        require(run(['sudo', '-u', user, '--', probe, '--probe', profile['address'], profile['peer_id'], 'A' * 43 + '='], check=False).returncode != 0,
                'A different profile public identity adopted the root authorization')
        unprivileged_probe()
        report['checks'].append('stale-boot-socket-index-and-changed-key-authorization-refused')
        configuration = state / 'vpn' / (logical + '.conf')
        before = hashlib.sha256(configuration.read_bytes()).hexdigest()
        foreign = state / 'vpn/foreign-fixture.key'
        private = run([wg, 'genkey']).stdout.strip()
        foreign.write_text(private + '\n', encoding='ascii')
        public = run([wg, 'pubkey'], input=private + '\n').stdout.strip()
        del private
        run([wg, 'set', runtime, 'private-key', foreign])
        foreign.unlink()
        for action in ('up', 'down'):
            require(app('vpn', action, check=False).returncode != 0, 'Tunnel control accepted a foreign macOS WireGuard key')
            require(run([wg, 'show', runtime, 'public-key']).stdout.strip() == public and
                    hashlib.sha256(configuration.read_bytes()).hexdigest() == before,
                    'Foreign macOS tunnel/configuration changed')
        report['checks'].append('foreign-live-WireGuard-identity-preserved')
        run([wg, 'set', runtime, 'private-key', state / 'vpn/private.key'])
        app('vpn', 'down')
        active = False
        deadline = time.monotonic() + 10
        while run(['ifconfig', runtime], check=False).returncode == 0:
            require(time.monotonic() < deadline, 'Owned utun did not disappear')
            time.sleep(0.2)
        require(not receipt.exists(), 'Owned authorization was not removed')
        require(unprivileged_probe(check=False).returncode != 0, 'A removed tunnel was still authorized')
        report['checks'].append('owned-utun-and-authorization-removal')
        report['success'] = True
    except Exception as error:
        report['error'] = str(error)
    finally:
        cleanup = True
        try:
            if active:
                if saved_receipt is not None:
                    receipt.write_bytes(saved_receipt)
                status = app('vpn', 'status')['tunnel']
                runtime = status.get('runtime_interface')
                if runtime:
                    run([wg, 'set', runtime, 'private-key', state / 'vpn/private.key'])
                app('vpn', 'down')
            if receipt is not None:
                require(not receipt.exists(), 'Owned authorization remains')
        except Exception:
            cleanup = False
            report['success'] = False
            report['cleanup_error'] = 'Owned macOS tunnel cleanup failed; fixture retained for disposable host teardown'
        if cleanup and work.exists():
            require(work.parent.resolve() == parent and not work.is_symlink() and work.name.startswith('cysynck-wg-mac-'),
                    'Refuse unsafe work cleanup')
            shutil.rmtree(work)
        report['owned_cleanup_complete'] = cleanup
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))
    return 0 if report['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
