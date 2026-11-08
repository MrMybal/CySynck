"""Actual WireGuardNT/service ownership gate, exclusively for disposable Windows CI.

No peer or default route is installed. Never run this on a user's workstation.
Only the fresh profile's exact tunnel service/adapter is controlled. Reports omit
keys, credentials, configuration contents and raw command output.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--disposable-host', action='store_true')
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--direct-probe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    require(os.name == 'nt' and args.disposable_host and os.environ.get('GITHUB_ACTIONS') == 'true',
            'This actual driver gate requires an explicitly disposable Windows CI host')
    require(ctypes.windll.shell32.IsUserAnAdmin(), 'The disposable driver gate requires elevation')
    cli, probe, work = args.cli.resolve(strict=True), args.direct_probe.resolve(strict=True), args.work.absolute()
    require(work.is_absolute() and not work.exists() and work.name.startswith('cysynck-wg-win-'),
            'Use a fresh reserved absolute work directory')
    parent = work.parent.resolve(strict=True)
    require(work.parent == parent and not parent.is_symlink(), 'Refuse a redirected work directory')
    wg = Path(os.environ['ProgramFiles']) / 'WireGuard/wg.exe'
    activation = wg.with_name('wireguard.exe')
    require(wg.is_file() and activation.is_file(), 'Install the verified official WireGuard tools first')
    environment = dict(os.environ, CYSYNCK_WG=str(wg), CYSYNCK_WIREGUARD=str(activation))
    environment.pop('CYSYNCK_WG_RUNTIME_DIRECTORY', None)
    state = work / 'profil été 世界'
    report = {'success': False, 'driver': 'official WireGuard Windows 1.1.1 / WireGuardNT', 'checks': []}
    logical = None
    dummy_service = False
    active = False
    cleanup_ok = True

    def run(command, *, input=None, check=True, timeout=60):
        result = subprocess.run(list(map(str, command)), input=input, capture_output=True,
                                text=True, encoding='utf-8', errors='replace', env=environment,
                                timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
        if check:
            # Deliberately no stdout/stderr: tool diagnostics may include credentials.
            require(result.returncode == 0, 'A disposable driver command failed: ' + Path(command[0]).name)
        return result

    def app(*command, check=True):
        result = run([cli, '--state', state, *command], check=check)
        return json.loads(result.stdout) if check else result

    def ps(script):
        return run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', script]).stdout.strip()

    def service_present():
        return run(['sc.exe', 'query', 'WireGuardTunnel$' + logical], check=False).returncode == 0

    try:
        work.mkdir()
        require(ps("@(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction Stop | Where-Object IPAddress -eq '10.77.249.1').Count") == '0',
                'The reserved test address is already used')
        profile = app('vpn', 'configure', 'Disposable Windows driver gate', '10.77.249.1', '--listen-port', '51829')
        logical = 'cs-' + hashlib.sha256(profile['peer_id'].encode()).hexdigest()[:12]
        require(re.fullmatch(r'cs-[0-9a-f]{12}', logical) and not service_present(),
                'A foreign service already owns the reserved profile name')
        # Stopped service with the expected name but a foreign executable/configuration.
        dummy_path = str(Path(os.environ['SystemRoot']) / 'System32/cmd.exe') + ' /c exit 0'
        run(['sc.exe', 'create', 'WireGuardTunnel$' + logical, 'binPath=', dummy_path, 'start=', 'demand'])
        dummy_service = True
        for action in ('up', 'down'):
            require(app('vpn', action, check=False).returncode != 0,
                    'Tunnel control accepted a foreign service configuration')
            require(service_present(), 'Tunnel control removed the foreign service fixture')
        run(['sc.exe', 'delete', 'WireGuardTunnel$' + logical])
        dummy_service = False
        report['checks'].append('real-SCM-foreign-service-preserved')
        app('vpn', 'up')
        active = True
        deadline = time.monotonic() + 30
        while True:
            status = app('vpn', 'status')
            tunnel = status.get('tunnel') or {}
            if tunnel.get('owned') and tunnel.get('present'):
                break
            require(time.monotonic() < deadline, 'The owned WireGuardNT service did not become ready')
            time.sleep(0.3)
        require(service_present(), 'The owned tunnel service is missing')
        run([probe, '--probe', profile['address'], profile['peer_id']])
        report['checks'].extend(['real-WireGuardNT-service-and-Unicode-profile',
                                 'actual-WireGuardNT-adapter-and-TCP-interface-binding'])
        configuration = state / 'vpn' / (logical + '.conf')
        before = hashlib.sha256(configuration.read_bytes()).hexdigest()
        foreign_file = state / 'vpn/foreign-fixture.key'
        private = run([wg, 'genkey']).stdout.strip()
        foreign_file.write_text(private + '\n', encoding='ascii')
        public = run([wg, 'pubkey'], input=private + '\n').stdout.strip()
        del private
        run([wg, 'set', logical, 'private-key', foreign_file])
        foreign_file.unlink()
        for action in ('up', 'down'):
            require(app('vpn', action, check=False).returncode != 0,
                    'Tunnel control accepted a foreign live WireGuard key')
            require(run([wg, 'show', logical, 'public-key']).stdout.strip() == public,
                    'Tunnel control replaced the foreign live key')
            require(service_present() and hashlib.sha256(configuration.read_bytes()).hexdigest() == before,
                    'Tunnel control modified foreign service/configuration state')
        report['checks'].append('real-WireGuardNT-foreign-key-preserved')
        run([wg, 'set', logical, 'private-key', state / 'vpn/private.key'])
        app('vpn', 'down')
        active = False
        require(not service_present(), 'Owned service cleanup left its service installed')
        require(run([wg, 'show', logical, 'public-key'], check=False).returncode != 0,
                'Owned service cleanup left its WireGuardNT adapter active')
        report['checks'].append('verified-owned-service-and-adapter-removal')
        report['success'] = True
    except Exception as error:
        report['error'] = str(error)
    finally:
        try:
            if dummy_service:
                run(['sc.exe', 'delete', 'WireGuardTunnel$' + logical])
            if active:
                # Restore only this test's known key, then let production ownership
                # checks authorize removal. Never uninstall an unverified service.
                run([wg, 'set', logical, 'private-key', state / 'vpn/private.key'])
                app('vpn', 'down')
            if logical:
                require(not service_present(), 'The disposable tunnel service remains installed')
        except Exception:
            cleanup_ok = False
            report['cleanup_error'] = 'Owned tunnel cleanup failed; private fixture retained for the disposable host teardown'
            report['success'] = False
        if cleanup_ok and work.exists():
            require(work.parent.resolve() == parent and work.name.startswith('cysynck-wg-win-') and
                    not work.is_symlink(), 'Refuse an unsafe recursive work cleanup')
            shutil.rmtree(work)
        report['owned_cleanup_complete'] = cleanup_ok
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))
    return 0 if report['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
