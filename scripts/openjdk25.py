#!/usr/bin/env python3
"""Prepare the pinned OpenJDK tree and run the experimental Horizon build.

Never changes the pristine source tree. Only explicitly named native-service
diagnostics enter dist/; no incomplete library is exported as a Java VM.
"""
import argparse
import fcntl
import hashlib
import json
import os
import pathlib
import shlex
import shutil
import struct
import subprocess
import sys
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEPS = ROOT / '.deps'
SOURCE = DEPS / 'openjdk25-source'
PORT = DEPS / 'openjdk25-horizon'
BUILD = ROOT / 'build/openjdk25-horizon'


def port_inputs():
    patches = sorted((ROOT / 'patches/openjdk25').glob('*.patch'))
    overlay = ROOT / 'ports/openjdk25'
    files = sorted(p for p in (overlay / 'src').rglob('*') if p.is_file())
    digest = hashlib.sha256(b''.join(p.read_bytes() for p in patches) + b''.join(
        str(p.relative_to(overlay)).encode() + b'\0' + p.read_bytes() for p in files)).hexdigest()
    return patches, overlay, files, digest


def prepare():
    entry = json.loads((ROOT / 'dependencies.lock.json').read_text())['openjdk25_source']
    archive = DEPS / 'openjdk25.archive'
    DEPS.mkdir(exist_ok=True)
    if not archive.exists():
        urllib.request.urlretrieve(entry['url'], archive)
    with archive.open('rb') as source:
        if hashlib.file_digest(source, 'sha256').hexdigest() != entry['sha256']:
            raise RuntimeError('OpenJDK source archive checksum mismatch')
    if not SOURCE.exists():
        with tarfile.open(archive) as source:
            for member in source:
                member.name = member.name.split('/', 1)[-1]
                if member.name:
                    source.extract(member, SOURCE, filter='data')
    patches, overlay, files, digest = port_inputs()
    stamp = PORT / '.javaswitch-patches'
    if not stamp.exists() or stamp.read_text() != digest:
        if PORT.exists():
            shutil.rmtree(PORT)
        shutil.copytree(SOURCE, PORT)
        for patch in patches:
            subprocess.run(['patch', '-p1', '--batch', '--fuzz=0', '-i', str(patch)],
                           cwd=PORT, check=True)
        for source in files:
            target = PORT / source.relative_to(overlay)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
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


