"""Run real CyRevision .NET <-> CySynck C++ Git/LFS exchanges in a new owned directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import uuid

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cysynck', required=True, type=Path)
parser.add_argument('--harness', required=True, type=Path)
parser.add_argument('--dotnet', default='dotnet')
parser.add_argument('--work-parent', required=True, type=Path)
parser.add_argument('--signature-format', choices=['lf', 'windows-legacy'], default='windows-legacy')
args = parser.parse_args()
env = {k.upper(): v for k, v in os.environ.items()}
work = args.work_parent.resolve() / ('cyrevision-roundtrip-' + uuid.uuid4().hex)
work.mkdir(parents=True)
env.update(GIT_AUTHOR_NAME='CySynck isolated test', GIT_AUTHOR_EMAIL='test@example.invalid',
           GIT_COMMITTER_NAME='CySynck isolated test', GIT_COMMITTER_EMAIL='test@example.invalid',
           GIT_LFS_SKIP_SMUDGE='1', DOTNET_CLI_HOME=str(work / 'dotnet-home'), DOTNET_NOLOGO='1',
           DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1')

def run(command, cwd=None):
    result = subprocess.run([str(x) for x in command], cwd=cwd, env=env, capture_output=True)
    if result.returncode:
        raise RuntimeError(f'{command[0:2]} failed: {result.stderr.decode("utf-8",errors="replace")}')
    return result.stdout

def git(repo, *command):
    return run(['git', '-C', repo, *command]).decode('utf-8').strip()

def init(name):
    repo = work / name
    repo.mkdir()
    git(repo, 'init', '--initial-branch=main')
    git(repo, 'config', 'core.autocrlf', 'false')
    return repo

def native(state, *command):
    return json.loads(run([args.cysynck.resolve(), '--state', state, *command]))

def reference(*command):
    return json.loads(run([args.dotnet, args.harness.resolve(), *command]))

def object_path(repo, digest):
    return repo / '.git/lfs/objects' / digest[:2] / digest[2:4] / digest

def create_asset(repo, data):
    digest = hashlib.sha256(data).hexdigest()
    pointer = f'version https://git-lfs.github.com/spec/v1\noid sha256:{digest}\nsize {len(data)}\n'
    (repo / '.gitattributes').write_text('*.bin filter=lfs diff=lfs merge=lfs -text\n', encoding='utf-8')
    (repo / 'asset.bin').write_bytes(pointer.encode())
    obj = object_path(repo, digest)
    obj.parent.mkdir(parents=True, exist_ok=True)
    obj.write_bytes(data)
    git(repo, '-c', 'filter.lfs.clean=cat', '-c', 'filter.lfs.required=false', 'add', '.')
    git(repo, '-c', 'core.hooksPath=', 'commit', '-m', 'Isolated signed Git/LFS fixture')
    return digest

try:
    source = init('reference-source')
    receiver = init('native-receiver')
    back = init('reference-receiver')
    exchange = work / 'exchange'; exchange.mkdir()
    state = work / 'native-profile'
    project_guid = str(uuid.uuid4())
    payload = bytes(range(256)) * 8193 + 'é direct asset\n'.encode()
    digest = create_asset(source, payload)
    (source / 'note.txt').write_text('Real .NET publication\n', encoding='utf-8')
    git(source, 'add', 'note.txt'); git(source, '-c', 'core.hooksPath=', 'commit', '-m', 'Text')
    git(source, 'branch', 'feature/é+test')
    publication = reference('export', source, exchange, project_guid, work / 'reference-identity', 'Reference é + &')
    assert publication['result']['transactionId'], publication
    author = publication['identity']
    author_file = work / 'reference-public-identity.json'
    author_file.write_text(json.dumps(author), encoding='utf-8')
    import base64
    fingerprint = hashlib.sha256(base64.b64decode(author['signingPublicKey'])).hexdigest()
    project = native(state, 'project', 'add', 'Reference transport', receiver, '--mode', 'git-bundles', '--exchange-path', exchange)
    pid = project['id']
    native(state, 'cyrevision', 'configure', pid, project_guid, '--signature-format', args.signature_format)
    native(state, 'cyrevision', 'trust', pid, author_file, '--fingerprint', fingerprint)
    incoming = native(state, 'cyrevision', 'inbox', pid)
    assert len(incoming['publications']) == 1, incoming
    result = native(state, 'cyrevision', 'import', pid, publication['result']['transactionId'])
    assert result['imported'] and result['lfs_copied'] == 1 and not result['lfs_deferred'], result
    assert object_path(receiver, digest).read_bytes() == payload
    assert not (receiver / 'note.txt').exists() and not (receiver / 'asset.bin').exists()
    peer_ref = f'refs/remotes/cysynck/cr-{author["deviceId"].replace("-", "")}/main'
    assert git(receiver, 'rev-parse', peer_ref) == git(source, 'rev-parse', 'main')
    assert native(state, 'cyrevision', 'import', pid, publication['result']['transactionId'])['already_imported']
    # Check out only inside this disposable fixture to author the return publication.
    git(receiver, '-c', 'filter.lfs.required=false', 'checkout', '-B', 'main', peer_ref)
    (receiver / 'note.txt').write_text('Native C++ return publication\n', encoding='utf-8')
    git(receiver, 'add', 'note.txt'); git(receiver, '-c', 'core.hooksPath=', 'commit', '-m', 'Native return')
    own = native(state, 'cyrevision', 'identity', '--name', 'Native é + &')
    native_public = work / 'native-public-identities.json'
    native_public.write_text(json.dumps([own['identity']]), encoding='utf-8')
    outgoing = native(state, 'cyrevision', 'export', pid)
    tx = outgoing['manifest']['transactionId'].replace('-', '')
    canonical = reference('canonical', exchange / 'transactions' / tx / 'transaction.json')
    assert canonical['verified'], canonical
    imported = reference('import', back, exchange, project_guid, work / 'reference-state', native_public)
    assert imported['importedTransactions'] == 1, imported
    back_ref = f'refs/remotes/cyrevision/{own["identity"]["deviceId"].replace("-", "")[:12]}/main'
    assert git(back, 'rev-parse', back_ref) == git(receiver, 'rev-parse', 'main')
    assert object_path(back, digest).read_bytes() == payload
    assert not (back / 'note.txt').exists() and not (back / 'asset.bin').exists()
    report = {'ok': True, 'referenceToNative': result, 'nativeToReference': imported,
              'assetBytes': len(payload), 'assetSha256': digest, 'workDirectory': str(work)}
    (work / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
except Exception:
    print(f'Isolated interoperation fixture preserved: {work}', flush=True)
    raise
