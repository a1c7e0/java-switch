#!/usr/bin/env python3
"""Exercise the Horizon libjimage backend against a host memory-bridge fake.

This checks fd cursor preservation and the private snapshot's ownership and
failure cleanup. It does not execute Horizon syscalls or validate device I/O.
"""
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / '.deps/openjdk25-source/src/java.base'
OVERLAY = ROOT / 'ports/openjdk25/src/java.base/horizon/native/libjimage'
HOTSPOT = ROOT / 'ports/openjdk25/src/hotspot/os/horizon'


def main():
    required = [SOURCE / 'share/native/include/jni.h',
                SOURCE / 'unix/native/include/jni_md.h',
                SOURCE / 'share/native/libjimage/osSupport.hpp']
    if any(not item.is_file() for item in required):
        raise SystemExit('run scripts/openjdk25.py prepare first (pinned source required)')
    with tempfile.TemporaryDirectory(prefix='jimage-host-') as temp:
        temp_root = pathlib.Path(temp)
        patched = temp_root / 'openjdk'
        libjimage = patched / 'src/java.base/share/native/libjimage'
        libjimage.mkdir(parents=True)
        for name in ('osSupport.hpp', 'imageFile.cpp'):
            shutil.copyfile(SOURCE / 'share/native/libjimage' / name,
                            libjimage / name)
        subprocess.run([
            'patch', '-p1', '--batch', '--fuzz=0', '-i',
            str(ROOT / 'patches/openjdk25/0015-horizon-jimage-index-snapshot.patch'),
        ], cwd=patched, check=True, stdout=subprocess.DEVNULL)
        binary = temp_root / 'jimage-host-test'
        command = [
            'c++', '-std=c++14', '-D__SWITCH__', '-Wall', '-Wextra', '-Werror', '-pthread',
            '-I' + str(libjimage),
            '-I' + str(SOURCE / 'share/native/include'),
            '-I' + str(SOURCE / 'unix/native/include'),
            '-I' + str(SOURCE / 'share/native/libjimage'),
            '-I' + str(HOTSPOT),
            str(OVERLAY / 'osSupport_horizon.cpp'),
            str(ROOT / 'tests/openjdk25_jimage_host.cpp'),
            '-o', str(binary),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
    print('Horizon libjimage host contract checks passed (bridge calls mocked)')


if __name__ == '__main__':
    main()
