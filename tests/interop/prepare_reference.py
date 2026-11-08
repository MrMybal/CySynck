"""Copy the tested CyRevision projects into an owned test directory and apply an LF compatibility patch.

The reference checkout is read-only. Its AGPL-3.0 license and copyright notices are retained.
No copied reference implementation is installed or linked into CySynck distributions.
"""
import argparse
import difflib
from pathlib import Path
import shutil
import uuid

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reference', type=Path, required=True)
parser.add_argument('--work-parent', type=Path, required=True)
args = parser.parse_args()
reference = args.reference.resolve()
target = args.work_parent.resolve() / ('cyrevision-lf-reference-' + uuid.uuid4().hex)
target.mkdir(parents=True)
for name in ('LICENSE', 'Directory.Build.props', 'Directory.Build.targets', 'global.json'):
    if (reference / name).is_file(): shutil.copyfile(reference / name, target / name)
for name in ('CyRevision.Core', 'CyRevision.Security', 'CyRevision.Git'):
    shutil.copytree(reference / 'src' / name, target / 'src' / name,
                    ignore=shutil.ignore_patterns('bin', 'obj', '.vs'))
relative = 'src/CyRevision.Git/GitPeerExchangeService.cs'
source = target / relative
original = source.read_text(encoding='utf-8-sig')
patched = original.replace('using System.Text.Json;', 'using System.Text;\nusing System.Text.Json;', 1)
sites = (
    ('identity.Identity', 'item.Manifest', 'item.Signature'),
    ('authorizedDevice', 'manifest', 'signed.Signature'),
    ('device', 'inventory', 'signed!.Signature'),
    ('requester', 'request', 'signed!.Signature'))
for identity, value, signature in sites:
    old = f'PeerExchangeCodec.VerifySignature({identity}, CanonicalBytes({value}), {signature})'
    assert patched.count(old) == 1, f'Reference signature site changed: {value}'
    patched = patched.replace(old, f'VerifyCanonicalSignature({identity}, {value}, {signature})')
old = '    private static byte[] CanonicalBytes<T>(T value) => JsonSerializer.SerializeToUtf8Bytes(value, JsonOptions);'
assert patched.count(old) == 1, 'Reference canonical serializer changed'
new = '''    private static byte[] CanonicalBytes<T>(T value) => Encoding.UTF8.GetBytes(
        JsonSerializer.Serialize(value, JsonOptions).Replace("\\r\\n", "\\n", StringComparison.Ordinal));

    private static bool VerifyCanonicalSignature<T>(DeviceIdentity identity, T value, string signature)
    {
        byte[] canonical = CanonicalBytes(value);
        if (PeerExchangeCodec.VerifySignature(identity, canonical, signature)) return true;
        // Preserve signatures already produced by the original Windows indented serializer.
        byte[] legacy = Encoding.UTF8.GetBytes(Encoding.UTF8.GetString(canonical)
            .Replace("\\n", "\\r\\n", StringComparison.Ordinal));
        return PeerExchangeCodec.VerifySignature(identity, legacy, signature);
    }'''
patched = patched.replace(old, new)
source.write_text(patched, encoding='utf-8', newline='\n')
patch = ''.join(difflib.unified_diff(original.splitlines(True), patched.splitlines(True),
             fromfile='a/' + relative, tofile='b/' + relative))
(target / 'cyrevision-canonical-lf.patch').write_text(patch, encoding='utf-8', newline='\n')
print(target)
