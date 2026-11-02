#!/usr/bin/env python3
"""Check shipped bytes, SDK headers and reconstruct the distributed source port."""
import hashlib
import io
import json
import pathlib
import subprocess
import tarfile
import tempfile
import zipfile

root = pathlib.Path(__file__).resolve().parents[1]
dist = root / 'dist'
for line in (dist / 'SHA256SUMS').read_text().splitlines():
    digest, name = line.split('  ', 1)
    assert hashlib.sha256((dist / name).read_bytes()).hexdigest() == digest, name
manifest = json.loads((dist / 'manifest.json').read_text())
assert manifest['java25_runtime'] == 'NOT IMPLEMENTED'
for artifact in manifest['artifacts']:
    assert hashlib.sha256((dist / artifact['name']).read_bytes()).hexdigest() == artifact['sha256']
with tarfile.open(dist / 'javaswitch-horizon-sdk.tar.gz') as sdk:
    assert sdk.extractfile('include/javaswitch/runtime.h').read() == (root / 'include/javaswitch/runtime.h').read_bytes()
    library = sdk.extractfile('lib/libjavaswitch.a').read()
    assert library == (root / 'build/sdk/libjavaswitch.a').read_bytes()
    assert 'licenses/LIBFFI-MIT.txt' in sdk.getnames()
    print('SDK ABI v1 header/library and notices: PASS')
    print('libjavaswitch.a SHA256 ' + hashlib.sha256(library).hexdigest())
with zipfile.ZipFile(dist / 'java-switch-test-package.zip') as sd:
    for name in ['java-test', 'ffi-test', 'zero-services-test']:
        path = dist / (name + '.nro')
        if path.exists():
            assert sd.read(f'switch/{name}/{name}.nro') == path.read_bytes()
    assert sd.read('java-switch-source.tar.gz') == (dist / 'java-switch-source.tar.gz').read_bytes()
    assert 'switch/java25-test/java25-test.nro' not in sd.namelist()
    print('SD ZIP binaries and accompanying source: PASS')

if (dist / 'zero-services-test.nro').exists():
    with tarfile.open(dist / 'java-switch-source.tar.gz') as bundle:
        upstream = bundle.extractfile('third-party/openjdk25-upstream.tar.gz').read()
        lock = json.load(bundle.extractfile('java-switch/dependencies.lock.json'))
        assert hashlib.sha256(upstream).hexdigest() == lock['openjdk25_source']['sha256']
        patches = {p.name: bundle.extractfile(p).read() for p in bundle
                   if p.isfile() and p.name.startswith('java-switch/patches/openjdk25/')}
        expected = list((root / 'patches/openjdk25').glob('*.patch'))
        assert len(patches) == len(expected)
        for path in expected:
            assert patches['java-switch/patches/openjdk25/' + path.name] == path.read_bytes()
        originals = set()
        for data in patches.values():
            originals.update(line[6:] for line in data.decode().splitlines() if line.startswith('--- a/'))
        with tempfile.TemporaryDirectory(dir=root / 'build') as directory:
            target = pathlib.Path(directory)
            with tarfile.open(fileobj=io.BytesIO(upstream)) as source:
                for member in source:
                    relative = member.name.split('/', 1)[-1]
                    if relative in originals:
                        member.name = relative
                        source.extract(member, target, filter='data')
            for name, data in sorted(patches.items()):
                patch = target / pathlib.Path(name).name
                patch.write_bytes(data)
                subprocess.run(['patch', '-p1', '--batch', '--fuzz=0', '-i', str(patch)],
                               cwd=target, stdout=subprocess.DEVNULL, check=True)
            prefix = 'java-switch/ports/openjdk25/'
            for member in bundle:
                if member.isfile() and member.name.startswith(prefix + 'src/'):
                    path = target / member.name.removeprefix(prefix)
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(bundle.extractfile(member).read())
            from openjdk25 import port_inputs, PORT
            port_patches, overlay, files, digest = port_inputs()
            assert digest == json.loads((root / 'build/openjdk25-probe.json').read_text())['port_digest']
            for name in originals | {'src/java.base/horizon/native/include/jni_md.h',
                                     'src/java.base/horizon/conf/net.properties'}:
                assert (target / name).read_bytes() == (PORT / name).read_bytes(), name
            for path in files:
                assert (target / path.relative_to(overlay)).read_bytes() == path.read_bytes()
        print('Full pinned OpenJDK archive, tracked zero-fuzz patches and native overlay reconstructed from shipped source: PASS')
print('All artifact SHA256 checks: PASS; Horizon hardware NOT TESTED')