def work():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'configure', 'build', 'components', 'probe', 'test-host'])
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
    if args.action == 'components':
        # Reuse the real upstream compiler command, not host-JDK headers or a
        # substitute VM. Run the normal build first to generate this command.
        commands = sorted((BUILD / 'make-support/failure-logs').glob('*precompiled*.cmdline'))
        if not commands:
            raise RuntimeError('Run the upstream build first to record its compile command')
        original = shlex.split(commands[0].read_text())
        flags = []
        skip = False
        for argument in original[1:]:
            if skip:
                skip = False
                continue
            if argument in ['-o', '-MF', '-x']:
                skip = True
            elif argument not in ['-c', '-MMD'] and not argument.endswith('.hpp'):
                flags.append(argument.replace('\\"', '"'))
        output = BUILD / 'components'
        output.mkdir(exist_ok=True)
        sources = ['src/hotspot/os/horizon/os_horizon_time.cpp',
                   'src/hotspot/os/horizon/semaphore_horizon.cpp',
                   'src/hotspot/share/utilities/stringUtils.cpp']
        with (ROOT / 'build/openjdk25-components.log').open('w') as log:
            for source in sources:
                obj = output / (pathlib.Path(source).stem + '.o')
                command = [original[0], *flags, '-ffunction-sections', '-fdata-sections',
                           '-c', str(PORT / source), '-o', str(obj)]
                log.write('$ ' + shlex.join(command) + '\n')
                log.flush()
                result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
                if result.returncode:
                    return result.returncode
                data = obj.read_bytes()
                if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', data, 16) != (1, 183):
                    raise RuntimeError('Component is not an AArch64 relocatable object')
                subprocess.run([str(dk / 'devkitA64/bin/aarch64-none-elf-nm'), '-C', str(obj)],
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            bridge = PORT / 'src/hotspot/os/horizon/horizon_bridge.c'
            subprocess.run([str(dk / 'devkitA64/bin/aarch64-none-elf-gcc'),
                            '-O2', '-march=armv8-a+crc+crypto', '-mtp=soft', '-fPIE',
                            '-I' + str(dk / 'libnx/include'), '-c', str(bridge),
                            '-o', str(output / 'horizon_bridge.o')], env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
            log.write('CROSS-BUILT: time, semaphore, string utilities and C bridge; no VM linked or executed.\n')
        metadata = {'compiler': original[0], 'flags': flags,
                    'port_digest': (PORT / '.javaswitch-patches').read_text(),
                    'objects': {name: hashlib.sha256((output / name).read_bytes()).hexdigest()
                                for name in ['os_horizon_time.o', 'semaphore_horizon.o',
                                             'stringUtils.o', 'horizon_bridge.o']}}
        (output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
        print('HotSpot component objects: CROSS-BUILT, not a usable libjvm')
        return 0
    if args.action in ['probe', 'test-host']:
        output = BUILD / 'components'
        metadata = json.loads((output / 'metadata.json').read_text())
        if metadata['port_digest'] != (PORT / '.javaswitch-patches').read_text():
            raise RuntimeError('Port changed: rebuild components before testing or linking')
        for name, digest in metadata['objects'].items():
            if hashlib.sha256((output / name).read_bytes()).hexdigest() != digest:
                raise RuntimeError('Component object checksum mismatch: ' + name)
        if args.action == 'test-host':
            # The test substitutes only clock inputs and the fatal error sink;
            # it compiles the actual os:: implementation against JDK25 headers.
            flags = ['-std=c++14', '-DPRODUCT', '-DHORIZON', '-DZERO', '-DAARCH64',
                     '-DTARGET_COMPILER_gcc', '-fno-exceptions',
                     '-include', 'cstdio', '-include', 'cstdlib', '-include', 'cstring',
                     '-include', 'cwchar', '-include', 'unistd.h',
                     '-DVM_LITTLE_ENDIAN', '-DINCLUDE_SUFFIX_OS=_horizon',
                     '-DINCLUDE_SUFFIX_CPU=_zero', '-DINCLUDE_SUFFIX_COMPILER=_gcc']
            flags += [f for f in metadata['flags'] if f.startswith('-I') and '/.toolchain/' not in f]
            flags += [f for f in metadata['flags'] if f.startswith('-DINCLUDE_')]
            binary = output / 'time-host'
            subprocess.run(['g++', *flags, str(PORT / 'src/hotspot/os/horizon/os_horizon_time.cpp'),
                            str(ROOT / 'tests/openjdk25_time_host.cpp'),
                            '-pthread', '-Wl,--wrap=gettimeofday', '-o', str(binary)], check=True)
            with (ROOT / 'build/openjdk25-time-host.log').open('w') as log:
                subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT, check=True)
                for mode in ['zero-frequency', 'clock-error']:
                    # Prevent core files from exhausting this small workspace.
                    import resource
                    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
                    result = subprocess.run([str(binary), mode], capture_output=True, text=True)
                    if result.returncode >= 0 or 'guarantee(' not in result.stderr:
                        raise RuntimeError('Expected a fatal clock failure: ' + mode)
                    log.write(f'{mode}: rejected (signal {-result.returncode}); {result.stderr}\n')
            print('Horizon time conversion/error handling: HOST-PASSED, mocked clocks')
            return 0
        flags = metadata['flags']
        cc = metadata['compiler']
        probe = output / 'services_probe.o'
        runner = output / 'services_runner.o'
        subprocess.run([cc, *flags, '-c', str(ROOT / 'tests/openjdk25_services_probe.cpp'),
                        '-o', str(probe)], env=env, check=True)
        arch = ['-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft', '-fPIE']
        for source, obj in [('src/zero_services_test.c', runner), ('src/platform.c', output / 'platform.o')]:
            subprocess.run([cc, *arch, '-D__SWITCH__', '-I' + str(dk / 'libnx/include'),
                            '-I' + str(ROOT / 'src'), '-c', str(ROOT / source), '-o', str(obj)],
                           env=env, check=True)
        elf = output / 'zero-services-test.elf'
        objects = [output / name for name in ['os_horizon_time.o', 'semaphore_horizon.o',
                   'horizon_bridge.o', 'platform.o', 'services_probe.o', 'services_runner.o']]
        subprocess.run([str(dk / 'devkitA64/bin/aarch64-none-elf-g++'), *arch,
                        '-specs=' + str(dk / 'libnx/switch.specs'), '-L' + str(dk / 'libnx/lib'),
                        '-Wl,--gc-sections', *map(str, objects), '-lnx', '-lm', '-o', str(elf)],
                       env=env, check=True)
        symbols = subprocess.check_output([str(dk / 'devkitA64/bin/aarch64-none-elf-nm'),
                                           '-C', str(elf)], text=True)
        if 'JNI_CreateJavaVM' in symbols:
            raise RuntimeError('Unexpected VM entry in standalone service diagnostic')
        for symbol in ['os::javaTimeNanos()', 'HorizonSemaphore::wait()']:
            if symbol not in symbols:
                raise RuntimeError('Missing real service symbol: ' + symbol)
        data = elf.read_bytes()
        if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', data, 16) != (3, 183):
            raise RuntimeError('Probe is not an AArch64 Horizon PIE')
        nacp = output / 'zero-services-test.nacp'
        subprocess.run([str(dk / 'tools/bin/nacptool'), '--create', 'zero-services-test',
                        'JavaSwitch', '0.2.0', str(nacp)], check=True)
        nro = ROOT / 'dist/zero-services-test.nro'
        subprocess.run([str(dk / 'tools/bin/elf2nro'), str(elf), str(nro), '--nacp=' + str(nacp),
                        '--icon=' + str(dk / 'libnx/default_icon.jpg')], check=True)
        evidence = {'port_digest': metadata['port_digest'], 'java_vm_linked': False,
                    'inputs': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                               for name in ['src/zero_services_test.c', 'src/platform.c',
                                            'tests/openjdk25_services_probe.cpp', 'scripts/openjdk25.py']},
                    'elf_sha256': hashlib.sha256(elf.read_bytes()).hexdigest(),
                    'nro_sha256': hashlib.sha256(nro.read_bytes()).hexdigest()}
        (ROOT / 'build/openjdk25-probe.json').write_text(json.dumps(evidence, indent=2) + '\n')
        print('zero-services-test.nro: CROSS-BUILT; Java VM unavailable; hardware NOT TESTED')
        return 0
    if args.action == 'build':
        return run(['make', 'JOBS=2',
                    'hotspot-zero-static-libs'], env, 'build.log')
    boot = next((DEPS / 'host-jdk25').glob('*/bin/javac')).parent.parent
    prefix = dk / 'devkitA64/bin/aarch64-none-elf-'
    # Use the actual W^X libffi archive and allocator, not the generic ELF
    # malloc closure allocator from libffi's intermediate build directory.
    ffi = BUILD / 'horizon-libffi'
    ffi.mkdir(exist_ok=True)
    shutil.copyfile(ROOT / 'build/libffi-horizon.a', ffi / 'libffi.a')
    subprocess.run([str(prefix) + 'ar', 'r', str(ffi / 'libffi.a'),
                    str(ROOT / 'build/sdk/ffi_horizon.o')], check=True)
    arch = f'-march=armv8-a+crc+crypto -mtune=cortex-a57 -mno-outline-atomics -mtp=soft -fPIE -D__SWITCH__ -I{dk}/libnx/include'
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
               f'--with-libffi-lib={ffi}',
               '--disable-jvm-feature-jfr', '--disable-jvm-feature-cds',
               '--disable-jvm-feature-services', '--disable-jvm-feature-jvmti',
               '--disable-jvm-feature-vm-structs']
    return run(command, env, 'configure.log')


def main():
    DEPS.mkdir(exist_ok=True)
    # Preparing and configuring the same port concurrently can invalidate a
    # compiler command or partially overwrite the tree. Serialize all actions.
    with (DEPS / 'openjdk25.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        return work()


if __name__ == '__main__':
    raise SystemExit(main())
