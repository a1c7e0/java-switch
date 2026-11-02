#!/usr/bin/env python3
"""Extract optional, checksum-pinned Debian autoconf/m4 without root access.

These are Linux build tools, never Horizon artifacts. A system autoconf/m4 may
also be used by omitting this bootstrap. Requires dpkg-deb and Perl.
"""
import hashlib
import json
import pathlib
import subprocess
import urllib.request

root = pathlib.Path(__file__).resolve().parents[1]
tools = root / '.deps/build-tools'
tools.mkdir(parents=True, exist_ok=True)
lock = json.loads((root / 'dependencies.lock.json').read_text())
for name, entry in lock['openjdk_build_tools'].items():
    archive = tools / entry['url'].rsplit('/', 1)[1]
    if not archive.exists():
        urllib.request.urlretrieve(entry['url'], archive)
    with archive.open('rb') as source:
        if hashlib.file_digest(source, 'sha256').hexdigest() != entry['sha256']:
            raise RuntimeError(f'{name} checksum mismatch')
    subprocess.run(['dpkg-deb', '-x', str(archive), str(tools / 'root')], check=True)
    print(f'{name}: verified and extracted')
