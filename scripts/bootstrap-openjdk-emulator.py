#!/usr/bin/env python3
"""Install the optional hash-pinned Linux x86_64 ARM64 test engine locally."""
import hashlib,json,pathlib,urllib.request,zipfile
root=pathlib.Path(__file__).resolve().parents[1]
entry=json.loads((root/'dependencies.lock.json').read_text())['openjdk_test_emulator']
archive=root/'.deps/build-tools'/entry['url'].rsplit('/',1)[1]
archive.parent.mkdir(parents=True,exist_ok=True)
if not archive.exists():urllib.request.urlretrieve(entry['url'],archive)
if hashlib.sha256(archive.read_bytes()).hexdigest()!=entry['sha256']:
    raise RuntimeError('Optional emulator archive SHA256 mismatch: '+str(archive))
with zipfile.ZipFile(archive) as wheel:wheel.extractall(root/'.deps/openjdk25-emulator')
print('Optional Linux Unicorn engine verified; not a Horizon artifact or VM')
