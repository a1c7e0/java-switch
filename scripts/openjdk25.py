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
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEPS = ROOT / '.deps'
SOURCE = DEPS / 'openjdk25-source'
PORT = DEPS / 'openjdk25-horizon'
BUILD = ROOT / 'build/openjdk25-horizon'
MINECRAFT_JAVA_MODULES = [
    'java.logging', 'jdk.unsupported', 'java.management', 'jdk.charsets',
    'java.desktop', 'java.datatransfer', 'java.xml', 'java.prefs',
]


def module_slug(name):
    return name.removeprefix('java.').replace('.', '-')


def port_snapshot():
    patches = sorted((ROOT / 'patches/openjdk25').glob('*.patch'))
    overlay = ROOT / 'ports/openjdk25'
    files = sorted(p for p in (overlay / 'src').rglob('*') if p.is_file())
    contents = {path: path.read_bytes() for path in patches + files}
    digest = hashlib.sha256(b''.join(contents[p] for p in patches) + b''.join(
        str(p.relative_to(overlay)).encode() + b'\0' + contents[p] for p in files)).hexdigest()
    return patches, overlay, files, digest, contents


def port_inputs():
    return port_snapshot()[:4]


def jvm_features():
    spec = (BUILD / 'spec.gmk').read_text()
    match = re.search(r'^JVM_FEATURES_zero\s*:=\s*(.*)$', spec, re.M)
    if match is None:
        raise RuntimeError('Configured Zero feature list is missing')
    return match.group(1).split()


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
    patches, overlay, files, digest, contents = port_snapshot()
    stamp = PORT / '.javaswitch-patches'
    patch_stamp = PORT / '.javaswitch-source-patches'
    overlay_stamp = PORT / '.javaswitch-overlay-files.json'
    patch_digest = hashlib.sha256(b''.join(p.name.encode() + b'\0' + contents[p] for p in patches)).hexdigest()
    names = [str(p.relative_to(overlay)) for p in files]
    # Source patch changes or removed overlays require a clean reconstruction.
    # Updating a native overlay alone must not rewrite all upstream headers and
    # force configure/PCH/full rebuild on every small porting iteration.
    rebuild = (not patch_stamp.exists() or patch_stamp.read_text() != patch_digest or
               not overlay_stamp.exists() or bool(set(json.loads(overlay_stamp.read_text())) - set(names)))
    unchanged_times = {}
    if rebuild:
        # Retain timestamps only when rebuilt bytes are identical. Otherwise
        # every patch edit rewrites all overlay headers and invalidates the
        # full VM PCH even when only Java code changed.
        candidates = set(names)
        for patch in patches:
            candidates.update(re.findall(r'^\+\+\+ b/(.+)$', contents[patch].decode(), re.M))
        for name in candidates:
            old = PORT / name
            if old.is_file():
                info = old.stat()
                unchanged_times[name] = (hashlib.sha256(old.read_bytes()).digest(),
                                         info.st_atime_ns, info.st_mtime_ns)
    # A rejected patch must not destroy the last complete prepared tree or its
    # timestamps. Publish a reconstructed tree only after every patch/overlay
    # and provenance stamp has been installed successfully.
    temporary = pathlib.Path(tempfile.mkdtemp(prefix='.openjdk25-prepare-', dir=DEPS)) if rebuild else None
    target_root = temporary / 'tree' if rebuild else PORT
    try:
        if rebuild:
            shutil.copytree(SOURCE, target_root)
            for patch in patches:
                subprocess.run(['patch', '-p1', '--batch', '--fuzz=0'],
                               input=contents[patch], cwd=target_root, check=True)
        for source in files:
            relative = source.relative_to(overlay)
            # The overlay is arranged under src/ for port organization, while
            # OpenJDK's make/ tree lives at the repository root.  HotSpot and
            # java.base sources, by contrast, live under src/ upstream.
            if relative.parts[:2] == ('src', 'make'):
                relative = pathlib.Path(*relative.parts[1:])
            target = target_root / relative
            if not target.exists() or target.read_bytes() != contents[source]:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(contents[source])
        for name, (old_hash, atime, mtime) in unchanged_times.items():
            target = target_root / name
            if target.is_file() and hashlib.sha256(target.read_bytes()).digest() == old_hash:
                os.utime(target, ns=(atime, mtime))
        # Use captured bytes throughout: later source edits require another
        # build, but cannot corrupt this build's recorded input digest.
        (target_root / patch_stamp.name).write_text(patch_digest)
        target_stamp = target_root / stamp.name
        if not target_stamp.exists() or target_stamp.read_text() != digest:
            target_stamp.write_text(digest)
        (target_root / overlay_stamp.name).write_text(json.dumps(names))
        if rebuild:
            old = temporary / 'old'
            if PORT.exists():
                PORT.rename(old)
            try:
                target_root.rename(PORT)
            except BaseException:
                if old.exists():
                    old.rename(PORT)
                raise
    finally:
        if temporary is not None:
            shutil.rmtree(temporary)
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


