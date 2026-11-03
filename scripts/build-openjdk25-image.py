#!/usr/bin/env python3
"""Build a minimal Horizon java.base image from actual target-built classes.

The host JDK supplies jmod/jlink build tools only. No host VM or native library
is copied into the output. The resulting image still requires a genuine linked
Horizon VM and its static class-library provider.
"""
import hashlib
import json
import pathlib
import shutil
import struct
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/openjdk25-horizon'
MODULE = BUILD / 'jdk/modules/java.base'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    proof = json.loads((ROOT / 'build/openjdk25-java-base.json').read_text())
    jdk = next((ROOT / '.deps/host-jdk25').glob('*/bin/javac')).parent.parent
    output = ROOT / 'build/openjdk25-runtime'
    log_path = ROOT / 'build/openjdk25-image.log'
    with tempfile.TemporaryDirectory(prefix='openjdk25-image-', dir=ROOT / 'build') as work:
        temp = pathlib.Path(work)
        classes = temp / 'classes'
        classes.mkdir()
        class_count = 0
        for name, digest in proof['files'].items():
            source = MODULE / name
            if sha(source) != digest:
                raise RuntimeError('Target java.base changed; rerun java-base: ' + name)
            if source.suffix == '.class':
                data = source.read_bytes()
                if data[:4] != b'\xca\xfe\xba\xbe' or struct.unpack_from('>H', data, 6)[0] != 69:
                    raise RuntimeError('Image input is not classfile69: ' + name)
                class_count += 1
            target = classes / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)

        # jlink validates its target string against OperatingSystem. Compile
        # only the maintained enum extension against the HOST PlatformProps;
        # the host tool remains Linux and can also recognize Horizon metadata.
        # Copying the target enum class here would inline Horizon host identity.
        tool_src = temp / 'tool-src/jdk/internal/util/OperatingSystem.java'
        tool_src.parent.mkdir(parents=True)
        shutil.copyfile(ROOT / '.deps/openjdk25-horizon/src/java.base/share/classes/jdk/internal/util/OperatingSystem.java', tool_src)
        tool_classes = temp / 'tool-classes'
        jmods = temp / 'jmods'
        jmods.mkdir()
        with log_path.open('w') as log:
            def run(command):
                log.write('$ ' + __import__('shlex').join(map(str, command)) + '\n')
                log.flush()
                subprocess.run(list(map(str, command)), stdout=log, stderr=subprocess.STDOUT, check=True)

            run([jdk / 'bin/javac', '--patch-module', 'java.base=' + str(temp / 'tool-src'),
                 '-d', tool_classes, tool_src])
            command = [jdk / 'bin/jmod', 'create', '--class-path', classes,
                       '--target-platform', 'horizon-aarch64', '--module-version', '25']
            conf = BUILD / 'support/modules_conf/java.base'
            if conf.is_dir():
                command += ['--config', conf]
            legal = temp / 'legal'
            shutil.copytree(ROOT / '.deps/openjdk25-source/src/java.base/share/legal', legal)
            for name in ['LICENSE', 'ADDITIONAL_LICENSE_INFO', 'ASSEMBLY_EXCEPTION']:
                shutil.copyfile(ROOT / '.deps/openjdk25-source' / name, legal / name)
            command += ['--legal-notices', legal]
            module_libs = BUILD / 'support/modules_libs/java.base'
            if module_libs.is_dir():
                for path in module_libs.rglob('*'):
                    if path.is_file() and (path.suffix in ('.a', '.so', '.dll', '.exe', '.dylib') or
                                           path.read_bytes()[:4] == b'\x7fELF'):
                        raise RuntimeError('Native payload is not part of the static class image: ' + str(path))
                command += ['--libs', module_libs]
            command.append(jmods / 'java.base.jmod')
            run(command)
            if output.exists():
                shutil.rmtree(output)
            run([jdk / 'bin/java', '--patch-module', 'java.base=' + str(tool_classes),
                 '-m', 'jdk.jlink/jdk.tools.jlink.internal.Main', '--module-path', jmods,
                 '--add-modules', 'java.base', '--endian', 'little', '--strip-debug',
                 '--no-header-files', '--no-man-pages', '--output', output])
            run([jdk / 'bin/jimage', 'verify', output / 'lib/modules'])
            log.write('Image built from Horizon target classes; execution NOT TESTED\n')

        # The SDK is the executable launcher. Any host executable/library here
        # would be a packaging error, not a usable Switch native runtime.
        for path in output.rglob('*'):
            if path.is_file() and path.suffix.lower() in ('.so', '.dll', '.exe', '.dylib'):
                raise RuntimeError('Unexpected native payload in static Horizon image: ' + str(path))
        manifest = {'java_base_port_digest': proof['port_digest'], 'classfile_major': 69,
                    'class_count': class_count, 'modules': ['java.base'],
                    'native_vm_included': False, 'horizon_execution': 'NOT TESTED',
                    'files': {str(path.relative_to(output)): sha(path)
                              for path in sorted(output.rglob('*')) if path.is_file()}}
        (ROOT / 'build/openjdk25-image.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print('Minimal Horizon java.base image built; requires linked native VM; hardware NOT TESTED')


if __name__ == '__main__':
    main()
