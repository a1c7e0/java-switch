#!/usr/bin/env python3
"""Prepare the pinned OpenJDK tree and run the experimental Horizon build.

Never changes the pristine source tree. Build products stay outside dist/ until
a complete VM can be linked and checked through the public SDK.
"""
import argparse
import hashlib
import json
import os
import pathlib
import shlex
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEPS = ROOT / '.deps'
SOURCE = DEPS / 'openjdk25-source'
PORT = DEPS / 'openjdk25-horizon'
BUILD = ROOT / 'build/openjdk25-horizon'


def prepare():
    entry = json.loads((ROOT / 'dependencies.lock.json').read_text())['openjdk25_source']
    archive = DEPS / 'openjdk25.archive'
    DEPS.mkdir(exist_ok=True)
    if not archive.exists():
        urllib.request.urlretrieve(entry['url'], archive)
    if hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest() != entry['sha256']:
        raise RuntimeError('OpenJDK source archive checksum mismatch')
    if not SOURCE.exists():
        with tarfile.open(archive) as source:
            for member in source:
                member.name = member.name.split('/', 1)[-1]
                if member.name:
                    source.extract(member, SOURCE, filter='data')
    patches = sorted((ROOT / 'patches/openjdk25').glob('*.patch'))
    digest = hashlib.sha256(b''.join(p.read_bytes() for p in patches)).hexdigest()
    stamp = PORT / '.javaswitch-patches'
    if not stamp.exists() or stamp.read_text() != digest:
        if PORT.exists():
            shutil.rmtree(PORT)
        shutil.copytree(SOURCE, PORT)
        for patch in patches:
            subprocess.run(['patch', '-p1', '--batch', '--fuzz=0', '-i', str(patch)],
                           cwd=PORT, check=True)
        stamp.write_text(digest)
    print(f'OpenJDK {entry["revision"]}, patches SHA256 {digest}', flush=True)


def run(command, env, log):
    BUILD.mkdir(parents=True, exist_ok=True)
    # configure rotates configure.log in its own output directory.
    log_path = ROOT / 'build' / ('openjdk25-' + log)
    with log_path.open('w') as output:
        output.write('$ ' + shlex.join(command) + '\n')
        output.flush()
        process = subprocess.run(command, cwd=BUILD, env=env,
                                 stdout=output, stderr=subprocess.STDOUT)
        output.write(f'\nExit status: {process.returncode}\n')
    print(f'{log_path.relative_to(ROOT)}: exit {process.returncode}', flush=True)
    return process.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'configure', 'build'])
    args = parser.parse_args()
    prepare()
    if args.action == 'prepare':
        return 0
    env = dict(os.environ)
    dk = pathlib.Path(env.get('DEVKITPRO', ROOT / '.toolchain/opt/devkitpro')).resolve()
    env['DEVKITPRO'] = str(dk)
    tools = DEPS / 'build-tools/root/usr'
    if tools.exists():
        config = DEPS / 'build-tools/autom4te.cfg'
        config.write_text((tools / 'share/autoconf/autom4te.cfg').read_text().replace(
            '/usr/share/autoconf', str(tools / 'share/autoconf')))
        env.update(AC_MACRODIR=str(tools / 'share/autoconf'),
                   PERL5LIB=str(tools / 'share/autoconf'),
                   AUTOM4TE_CFG=str(config),
                   trailer_m4=str(tools / 'share/autoconf/autoconf/trailer.m4'),
                   AUTOM4TE=str(tools / 'bin/autom4te'),
                   AUTOCONF=str(tools / 'bin/autoconf'), M4=str(tools / 'bin/m4'))
    env['PATH'] = str(dk / 'devkitA64/bin') + os.pathsep + env['PATH']
    if args.action == 'build':
        return run(['make', 'JOBS=2',
                    'hotspot-zero-static-libs'], env, 'build.log')
    boot = next((DEPS / 'host-jdk25').glob('*/bin/javac')).parent.parent
    prefix = dk / 'devkitA64/bin/aarch64-none-elf-'
    arch = f'-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ -I{dk}/libnx/include'
    command = ['bash', str(PORT / 'configure'),
               '--openjdk-target=aarch64-none-horizon', '--with-jvm-variants=zero',
               '--enable-headless-only', '--disable-warnings-as-errors',
               '--with-freetype=bundled',
               '--with-debug-level=release', f'--with-boot-jdk={boot}',
               f'CC={prefix}gcc', f'CXX={prefix}g++', f'AR={prefix}ar',
               f'NM={prefix}nm', f'STRIP={prefix}strip', f'OBJCOPY={prefix}objcopy',
               f'--with-extra-cflags={arch}', f'--with-extra-cxxflags={arch}',
               f'--with-extra-ldflags=-specs={dk}/libnx/switch.specs -L{dk}/libnx/lib',
               f'--with-libffi-include={ROOT}/build/libffi/include',
               f'--with-libffi-lib={ROOT}/build/libffi/.libs',
               '--disable-jvm-feature-jfr', '--disable-jvm-feature-cds',
               '--disable-jvm-feature-services', '--disable-jvm-feature-jvmti',
               '--disable-jvm-feature-vm-structs']
    return run(command, env, 'configure.log')


if __name__ == '__main__':
    raise SystemExit(main())