def link_vm(dk, env, native=False, instrument=False, minecraft=False):
    # Never replace missing services with stubs or turn an unresolved ELF into
    # an NRO. This action intentionally stops at a real native link failure.
    archive = BUILD / 'hotspot/variant-zero/libjvm/objs/static/libjvm.a'
    archive_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive_proof = ROOT / ('build/openjdk25-minecraft-archive.json' if minecraft
                            else 'build/openjdk25-archive.json')
    proof = json.loads(archive_proof.read_text())
    if not proof['full_upstream_archive_target'] or proof['archive_sha256'] != archive_hash:
        raise RuntimeError('Build the genuine upstream archive before linking')
    features = proof.get('jvm_features', [])
    if instrument and not {'jvmti', 'services'}.issubset(features):
        raise RuntimeError('The startup agent requires a genuine JVMTI/services VM archive')
    if minecraft and 'management' not in features:
        raise RuntimeError('The Minecraft profile requires genuine HotSpot management/JMM support')
    output_name = 'runtime-link-minecraft' if minecraft else ('runtime-link' if native else 'vm-link')
    output = BUILD / output_name
    output.mkdir(exist_ok=True)
    arch = ['-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mno-outline-atomics', '-mtp=soft', '-fPIE']
    elf = output / 'java25-test.elf'
    evidence_prefix = ('openjdk25-runtime-link-minecraft' if minecraft else
                       'openjdk25-runtime-link' if native else 'openjdk25-link')
    log_path = ROOT / 'build' / (evidence_prefix + '.log')
    libraries = []
    with log_path.open('w') as log:
        log.write('Real upstream Zero archive SHA256 ' + hashlib.sha256(archive.read_bytes()).hexdigest() + '\n')
        if native:
            base_proof = json.loads((ROOT / 'build/openjdk25-native-base.json').read_text())
            if base_proof['port_digest'] != proof['port_digest']:
                raise RuntimeError('Rebuild native java.base for this VM port revision')
            for module in ['java', 'zip', 'jimage', 'verify', 'net', 'nio', 'fallbackLinker', 'syslookup']:
                path = BUILD / 'support/native/java.base' / ('lib' + module) / 'static' / ('lib' + module + '.a')
                if not path.is_file():
                    raise RuntimeError('Build real native class library first: ' + str(path))
                if hashlib.sha256(path.read_bytes()).hexdigest() != base_proof['archives'][module]['sha256']:
                    raise RuntimeError('Native java.base archive changed: ' + module)
                libraries.append((module, path))
            if instrument:
                native_proof = json.loads((ROOT / 'build/openjdk25-native-instrument.json').read_text())
                if native_proof['port_digest'] != proof['port_digest']:
                    raise RuntimeError('Rebuild java.instrument for this port revision')
                for module, owner in [('jli_manifest', 'java.base'), ('instrument', 'java.instrument')]:
                    path = BUILD / 'support/native' / owner / ('lib' + module) / 'static' / ('lib' + module + '.a')
                    if hashlib.sha256(path.read_bytes()).hexdigest() != native_proof['archives'][module]['sha256']:
                        raise RuntimeError('The instrument archive changed: ' + module)
                    libraries.append((module, path))
            if minecraft:
                management_proof = json.loads((ROOT / 'build/openjdk25-native-management.json').read_text())
                if management_proof['port_digest'] != proof['port_digest']:
                    raise RuntimeError('Rebuild native java.management for this VM port revision')
                path = BUILD / 'support/native/java.management/libmanagement/static/libmanagement.a'
                if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != management_proof['archives']['management']['sha256']:
                    raise RuntimeError('Missing or changed genuine java.management native archive')
                libraries.append(('management', path))
            generator = [sys.executable, str(ROOT / 'scripts/generate-openjdk25-native-resolver.py'),
                         '--nm', str(dk / 'devkitA64/bin/aarch64-none-elf-nm'),
                         '--output', str(output / 'native_resolver.c'),
                         '--manifest', str(output / 'native_resolver.json')]
            # FFM system lookup needs a deliberately bounded catalogue of real
            # C/libm exports. Do not reference every newlib syscall wrapper.
            c_exports = ['strlen', 'strcmp', 'strncmp', 'strcpy', 'strncpy', 'strcat', 'strncat',
                         'strchr', 'strrchr', 'strstr', 'strspn', 'strcspn', 'strpbrk',
                         'memcpy', 'memmove', 'memcmp', 'memset', 'memchr',
                         'malloc', 'calloc', 'realloc', 'free', 'abort',
                         'printf', 'snprintf', 'sprintf', 'puts', 'putchar',
                         'fopen', 'fclose', 'fread', 'fwrite', 'fflush', 'fseek', 'ftell']
            math_exports = ['sin', 'cos', 'tan', 'atan2', 'pow', 'sqrt', 'ceil', 'floor',
                            'fabs', 'fmod', 'exp', 'log']
            for name in ['libc.a', 'libm.a']:
                path = pathlib.Path(subprocess.check_output(
                    [str(dk / 'devkitA64/bin/aarch64-none-elf-gcc'), *arch,
                     '-print-file-name=' + name], env=env, text=True).strip()).resolve()
                if not path.is_file():
                    raise RuntimeError('Missing target system archive: ' + name)
                libraries.append(('syslookup', path))
            for module, path in libraries:
                generator += ['--library', module + '=' + str(path)]
            generator += ['--symbols', 'syslookup=' + ','.join(
                ['JNI_OnLoad_syslookup', 'syslookup', *c_exports, *math_exports])]
            log.write('$ ' + shlex.join(generator) + '\n'); log.flush()
            subprocess.run(generator, stdout=log, stderr=subprocess.STDOUT, check=True)
            command = [str(dk / 'devkitA64/bin/aarch64-none-elf-gcc'), *arch,
                       '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'build'), '-c', str(output / 'native_resolver.c'),
                       '-o', str(output / 'native_resolver.o')]
            log.write('$ ' + shlex.join(command) + '\n'); log.flush()
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        for name in ['java25_vm_test', 'platform', 'runtime_openjdk']:
            command = [str(dk / 'devkitA64/bin/aarch64-none-elf-gcc'), *arch,
                       *(['-DJS_OPENJDK25_STATIC_PROVIDER'] if native else []),
                       *(['-DJS_OPENJDK25_INSTRUMENT'] if instrument else []),
                       '-D__SWITCH__', '-I' + str(dk / 'libnx/include'),
                       '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'build'), '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'build'),
                       '-I' + str(PORT / 'src/java.base/share/native/include'),
                       '-I' + str(PORT / 'src/java.base/horizon/native/include'),
                       '-c', str(ROOT / 'src' / (name + '.c')), '-o', str(output / (name + '.o'))]
            log.write('$ ' + shlex.join(command) + '\n'); log.flush()
            subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        command = [str(dk / 'devkitA64/bin/aarch64-none-elf-g++'), *arch,
                   '-specs=' + str(dk / 'libnx/switch.specs'), '-L' + str(dk / 'libnx/lib'),
                   '-Wl,--gc-sections', '-Wl,-u,JNI_CreateJavaVM',
                   str(output / 'java25_vm_test.o'), str(output / 'platform.o'),
                   str(output / 'runtime_openjdk.o'),
                   *([str(output / 'native_resolver.o')] if native else []),
                   '-Wl,--start-group', *[str(path) for _, path in libraries if path.name not in ('libc.a', 'libm.a')],
                   str(archive), str(ROOT / 'build/sdk/libjavaswitch.a'), '-lnx',
                   *[str(path) for _, path in libraries if path.name in ('libc.a', 'libm.a')], '-lm', '-Wl,--end-group', '-o', str(elf)]
        log.write('$ ' + shlex.join(command) + '\n'); log.flush()
        result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        log.write(f'\nELF link exit status: {result.returncode}; NRO conversion has not run yet\n')
    missing = sorted(set(re.findall(r"undefined reference to [`‘'](.*?)[’']", log_path.read_text())))
    (ROOT / 'build/openjdk25-missing-symbols.txt').write_text('\n'.join(missing) + '\n')
    evidence = {'archive_port_digest': proof['port_digest'], 'archive_sha256': archive_hash,
                'jvm_features': features, 'java_instrument': instrument,
                'sdk_sha256': hashlib.sha256((ROOT / 'build/sdk/libjavaswitch.a').read_bytes()).hexdigest(),
                'link_exit_status': result.returncode, 'missing_symbols': missing,
                'java_execution': 'NOT TESTED', 'horizon_execution': 'NOT TESTED'}
    if result.returncode == 0:
        data = elf.read_bytes()
        if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', data, 16) != (3, 183):
            raise RuntimeError('VM link is not an AArch64 Horizon PIE')
        symbols = subprocess.check_output([str(dk / 'devkitA64/bin/aarch64-none-elf-nm'), str(elf)], text=True)
        if not re.search(r'^[0-9a-f]+ T JNI_CreateJavaVM$', symbols, re.M):
            raise RuntimeError('Genuine VM entry was not linked')
        evidence['elf_sha256'] = hashlib.sha256(data).hexdigest()
    if native:
        evidence['class_library_provider'] = json.loads((output / 'native_resolver.json').read_text())
        evidence['launcher_sha256'] = hashlib.sha256((ROOT / 'src/java25_vm_test.c').read_bytes()).hexdigest()
        evidence['minecraft_profile'] = minecraft
        if result.returncode == 0:
            required = ['JNI_OnLoad_java', 'JIMAGE_Open', 'js_openjdk25_native_resolver']
            if instrument:
                required += ['JNI_OnLoad_instrument', 'Agent_OnLoad_instrument', 'JLI_ManifestIterate']
            if minecraft:
                required += ['JNI_OnLoad_management', 'JVM_GetManagement']
            for symbol in required:
                if not re.search(r'^[0-9a-f]+ [A-Za-z] ' + symbol + '$', symbols, re.M):
                    raise RuntimeError('Missing real runtime provider symbol: ' + symbol)
            nacp = output / 'java25-test.nacp'
            subprocess.run([str(dk / 'tools/bin/nacptool'), '--create', 'java25-test',
                            'JavaSwitch', '0.2.0', str(nacp)], check=True)
            nro = ROOT / 'dist/java25-test.nro'
            subprocess.run([str(dk / 'tools/bin/elf2nro'), str(elf), str(nro),
                            '--nacp=' + str(nacp), '--icon=' + str(dk / 'libnx/default_icon.jpg')], check=True)
            if nro.read_bytes()[16:20] != b'NRO0':
                raise RuntimeError('Invalid native VM NRO')
            evidence['nro_sha256'] = hashlib.sha256(nro.read_bytes()).hexdigest()
    (ROOT / 'build' / (evidence_prefix + '.json')).write_text(json.dumps(evidence, indent=2) + '\n')
    if native and result.returncode == 0:
        (ROOT / 'build/openjdk25-runtime-profile.json').write_text(json.dumps(
            {'minecraft_profile': minecraft, 'java_instrument': instrument}, indent=2) + '\n')
    artifact = 'native NRO cross-built; execution NOT TESTED' if native and result.returncode == 0 else 'no NRO created'
    print(f'Genuine JNI VM ELF link: exit {result.returncode}; {len(missing)} unique missing symbols; {artifact}')
    return result.returncode


