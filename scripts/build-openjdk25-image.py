#!/usr/bin/env python3
"""Build Horizon class images from actual target-built Java 25 module classes.

The host JDK supplies jmod/jlink build tools only. No host VM or native library
is copied into the output. The resulting image still requires a genuine linked
Horizon VM and its static class-library provider.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import struct
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/openjdk25-horizon'
MINECRAFT_MODULES = ['java.logging', 'jdk.unsupported', 'java.management',
                     'java.desktop', 'java.datatransfer', 'java.xml', 'java.prefs',
                     'jdk.charsets']


def module_slug(module_name):
    return module_name.removeprefix('java.').replace('.', '-')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_module(module_name):
    proof_name = module_slug(module_name)
    proof_path = ROOT / ('build/openjdk25-java-' + proof_name + '.json')
    module = BUILD / 'jdk/modules' / module_name
    if not proof_path.is_file():
        raise RuntimeError('Missing target ' + module_name + ' build proof: ' + str(proof_path))
    if not module.is_dir():
        raise RuntimeError('Missing actual target module classes: ' + str(module))
    proof = json.loads(proof_path.read_text())
    if proof.get('classfile_major') != 69 or not isinstance(proof.get('files'), dict) or not proof['files']:
        raise RuntimeError('Invalid classfile-69 target proof for ' + module_name)
    if proof.get('module', module_name) != module_name:
        raise RuntimeError('Target proof is for a different Java module: ' + module_name)
    actual = {path.relative_to(module).as_posix(): sha(path)
              for path in sorted(module.rglob('*'))
              if path.is_file() and not path.name.startswith('_the.')}
    if actual != proof['files']:
        raise RuntimeError('Target ' + module_name + ' inventory differs from proof; rerun java-' +
                           proof_name + '.')
    return module, proof


def copy_module(module_name, source_module, proof, classes_root):
    proof_name = module_name.removeprefix('java.')
    target_root = classes_root / module_name
    class_count = 0
    for name, digest in proof['files'].items():
        source = source_module / name
        if sha(source) != digest:
            raise RuntimeError('Target ' + module_name + ' changed; rerun java-' +
                               proof_name + ': ' + name)
        if source.suffix == '.class':
            data = source.read_bytes()
            if data[:4] != b'\xca\xfe\xba\xbe' or struct.unpack_from('>H', data, 6)[0] != 69:
                raise RuntimeError('Image input is not classfile69: ' + module_name + '/' + name)
            class_count += 1
        target = target_root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    return class_count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--with-instrument', action='store_true',
                        help='include target-built java.instrument classes')
    parser.add_argument('--with-minecraft', action='store_true',
                        help='include actual target-built logging, unsafe, management, desktop, preferences, datatransfer and XML modules')
    args = parser.parse_args()
    module_names = (['java.base'] + (MINECRAFT_MODULES if args.with_minecraft else []) +
                    (['java.instrument'] if args.with_instrument else []))
    module_proofs = {name: load_module(name) for name in module_names}
    port_digests = {entry[1]['port_digest'] for entry in module_proofs.values()}
    if len(port_digests) != 1:
        raise RuntimeError('Target module proofs have different port digests; rebuild selected modules together')
    native_management = None
    if args.with_minecraft:
        native_path = ROOT / 'build/openjdk25-native-management.json'
        archive_path = ROOT / 'build/openjdk25-minecraft-archive.json'
        if not native_path.is_file() or not archive_path.is_file():
            raise RuntimeError('Minecraft profile requires actual native-management and management-enabled VM build proofs')
        native_management = json.loads(native_path.read_text())
        archive = json.loads(archive_path.read_text())
        if native_management.get('port_digest') != next(iter(port_digests)) or archive.get('port_digest') != next(iter(port_digests)):
            raise RuntimeError('Minecraft native/VM proofs have a different source port digest')
        if not archive.get('minecraft_profile') or 'management' not in archive.get('jvm_features', []):
            raise RuntimeError('Minecraft image requires the management-enabled genuine HotSpot archive')
        if not native_management.get('archives', {}).get('management', {}).get('members'):
            raise RuntimeError('Missing actual upstream java.management native archive proof')
    jdk = next((ROOT / '.deps/host-jdk25').glob('*/bin/javac')).parent.parent
    output = ROOT / 'build/openjdk25-runtime'
    log_path = ROOT / 'build/openjdk25-image.log'
    with tempfile.TemporaryDirectory(prefix='openjdk25-image-', dir=ROOT / 'build') as work:
        temp = pathlib.Path(work)
        classes = temp / 'classes'
        classes.mkdir()
        class_count = sum(copy_module(name, *module_proofs[name], classes)
                          for name in module_names)

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

            legal = temp / 'legal'
            shutil.copytree(ROOT / '.deps/openjdk25-source/src/java.base/share/legal', legal)
            for name in ['LICENSE', 'ADDITIONAL_LICENSE_INFO', 'ASSEMBLY_EXCEPTION']:
                shutil.copyfile(ROOT / '.deps/openjdk25-source' / name, legal / name)
            for module_name in module_names:
                if module_name == 'java.base':
                    continue
                module_legal = ROOT / '.deps/openjdk25-source/src' / module_name / 'share/legal'
                if module_legal.is_dir():
                    shutil.copytree(module_legal, legal / module_name)

            for module_name in module_names:
                module_classes = classes / module_name
                command = [jdk / 'bin/jmod', 'create', '--class-path', module_classes,
                           '--target-platform', 'horizon-aarch64', '--module-version', '25']
                conf = BUILD / 'support/modules_conf' / module_name
                if conf.is_dir():
                    command += ['--config', conf]
                command += ['--legal-notices', legal]
                # java.instrument has no host or native inputs. java.base keeps
                # its prior target support/modules_libs input, checked above.
                if module_name == 'java.base':
                    module_libs = BUILD / 'support/modules_libs/java.base'
                    if module_libs.is_dir():
                        for path in module_libs.rglob('*'):
                            if path.is_file() and (path.suffix in ('.a', '.so', '.dll', '.exe', '.dylib') or
                                                   path.read_bytes()[:4] == b'\x7fELF'):
                                raise RuntimeError('Native payload is not part of the static class image: ' + str(path))
                        command += ['--libs', module_libs]
                command.append(jmods / (module_name + '.jmod'))
                run(command)

            if output.exists():
                shutil.rmtree(output)
            run([jdk / 'bin/java', '--patch-module', 'java.base=' + str(tool_classes),
                 '-m', 'jdk.jlink/jdk.tools.jlink.internal.Main', '--module-path', jmods,
                 '--add-modules', ','.join(module_names), '--endian', 'little', '--strip-debug',
                 '--no-header-files', '--no-man-pages', '--output', output])
            run([jdk / 'bin/jimage', 'verify', output / 'lib/modules'])
            log.write('Image modules: ' + ', '.join(module_names) + '\n')
            log.write('Image built from Horizon target classes; execution NOT TESTED\n')

        # The SDK is the executable launcher. Any host executable/library here
        # would be a packaging error, not a usable Switch native runtime.
        for path in output.rglob('*'):
            if path.is_file() and path.suffix.lower() in ('.so', '.dll', '.exe', '.dylib'):
                raise RuntimeError('Unexpected native payload in static Horizon image: ' + str(path))
        manifest = {'java_base_port_digest': module_proofs['java.base'][1]['port_digest'],
                    'module_port_digests': {name: module_proofs[name][1]['port_digest']
                                            for name in module_names},
                    'classfile_major': 69, 'class_count': class_count, 'modules': module_names,
                    'minecraft_profile': args.with_minecraft,
                    'java_instrument': args.with_instrument,
                    'native_modules': (['management'] if args.with_minecraft else []),
                    'native_management_proof': (native_management if args.with_minecraft else None),
                    'native_vm_included': False, 'horizon_execution': 'NOT TESTED',
                    'files': {str(path.relative_to(output)): sha(path)
                              for path in sorted(output.rglob('*')) if path.is_file()}}
        (ROOT / 'build/openjdk25-image.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print('Horizon ' + ', '.join(module_names) +
              ' image built; requires linked native VM; hardware NOT TESTED')


if __name__ == '__main__':
    main()