def work():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'configure', 'configure-agent',
        'configure-minecraft', 'build', 'build-minecraft', 'java-base', 'native-base',
        'java-instrument', 'native-instrument', 'java-minecraft', 'native-management',
        'link-vm', 'link-runtime', 'link-runtime-agent', 'link-runtime-minecraft',
        'link-runtime-agent-minecraft',
        'components', 'probe', 'test-host'])
    args = parser.parse_args()
    # ELF linking consumes an already verified archive, independent of source
    # preparation. Its metadata records that archive's exact source digest.
    if args.action not in ('link-vm', 'link-runtime', 'link-runtime-agent',
                           'link-runtime-minecraft', 'link-runtime-agent-minecraft'):
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
    if args.action in ('link-vm', 'link-runtime', 'link-runtime-agent',
                       'link-runtime-minecraft', 'link-runtime-agent-minecraft'):
        return link_vm(dk, env, native=args.action != 'link-vm',
                       instrument=args.action in ('link-runtime-agent', 'link-runtime-agent-minecraft'),
                       minecraft=args.action in ('link-runtime-minecraft', 'link-runtime-agent-minecraft'))
    if args.action == 'components':
        # Reuse the real upstream compiler command, not host-JDK headers or a
        # substitute VM. Run the normal build first to generate this command.
        recorded = BUILD / 'hotspot/variant-zero/libjvm/objs/static/os_horizon_thread.o.cmdline'
        commands = ([recorded] if recorded.exists() else
                    sorted((BUILD / 'make-support/failure-logs').glob('*.cmdline')))
        # A Java class-library failure may be the most recent build error.
        # Its javac command cannot be reused to compile HotSpot components.
        commands = [path for path in commands
                    if 'aarch64-none-elf-g++' in path.read_text() and
                       '-DTARGET_ARCH_zero' in path.read_text()]
        if not commands:
            # A successful full archive build clears failure logs. Reuse its
            # actual ordinary C++ command rather than requiring a new failure.
            commands = [BUILD / 'hotspot/variant-zero/libjvm/objs/static/os_horizon_thread.o.cmdline']
            commands = [path for path in commands if path.exists()]
        if not commands:
            raise RuntimeError('Run the upstream build first to record its compile command')
        original = shlex.split(commands[0].read_text())
        flags = []
        skip = False
        for argument in original[1:]:
            if skip:
                skip = False
                continue
            if argument in ['-o', '-MF', '-x', '-include']:
                skip = True
            elif argument not in ['-c', '-MMD'] and not argument.endswith(('.hpp', '.cpp', '.c', '.s', '.S')):
                flags.append(argument.replace('\\"', '"'))
        output = BUILD / 'components'
        output.mkdir(exist_ok=True)
        sources = ['src/hotspot/os/horizon/os_horizon_time.cpp',
                   'src/hotspot/os/horizon/semaphore_horizon.cpp',
                   'src/hotspot/os/horizon/mutex_horizon.cpp',
                   'src/hotspot/os/horizon/park_horizon.cpp',
                   'src/hotspot/os/horizon/parker_horizon.cpp',
                   'src/hotspot/os/horizon/threadLocalStorage_horizon.cpp',
                   'src/hotspot/os/horizon/os_horizon_thread.cpp',
                   'src/hotspot/os/horizon/os_horizon_memory.cpp',
                   'src/hotspot/os/horizon/os_horizon_system.cpp',
                   'src/hotspot/os/horizon/os_horizon_libc.cpp',
                   'src/hotspot/os/horizon/os_horizon_boot.cpp',
                   'src/hotspot/os/horizon/os_horizon_library.cpp',
                   'src/hotspot/os/horizon/perfMemory_horizon.cpp',
                   'src/hotspot/os/horizon/os_horizon_optional.cpp',
                   'src/hotspot/os/horizon/os_horizon_diagnostics.cpp',
                   'src/hotspot/os/horizon/vmError_horizon.cpp',
                   'src/hotspot/os_cpu/horizon_zero/os_horizon_zero.cpp',
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
            subprocess.run([str(dk / 'devkitA64/bin/aarch64-none-elf-gcc'), '-std=gnu11',
                            '-O2', '-march=armv8-a+crc+crypto', '-mtp=soft', '-fPIE',
                            '-I' + str(dk / 'libnx/include'), '-c',
                            str(PORT / 'src/hotspot/os/horizon/horizon_thread_bridge.c'),
                            '-o', str(output / 'horizon_thread_bridge.o')], env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
            for name in ['safefetch_horizon.S', 'exception_fallback_horizon.S', 'horizon_fault_abi.c',
                         'horizon_memory_bridge.c', 'horizon_system_bridge.c']:
                subprocess.run([str(dk / 'devkitA64/bin/aarch64-none-elf-gcc'),
                                '-march=armv8-a+crc+crypto', '-fPIE',
                                '-I' + str(dk / 'libnx/include'), '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'build'), '-c',
                                str(PORT / 'src/hotspot/os/horizon' / name),
                                '-o', str(output / (pathlib.Path(name).stem + '.o'))], env=env,
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            log.write('CROSS-BUILT: time, synchronization, parking, native thread/TLS, SafeFetch/fatal entry and string utilities; no VM linked or executed.\n')
        metadata = {'compiler': original[0], 'flags': flags,
                    'port_digest': (PORT / '.javaswitch-patches').read_text(),
                    'objects': {name: hashlib.sha256((output / name).read_bytes()).hexdigest()
                                for name in ['os_horizon_time.o', 'semaphore_horizon.o', 'mutex_horizon.o',
                                             'park_horizon.o', 'horizon_thread_bridge.o',
                                             'parker_horizon.o',
                                             'threadLocalStorage_horizon.o',
                                             'os_horizon_thread.o',
                                             'os_horizon_memory.o', 'os_horizon_system.o',
                                             'os_horizon_libc.o', 'os_horizon_boot.o', 'os_horizon_library.o',
                                             'perfMemory_horizon.o', 'os_horizon_zero.o',
                                             'horizon_memory_bridge.o', 'horizon_system_bridge.o',
                                             'safefetch_horizon.o', 'exception_fallback_horizon.o', 'horizon_fault_abi.o',
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
            bridge_dir = PORT / 'src/hotspot/os/horizon'
            mock_objects = []
            for source in [bridge_dir / 'horizon_bridge.c', ROOT / 'tests/openjdk25_monitor_libnx_mock.c']:
                obj = output / (source.stem + '-host.o')
                subprocess.run(['gcc', '-std=gnu11', '-D_GNU_SOURCE', '-Wall', '-Wextra', '-Werror',
                                '-I' + str(ROOT / 'tests/openjdk25_stubs'), '-I' + str(bridge_dir),
                                '-c', str(source), '-o', str(obj)], check=True)
                mock_objects.append(str(obj))
            binary = output / 'monitor-host'
            subprocess.run(['g++', *flags, str(bridge_dir / 'mutex_horizon.cpp'),
                            str(bridge_dir / 'semaphore_horizon.cpp'),
                            str(bridge_dir / 'park_horizon.cpp'), str(bridge_dir / 'os_horizon_time.cpp'),
                            str(ROOT / 'tests/openjdk25_monitor_host.cpp'), *mock_objects,
                            '-pthread', '-Wl,--wrap=malloc', '-o', str(binary)], check=True)
            with (ROOT / 'build/openjdk25-monitor-host.log').open('w') as log:
                subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=30)
                for mode in ['oom', 'semaphore-oom', 'wait-error']:
                    result = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=10)
                    if result.returncode >= 0 or 'guarantee(' not in result.stderr:
                        raise RuntimeError('Expected fatal monitor failure: ' + mode)
                    if mode == 'wait-error' and '0xf601' not in result.stderr:
                        raise RuntimeError('Raw Horizon Result was lost')
                    log.write(f'{mode}: rejected (signal {-result.returncode}); {result.stderr}\n')
            print('Horizon monitor contention/timeouts/failures: HOST-PASSED, libnx mocks')
            thread_tests = ROOT / 'ports/openjdk25/tests/thread_bridge'
            binary = output / 'thread-bridge-host'
            subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Dcalloc=mock_calloc',
                            '-I' + str(thread_tests / 'mocks'), '-I' + str(bridge_dir),
                            str(bridge_dir / 'horizon_thread_bridge.c'),
                            str(thread_tests / 'test_thread_bridge.c'), '-o', str(binary)], check=True)
            with (ROOT / 'build/openjdk25-thread-host.log').open('w') as log:
                subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=10)
            binary = output / 'reaper-host'
            subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-DJS_HORIZON_THREAD_TESTING',
                            '-I' + str(thread_tests / 'mocks'), '-I' + str(bridge_dir),
                            str(bridge_dir / 'horizon_thread_bridge.c'),
                            str(thread_tests / 'test_reaper.c'), '-o', str(binary)], check=True)
            with (ROOT / 'build/openjdk25-reaper-host.log').open('w') as log:
                subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=10)
            binary = output / 'thread-start-host'
            subprocess.run(['g++', *flags, str(bridge_dir / 'mutex_horizon.cpp'),
                            str(ROOT / 'tests/openjdk25_thread_start_host.cpp'), *mock_objects,
                            '-pthread', '-Wl,--wrap=malloc', '-o', str(binary)], check=True)
            with (ROOT / 'build/openjdk25-thread-start-host.log').open('w') as log:
                subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=15)
                for mode in ['early-start', 'double-start', 'double-report']:
                    result = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=10)
                    if result.returncode >= 0 or 'guarantee(' not in result.stderr:
                        raise RuntimeError('Expected fatal startup-gate contract failure: ' + mode)
                    log.write(f'{mode}: rejected (signal {-result.returncode}); {result.stderr}\n')
            binary = output / 'timezone-host'
            subprocess.run(['g++', '-std=c++14', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'tests/openjdk25_timezone_host.cpp'), '-o', str(binary)], check=True)
            with (ROOT / 'build/openjdk25-timezone-host.log').open('w') as log:
                subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=10)
            for name in ['memory', 'system']:
                test_dir = ROOT / 'ports/openjdk25/tests' / (name + '_bridge')
                binary = output / (name + '-bridge-host')
                command = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                           '-I' + str(test_dir / 'mocks'), '-I' + str(bridge_dir),
                           str(bridge_dir / ('horizon_' + name + '_bridge.c')),
                           str(test_dir / ('test_' + name + '_bridge.c')),
                           '-pthread', '-o', str(binary)]
                if name == 'memory':
                    command += ['-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'build'),
                                str(ROOT / 'src/horizon_alias.c'), str(ROOT / 'src/vmem_horizon.c'),
                                '-Wl,--wrap=calloc']
                subprocess.run(command, check=True)
                with (ROOT / 'build' / ('openjdk25-' + name + '-bridge-host.log')).open('w') as log:
                    subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=15)
                    if name == 'system':
                        for mode in ['exit', 'quick']:
                            result = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=10)
                            if result.returncode != 83 or (mode == 'exit' and 'status 37' not in result.stderr):
                                raise RuntimeError('Horizon process exit did not reach the mocked kernel termination')
                            if mode == 'quick' and result.stderr:
                                raise RuntimeError('Emergency termination unexpectedly used stdio')
                            log.write(f'{mode}: kernel exit sentinel reached; {result.stderr}\n')
            binary = output / 'tls-host'
            subprocess.run(['g++', *flags, str(bridge_dir / 'threadLocalStorage_horizon.cpp'),
                            str(ROOT / 'tests/openjdk25_tls_host.cpp'), *mock_objects,
                            '-pthread', '-Wl,--wrap=malloc', '-o', str(binary)], check=True)
            with (ROOT / 'build/openjdk25-tls-host.log').open('w') as log:
                subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=10)
                for mode in ['allocation-failure', 'uninitialized', 'double-init']:
                    result = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=10)
                    if result.returncode >= 0 or 'guarantee(' not in result.stderr:
                        raise RuntimeError('Expected fatal TLS contract failure: ' + mode)
                    log.write(f'{mode}: rejected (signal {-result.returncode}); {result.stderr}\n')
            print('Horizon ThreadLocalStorage/compiler TLS: HOST-PASSED, libnx TLS mocked')
            return 0
        flags = metadata['flags']
        cc = metadata['compiler']
        probe = output / 'services_probe.o'
        runner = output / 'services_runner.o'
        subprocess.run([cc, *flags, '-c', str(ROOT / 'tests/openjdk25_services_probe.cpp'),
                        '-o', str(probe)], env=env, check=True)
        subprocess.run([cc, *flags, '-c', str(ROOT / 'tests/openjdk25_thread_probe.cpp'),
                        '-o', str(output / 'thread_probe.o')], env=env, check=True)
        subprocess.run([cc, *flags, '-c', str(ROOT / 'tests/openjdk25_reaper_probe.cpp'),
                        '-o', str(output / 'reaper_probe.o')], env=env, check=True)
        arch = ['-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft', '-fPIE']
        for source, obj in [('src/zero_services_test.c', runner), ('src/platform.c', output / 'platform.o')]:
            subprocess.run([str(dk / 'devkitA64/bin/aarch64-none-elf-gcc'), *arch, '-D__SWITCH__', '-I' + str(dk / 'libnx/include'),
                            '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'build'), '-c', str(ROOT / source), '-o', str(obj)],
                           env=env, check=True)
        for source in ['tests/openjdk25_memory_probe.c', 'src/horizon_alias.c', 'src/vmem_horizon.c']:
            obj = output / (pathlib.Path(source).stem + '.o')
            subprocess.run([str(dk / 'devkitA64/bin/aarch64-none-elf-gcc'), *arch,
                            '-mno-outline-atomics', '-D__SWITCH__', '-D_DEFAULT_SOURCE',
                            '-I' + str(dk / 'libnx/include'), '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'build'),
                            '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'build'), '-c', str(ROOT / source), '-o', str(obj)],
                           env=env, check=True)
        elf = output / 'zero-services-test.elf'
        objects = [output / name for name in ['os_horizon_time.o', 'semaphore_horizon.o',
                   'mutex_horizon.o',
                   'park_horizon.o', 'horizon_thread_bridge.o',
                   'threadLocalStorage_horizon.o',
                   'thread_probe.o',
                   'reaper_probe.o', 'openjdk25_memory_probe.o',
                   'horizon_memory_bridge.o', 'horizon_alias.o', 'vmem_horizon.o',
                   'safefetch_horizon.o', 'exception_fallback_horizon.o', 'horizon_fault_abi.o',
                   'horizon_bridge.o', 'platform.o', 'services_probe.o', 'services_runner.o']]
        subprocess.run([str(dk / 'devkitA64/bin/aarch64-none-elf-g++'), *arch,
                        '-specs=' + str(dk / 'libnx/switch.specs'), '-L' + str(dk / 'libnx/lib'),
                        '-Wl,--gc-sections', *map(str, objects), '-lnx', '-lm', '-o', str(elf)],
                       env=env, check=True)
        symbols = subprocess.check_output([str(dk / 'devkitA64/bin/aarch64-none-elf-nm'),
                                           '-C', str(elf)], text=True)
        if 'JNI_CreateJavaVM' in symbols:
            raise RuntimeError('Unexpected VM entry in standalone service diagnostic')
        for symbol in ['os::javaTimeNanos()', 'HorizonSemaphore::wait()', 'SafeFetch32_impl',
                       'SafeFetchN_impl', '__libnx_exception_entry', 'js_horizon_unhandled_exception_entry']:
            if symbol not in symbols:
                raise RuntimeError('Missing real service symbol: ' + symbol)
        if not any(line.split()[-2:] == ['T', '__aarch64_read_tp'] for line in symbols.splitlines()):
            raise RuntimeError('Native compiler TLS accessor is not linked from libnx')
        sized_symbols = subprocess.check_output(
            [str(dk / 'devkitA64/bin/aarch64-none-elf-nm'), '-S', str(elf)], text=True)
        exception_stack = [line.split() for line in sized_symbols.splitlines()
                           if line.split() and line.split()[-1] == '__nx_exception_stack']
        exception_stack_size = [line.split() for line in sized_symbols.splitlines()
                                if line.split() and line.split()[-1] == '__nx_exception_stack_size']
        if (len(exception_stack) != 1 or len(exception_stack_size) != 1 or
                exception_stack[0][1:3] != ['0000000000010000', 'B'] or
                exception_stack_size[0][1:3] != ['0000000000000008', 'D']):
            raise RuntimeError('Probe must override libnx weak 1 KiB exception stack with 64 KiB')
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
                    'exception_stack_bytes': 64 * 1024,
                    'startup_observation': 'main reached after sc_init; pre-main launch failures remain unobservable in the SD log',
                    'inputs': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                               for name in ['src/zero_services_test.c', 'src/platform.c',
                                            'tests/openjdk25_services_probe.cpp',
                                            'tests/openjdk25_thread_probe.cpp',
                                            'tests/openjdk25_reaper_probe.cpp',
                                            'tests/openjdk25_memory_probe.c',
                                            'src/horizon_alias.c', 'src/vmem_horizon.c',
                                            'include/javaswitch/detail/horizon_alias.h',
                                            'include/javaswitch/vmem.h', 'scripts/openjdk25.py']},
                    'elf_sha256': hashlib.sha256(elf.read_bytes()).hexdigest(),
                    'nro_sha256': hashlib.sha256(nro.read_bytes()).hexdigest()}
        (ROOT / 'build/openjdk25-probe.json').write_text(json.dumps(evidence, indent=2) + '\n')
        print('zero-services-test.nro: CROSS-BUILT; Java VM unavailable; hardware NOT TESTED')
        return 0
    if args.action in ('java-base', 'native-base', 'java-instrument', 'native-instrument'):
        java = args.action.startswith('java-')
        module_name = 'java.instrument' if args.action.endswith('-instrument') else 'java.base'
        target = module_name + ('-java' if java else '-static-libs')
        targets = [target, 'java.base-copy', 'java.base-gendata'] if args.action == 'java-base' else [target]
        # Direct javac avoids a detached compiler server surviving a cloud
        # pause or failing to publish its port file on a new module build.
        result = run(['make', 'CONF_CHECK=auto', 'JOBS=2', 'ENABLE_JAVAC_SERVER=false',
                      *([] if java else ['-k']),
                      *targets], env, args.action + '.log')
        if result == 0 and args.action == 'native-base':
            records = {}
            for module in ['java', 'zip', 'jimage', 'verify', 'net', 'nio', 'fallbackLinker', 'syslookup']:
                path = BUILD / 'support/native/java.base' / ('lib' + module) / 'static' / ('lib' + module + '.a')
                members = subprocess.check_output([str(dk / 'devkitA64/bin/aarch64-none-elf-ar'),
                                                   't', str(path)], text=True).splitlines()
                records[module] = {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'members': members}
            if 'childproc.o' in records['java']['members'] or 'osSupport_unix.o' in records['jimage']['members']:
                raise RuntimeError('A stale excluded Unix member survived the Horizon archive rebuild')
            proof = {'port_digest': (PORT / '.javaswitch-patches').read_text(),
                     'archives': records, 'horizon_execution': 'NOT TESTED'}
            (ROOT / 'build/openjdk25-native-base.json').write_text(json.dumps(proof, indent=2) + '\n')
        if result == 0 and args.action == 'native-instrument':
            records = {}
            for name, owner in [('jli_manifest', 'java.base'), ('instrument', 'java.instrument')]:
                path = BUILD / 'support/native' / owner / ('lib' + name) / 'static' / ('lib' + name + '.a')
                members = subprocess.check_output([str(dk / 'devkitA64/bin/aarch64-none-elf-ar'),
                                                   't', str(path)], text=True).splitlines()
                records[name] = {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'members': members}
            if records['jli_manifest']['members'] != ['parse_manifest.o']:
                raise RuntimeError('The manifest subset must contain only the genuine upstream parser')
            proof = {'port_digest': (PORT / '.javaswitch-patches').read_text(),
                     'archives': records, 'horizon_execution': 'NOT TESTED'}
            (ROOT / 'build/openjdk25-native-instrument.json').write_text(json.dumps(proof, indent=2) + '\n')
        if result == 0 and java:
            module = BUILD / 'jdk/modules' / module_name
            inventory = {str(path.relative_to(module)): hashlib.sha256(path.read_bytes()).hexdigest()
                         for path in sorted(module.rglob('*')) if path.is_file() and not path.name.startswith('_the.')}
            for path in module.rglob('*.class'):
                data = path.read_bytes()
                if data[:4] != b'\xca\xfe\xba\xbe' or struct.unpack_from('>H', data, 6)[0] != 69:
                    raise RuntimeError(module_name + ' contains a non-Java25 class: ' + str(path))
            proof = {'port_digest': (PORT / '.javaswitch-patches').read_text(),
                     'classfile_major': 69, 'files': inventory, 'horizon_execution': 'NOT TESTED'}
            (ROOT / ('build/openjdk25-' + args.action + '.json')).write_text(json.dumps(proof, indent=2) + '\n')
        return result
    if args.action == 'java-minecraft':
        targets = [module + '-java' for module in MINECRAFT_JAVA_MODULES]
        result = run(['make', 'CONF_CHECK=auto', 'JOBS=2', 'ENABLE_JAVAC_SERVER=false',
                      *targets], env, 'java-minecraft.log')
        if result == 0:
            for module_name in MINECRAFT_JAVA_MODULES:
                module = BUILD / 'jdk/modules' / module_name
                inventory = {str(path.relative_to(module)): hashlib.sha256(path.read_bytes()).hexdigest()
                             for path in sorted(module.rglob('*'))
                             if path.is_file() and not path.name.startswith('_the.')}
                if not inventory:
                    raise RuntimeError('Target Java module produced no files: ' + module_name)
                for path in module.rglob('*.class'):
                    data = path.read_bytes()
                    if data[:4] != b'\xca\xfe\xba\xbe' or struct.unpack_from('>H', data, 6)[0] != 69:
                        raise RuntimeError(module_name + ' contains a non-Java25 class: ' + str(path))
                proof = {'module': module_name,
                         'port_digest': (PORT / '.javaswitch-patches').read_text(),
                         'classfile_major': 69, 'files': inventory,
                         'horizon_execution': 'NOT TESTED'}
                proof_name = 'build/openjdk25-java-' + module_slug(module_name) + '.json'
                (ROOT / proof_name).write_text(json.dumps(proof, indent=2) + '\n')
        return result
    if args.action == 'native-management':
        result = run(['make', 'CONF_CHECK=auto', 'JOBS=2', 'ENABLE_JAVAC_SERVER=false',
                      'java.management-static-libs'], env, 'native-management.log')
        if result == 0:
            path = BUILD / 'support/native/java.management/libmanagement/static/libmanagement.a'
            ar = dk / 'devkitA64/bin/aarch64-none-elf-ar'
            members = subprocess.check_output([str(ar), 't', str(path)], text=True).splitlines()
            if not members:
                raise RuntimeError('Upstream libmanagement archive is empty')
            proof = {'port_digest': (PORT / '.javaswitch-patches').read_text(),
                     'archives': {'management': {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                                                 'members': members}},
                     'implementation': 'upstream java.management libmanagement + HotSpot JMM',
                     'horizon_execution': 'NOT TESTED'}
            (ROOT / 'build/openjdk25-native-management.json').write_text(
                json.dumps(proof, indent=2) + '\n')
        return result
    if args.action in ('build', 'build-minecraft'):
        if args.action == 'build-minecraft' and 'management' not in jvm_features():
            raise RuntimeError('Configure with `configure-minecraft` before building the Minecraft VM')
        log_name = 'build-minecraft.log' if args.action == 'build-minecraft' else 'build.log'
        result = run(['make', 'CONF_CHECK=auto', 'JOBS=2', 'ENABLE_JAVAC_SERVER=false', 'hotspot-zero-static-libs'], env, log_name)
        if result == 0:
            archive = BUILD / 'hotspot/variant-zero/libjvm/objs/static/libjvm.a'
            members = subprocess.check_output([str(dk / 'devkitA64/bin/aarch64-none-elf-ar'),
                                               't', str(archive)], text=True).splitlines()
            proof = {'port_digest': (PORT / '.javaswitch-patches').read_text(),
                     'jvm_features': jvm_features(),
                     'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
                     'members': members, 'full_upstream_archive_target': True,
                     'minecraft_profile': args.action == 'build-minecraft',
                     'vm_linked': False, 'horizon_execution': 'NOT TESTED'}
            proof_name = ('openjdk25-minecraft-archive.json' if args.action == 'build-minecraft'
                          else 'openjdk25-archive.json')
            (ROOT / 'build' / proof_name).write_text(json.dumps(proof, indent=2) + '\n')
        return result
    boot = next((DEPS / 'host-jdk25').glob('*/bin/javac')).parent.parent
    prefix = dk / 'devkitA64/bin/aarch64-none-elf-'
    # Use the actual W^X libffi archive and allocator, not the generic ELF
    # malloc closure allocator from libffi's intermediate build directory.
    ffi = BUILD / 'horizon-libffi'
    ffi.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'build/libffi-horizon.a', ffi / 'libffi.a')
    subprocess.run([str(prefix) + 'ar', 'r', str(ffi / 'libffi.a'),
                    str(ROOT / 'build/sdk/ffi_horizon.o')], check=True)
    arch = f'-march=armv8-a+crc+crypto -mtune=cortex-a57 -mno-outline-atomics -mtp=soft -fPIE -D__SWITCH__ -D_DEFAULT_SOURCE -I{dk}/libnx/include -I{ROOT}/include'
    command = ['bash', str(PORT / 'configure'),
               '--openjdk-target=aarch64-none-horizon', '--with-jvm-variants=zero',
               '--enable-headless-only', '--disable-warnings-as-errors',
               '--disable-javac-server',
               '--with-freetype=bundled',
               '--with-debug-level=release', f'--with-boot-jdk={boot}',
               f'CC={prefix}gcc', f'CXX={prefix}g++', f'AR={prefix}ar',
               f'NM={prefix}nm', f'STRIP={prefix}strip', f'OBJCOPY={prefix}objcopy',
               f'--with-extra-cflags={arch}', f'--with-extra-cxxflags={arch}',
               f'--with-extra-ldflags=-specs={dk}/libnx/switch.specs -L{dk}/libnx/lib',
               f'--with-libffi-include={ROOT}/build/libffi/include',
               f'--with-libffi-lib={ffi}',
               '--disable-jvm-feature-jfr', '--disable-jvm-feature-cds',
               *(['--enable-jvm-feature-services', '--enable-jvm-feature-jvmti']
                 if args.action in ('configure-agent', 'configure-minecraft') else
                 ['--disable-jvm-feature-services', '--disable-jvm-feature-jvmti']),
               '--disable-jvm-feature-g1gc', '--disable-jvm-feature-parallelgc',
               '--disable-jvm-feature-shenandoahgc', '--disable-jvm-feature-epsilongc',
               *(['--enable-jvm-feature-management'] if args.action == 'configure-minecraft'
                 else ['--disable-jvm-feature-management']),
               '--disable-jvm-feature-vm-structs']
    return run(command, env, 'configure-minecraft.log' if args.action == 'configure-minecraft'
               else 'configure.log')


def main():
    DEPS.mkdir(exist_ok=True)
    # Preparing and configuring the same port concurrently can invalidate a
    # compiler command or partially overwrite the tree. Serialize all actions.
    with (DEPS / 'openjdk25.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        return work()


if __name__ == '__main__':
    raise SystemExit(main())
