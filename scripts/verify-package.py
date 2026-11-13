#!/usr/bin/env python3
"""Check shipped bytes, SDK/runtime contracts and reconstruct the source port."""
import hashlib
import io
import json
import os
import pathlib
import shutil
import struct
import subprocess
import tarfile
import tempfile
import zipfile

root = pathlib.Path(__file__).resolve().parents[1]
dist = root / 'dist'
BASE_PROVIDER_MODULES={'java','zip','jimage','verify','net','nio','fallbackLinker','syslookup'}
INSTRUMENT_PROVIDER_MODULES=BASE_PROVIDER_MODULES|{'jli_manifest','instrument'}
MINECRAFT_IMAGE_MODULES={'java.logging','jdk.unsupported','java.management','java.desktop',
                         'java.datatransfer','java.xml','java.prefs','jdk.charsets'}

def jvmti_header():
    build=root/'build/openjdk25-horizon'
    candidates=[build/'jdk/include/jvmti.h',
                build/'hotspot/variant-zero/gensrc/jvmtifiles/jvmti.h',
                build/'support/modules_include/java.base/jvmti.h',
                root/'.deps/openjdk25-source/src/java.base/share/native/include/jvmti.h']
    return next((path for path in candidates if path.is_file()),None)

def sha256(data):
    if isinstance(data, pathlib.Path):
        data = data.read_bytes()
    return hashlib.sha256(data).hexdigest()

def tar_bytes(archive, name):
    member = archive.getmember(name)
    assert member.isfile(), name
    stream = archive.extractfile(member)
    assert stream is not None, name
    return stream.read()

for line in (dist / 'SHA256SUMS').read_text().splitlines():
    digest, name = line.split('  ', 1)
    assert sha256(dist / name) == digest, name
manifest = json.loads((dist / 'manifest.json').read_text())
assert manifest['horizon_execution'] == 'NOT TESTED'
assert manifest['sdk_abi'] == 1
assert 'NOT INCLUDED' in manifest['java25_sdk_availability']
assert 'ABI 1' in manifest['java25_sdk_availability'] and 'VM-free' in manifest['java25_sdk_availability']
assert 'separate' in manifest['baseline_jni_host_tests']
for artifact in manifest['artifacts']:
    assert sha256(dist / artifact['name']) == artifact['sha256']
    if artifact['name'] in {'java25-test.nro', 'openjdk25-horizon-runtime.tar.gz'}:
        assert artifact['hardware_verification'] == 'NOT TESTED'
        assert 'UNVERIFIED' in ' '.join(artifact['known_limitations']) or 'NOT TESTED' in ' '.join(artifact['known_limitations'])

with tarfile.open(dist / 'javaswitch-horizon-sdk.tar.gz') as sdk:
    assert tar_bytes(sdk, 'include/javaswitch/runtime.h') == (root / 'include/javaswitch/runtime.h').read_bytes()
    library = tar_bytes(sdk, 'lib/libjavaswitch.a')
    assert library == (root / 'build/sdk/libjavaswitch.a').read_bytes()
    contract = json.loads(tar_bytes(sdk, 'runtime-contract.json'))
    assert contract['abi'] == 1 and contract['built_backends']['openjdk25'] is False
    assert contract == json.loads((root / 'build/sdk/runtime-contract.json').read_text())
    names = set(sdk.getnames())
    assert 'licenses/LIBFFI-MIT.txt' in names
    print('SDK ABI v1 header/library and notices; no Java 25 VM: PASS')
    print('libjavaswitch.a SHA256 ' + sha256(library))

probe_path = dist / 'zero-services-test.nro'
java25_nro_path = dist / 'java25-test.nro'
probe_exists = probe_path.is_file()
java25_exists = java25_nro_path.is_file()
if java25_exists:
    minecraft = manifest.get('java25_minecraft_profile',False)
    profile_path=root/'build/openjdk25-runtime-profile.json'
    profile=json.loads(profile_path.read_text())
    assert profile.get('minecraft_profile',False) is minecraft
    runtime_proof_path=root/('build/openjdk25-runtime-link-minecraft.json' if minecraft
                             else 'build/openjdk25-runtime-link.json')
    runtime_proof = json.loads(runtime_proof_path.read_text())
    image_proof = json.loads((root / 'build/openjdk25-image.json').read_text())
    java_base_proof = json.loads((root / 'build/openjdk25-java-base.json').read_text())
    assert isinstance(runtime_proof['java_instrument'],bool)
    instrument = runtime_proof['java_instrument']
    assert isinstance(runtime_proof['jvm_features'],list) and runtime_proof['jvm_features']
    assert manifest['java25_instrumentation_available'] is instrument
    expected_image_modules={'java.base'}
    if instrument:expected_image_modules.add('java.instrument')
    if minecraft:expected_image_modules |= MINECRAFT_IMAGE_MODULES
    assert set(manifest['java25_runtime_image_modules']) == expected_image_modules
    assert manifest['java25_minecraft_profile'] is minecraft
    assert manifest['java25_jvm_features'] == runtime_proof['jvm_features']
    if instrument:
        assert {'jvmti','services'} <= set(runtime_proof['jvm_features'])
        instrument_proof = json.loads((root / 'build/openjdk25-java-instrument.json').read_text())
        assert instrument_proof['classfile_major'] == 69 and instrument_proof['files']
        agent_proof=json.loads((root/'build/openjdk25-agent-host.json').read_text())
        assert agent_proof['host_tests']=={'startup_agent':'PASS','no_agent_control':'PASS'}
        assert agent_proof['classfile_major']==69 and agent_proof['horizon_execution']=='NOT TESTED'
        for name,digest in agent_proof['inputs'].items():assert sha256(root/name)==digest
        for name,digest in agent_proof['jars'].items():assert sha256(root/'build'/name)==digest
    else:
        instrument_proof = None
    assert runtime_proof['link_exit_status'] == 0 and runtime_proof['missing_symbols'] == []
    assert runtime_proof['java_execution'] == 'NOT TESTED'
    assert runtime_proof['horizon_execution'] == 'NOT TESTED'
    assert manifest['horizon_execution'] == 'NOT TESTED'
    assert 'EXPERIMENTAL' in manifest['java25_runtime']
    for claim in ['startup UNVERIFIED', 'classfile execution NOT TESTED', 'hardware NOT TESTED']:
        assert claim in manifest['java25_runtime']
    assert 'NOT INCLUDED' in manifest['java25_sdk_availability']
    assert 'Minecraft' not in manifest['java25_runtime']
    assert image_proof['classfile_major'] == java_base_proof['classfile_major'] == 69
    assert image_proof['native_vm_included'] is False
    assert runtime_proof.get('minecraft_profile',False) is minecraft
    expected_image_modules={'java.base'}
    if instrument:expected_image_modules.add('java.instrument')
    if minecraft:expected_image_modules |= MINECRAFT_IMAGE_MODULES
    assert set(image_proof['modules']) == expected_image_modules
    assert image_proof.get('minecraft_profile',False) is minecraft
    assert image_proof.get('java_instrument',False) is instrument
    assert image_proof['java_base_port_digest'] == java_base_proof['port_digest']
    if instrument:
        assert set(image_proof['module_port_digests']) == expected_image_modules
        assert image_proof['module_port_digests']['java.base'] == java_base_proof['port_digest']
        assert image_proof['module_port_digests']['java.instrument'] == instrument_proof['port_digest']
    elif 'module_port_digests' in image_proof and not minecraft:
        assert image_proof['module_port_digests'] == {'java.base':java_base_proof['port_digest']}
    if minecraft:
        assert image_proof['native_modules']==['management']
        management=json.loads((root/'build/openjdk25-native-management.json').read_text())
        assert management['port_digest']==runtime_proof['archive_port_digest']
        assert image_proof['native_management_proof']==management
        assert set(image_proof['module_port_digests'])==expected_image_modules
        for module in MINECRAFT_IMAGE_MODULES:
            slug=module.removeprefix('java.').replace('.','-')
            module_proof=json.loads((root/('build/openjdk25-java-'+slug+'.json')).read_text())
            assert module_proof['module']==module
            assert module_proof['port_digest']==runtime_proof['archive_port_digest']
            assert image_proof['module_port_digests'][module]==module_proof['port_digest']
    image_root = root / 'build/openjdk25-runtime'
    actual_image = {p.relative_to(image_root).as_posix() for p in image_root.rglob('*') if p.is_file()}
    assert actual_image == set(image_proof['files'])
    for rel, digest in image_proof['files'].items():
        path = image_root / rel
        assert path.is_file() and sha256(path) == digest, rel

    # The host jimage utility checks/extracts the image format. It does not run
    # the target VM; inspect an extracted class header to verify major version.
    pinned_jimage = next((root / '.deps/host-jdk25').glob('*/bin/jimage'), None)
    jimage = str(pinned_jimage) if pinned_jimage else shutil.which('jimage')
    assert jimage, 'Host JDK jimage tool is required to verify the packaged image'
    modules_image = image_root / 'lib/modules'
    subprocess.run([jimage, 'verify', str(modules_image)], check=True)
    with tempfile.TemporaryDirectory(dir=root / 'build') as temporary:
        extracted = pathlib.Path(temporary) / 'image'
        subprocess.run([jimage, 'extract', '--dir', str(extracted), str(modules_image)],
                       check=True, stdout=subprocess.DEVNULL)
        object_class = extracted / 'java.base/java/lang/Object.class'
        class_bytes = object_class.read_bytes()
        assert class_bytes[:4] == b'\xca\xfe\xba\xbe'
        assert struct.unpack_from('>H', class_bytes, 6)[0] == 69

    link_dir='runtime-link-minecraft' if minecraft else 'runtime-link'
    elf_path = root / 'build/openjdk25-horizon' / link_dir / 'java25-test.elf'
    nro = java25_nro_path.read_bytes()
    elf = elf_path.read_bytes()
    assert sha256(elf_path) == runtime_proof['elf_sha256']
    assert sha256(nro) == runtime_proof['nro_sha256']
    assert elf[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<HH', elf, 16) == (3, 183)
    assert nro[16:20] == b'NRO0'
    nm = pathlib.Path(os.environ.get(
        'DEVKITPRO', str(root / '.toolchain/opt/devkitpro')))
    nm = nm / 'devkitA64/bin/aarch64-none-elf-nm'
    symbols = subprocess.check_output([str(nm), str(elf_path)], text=True)
    assert ' T JNI_CreateJavaVM' in symbols
    if instrument:
        for symbol in ['JNI_OnLoad_instrument','Agent_OnLoad_instrument','JLI_ManifestIterate']:
            assert any(line.split()[-1:]==[symbol] for line in symbols.splitlines()), symbol
    assert sha256(root / 'src/java25_vm_test.c') == runtime_proof['launcher_sha256']
    archive_proof = json.loads((root / ('build/openjdk25-minecraft-archive.json' if minecraft
                                        else 'build/openjdk25-archive.json')).read_text())
    assert archive_proof['full_upstream_archive_target'] is True
    assert archive_proof['archive_sha256'] == runtime_proof['archive_sha256']
    assert archive_proof['port_digest'] == runtime_proof['archive_port_digest']
    assert archive_proof['jvm_features'] == runtime_proof['jvm_features']
    assert archive_proof.get('minecraft_profile',False) is minecraft
    if minecraft:
        assert 'management' in archive_proof['jvm_features']

    integration_path = dist / 'openjdk25-horizon-runtime.tar.gz'
    assert integration_path.is_file()
    integration_root = 'openjdk25-horizon-runtime/'
    native_archive = root / 'build/openjdk25-horizon/hotspot/variant-zero/libjvm/objs/static/libjvm.a'
    assert sha256(native_archive) == runtime_proof['archive_sha256']
    provider = runtime_proof['class_library_provider']
    expected_modules=INSTRUMENT_PROVIDER_MODULES if instrument else BASE_PROVIDER_MODULES
    if minecraft:expected_modules=expected_modules|{'management'}
    assert set(provider['modules'])==expected_modules
    link_dir='runtime-link-minecraft' if minecraft else 'runtime-link'
    resolver_dir = root / 'build/openjdk25-horizon' / link_dir
    resolver_json = resolver_dir / 'native_resolver.json'
    resolver_c = resolver_dir / 'native_resolver.c'
    assert json.loads(resolver_json.read_text()) == provider
    native_archives = {}
    excluded_system_archives = set()
    for item in provider['archives']:
        path = pathlib.Path(item['path'])
        assert path.is_file() and sha256(path) == item['sha256']
        if path.name in {'libc.a', 'libm.a'}:
            excluded_system_archives.add(path.name)
            continue
        module = item['module']
        assert module not in native_archives and path.name == 'lib' + module + '.a'
        native_archives[module] = path
    assert set(native_archives) == expected_modules
    assert excluded_system_archives == {'libc.a', 'libm.a'}
    with tarfile.open(integration_path) as package:
        package_names = set(package.getnames())
        assert tar_bytes(package, integration_root + 'native/libjvm.a') == native_archive.read_bytes()
        for module, path in native_archives.items():
            packaged_name = integration_root + 'native/lib' + module + '.a'
            assert tar_bytes(package, packaged_name) == path.read_bytes()
        static_members = {name.removeprefix(integration_root + 'native/') for name in package_names
                          if name.startswith(integration_root + 'native/') and name.endswith('.a')}
        assert static_members == {'libjvm.a'} | {'lib' + module + '.a' for module in expected_modules}
        assert not any(name.endswith('.so') or '.so.' in name for name in package_names)
        assert integration_root + 'native/libc.a' not in package_names
        assert integration_root + 'native/libm.a' not in package_names
        assert tar_bytes(package, integration_root + 'native/native_resolver.c') == resolver_c.read_bytes()
        assert tar_bytes(package, integration_root + 'native/native_resolver.json') == resolver_json.read_bytes()
        jdk_include = root / 'build/openjdk25-horizon/jdk/include'
        jni_h = jdk_include / 'jni.h'
        jni_md = jdk_include / 'horizon/jni_md.h'
        assert tar_bytes(package, integration_root + 'include/jni.h') == jni_h.read_bytes()
        assert tar_bytes(package, integration_root + 'include/horizon/jni_md.h') == jni_md.read_bytes()
        if instrument:
            jvmti_h=jvmti_header()
            assert jvmti_h is not None
            assert tar_bytes(package,integration_root+'include/jvmti.h')==jvmti_h.read_bytes()
        else:
            assert integration_root+'include/jvmti.h' not in package_names
        for header in (root / 'include').rglob('*'):
            if header.is_file():
                assert tar_bytes(package, integration_root + header.relative_to(root).as_posix()) == header.read_bytes()
        packaged_image = {name.removeprefix(integration_root + 'runtime/')
                          for name in package_names if name.startswith(integration_root + 'runtime/')
                          and name[len(integration_root + 'runtime/'):].count('/') >= 0}
        assert packaged_image == set(image_proof['files'])
        for rel, digest in image_proof['files'].items():
            contents = tar_bytes(package, integration_root + 'runtime/' + rel)
            assert sha256(contents) == digest
        package_manifest = json.loads(tar_bytes(package, integration_root + 'runtime-package.json'))
        assert package_manifest['runtime_startup'] == 'UNVERIFIED'
        assert package_manifest['classfile_execution'] == 'NOT TESTED'
        assert package_manifest['experimental_hardware'] == 'NOT TESTED'
        assert package_manifest['minecraft_or_title_execution'] == 'NOT CLAIMED'
        assert package_manifest['sdk_dependency'] == 'javaswitch-horizon-sdk.tar.gz (ABI 1; no VM)'
        assert package_manifest['runtime_image_files'] == image_proof['files']
        assert package_manifest['java_instrument'] is instrument
        assert package_manifest['minecraft_profile'] is minecraft
        assert package_manifest['runtime_image_modules'] == image_proof['modules']
        assert package_manifest['jvm_features'] == runtime_proof['jvm_features']
        assert package_manifest['elf_sha256'] == sha256(elf_path)
        assert package_manifest['nro_sha256'] == sha256(nro)
        assert package_manifest['upstream_libjvm_archive_sha256'] == sha256(native_archive)
        assert package_manifest['sdk_libjavaswitch_sha256'] == sha256(root / 'build/sdk/libjavaswitch.a')
        assert package_manifest['launcher_sha256'] == sha256(root / 'src/java25_vm_test.c')
        assert package_manifest['native_archives'] == {
            module: {'name': path.name, 'sha256': sha256(path)} for module, path in sorted(native_archives.items())}
        assert package_manifest['native_resolver'] == {
            'c_sha256': sha256(resolver_c), 'json_sha256': sha256(resolver_json)}
        assert package_manifest['jni_headers'] == {
            'jni.h': sha256(jni_h), 'horizon/jni_md.h': sha256(jni_md)}
        if instrument:
            assert package_manifest['jvmti_header_sha256'] == sha256(jvmti_h)
            smoke_jars={name:sha256(root/'build'/name) for name in
                        ['java25-agent-smoke-agent.jar','java25-agent-smoke-app.jar']}
            assert package_manifest['agent_smoke_jars'] == smoke_jars
            for name,digest in smoke_jars.items():
                packaged=integration_root+'smoke-tests/'+name
                assert sha256(tar_bytes(package,packaged))==digest
        else:
            assert package_manifest['agent_smoke_jars'] == {}
            assert 'jvmti_header_sha256' not in package_manifest
            assert not any(name.startswith(integration_root+'smoke-tests/') for name in package_names)
        assert package_manifest['system_archives_verified_not_bundled'] == {
            path.name: sha256(path) for path in sorted(
                pathlib.Path(item['path']) for item in provider['archives']
                if pathlib.Path(item['path']).name in {'libc.a', 'libm.a'})}
        sdk_dependency = tar_bytes(package, integration_root + 'SDK-DEPENDENCY.txt').decode()
        assert 'javaswitch-horizon-sdk.tar.gz' in sdk_dependency
        assert 'version 1' in sdk_dependency and 'contains no Java VM' in sdk_dependency
        for notice in ['LICENSE', 'ADDITIONAL_LICENSE_INFO']:
            name = integration_root + 'source-notices/' + notice
            assert name in package_names
            assert tar_bytes(package, name) == (root / '.deps/openjdk25-source' / notice).read_bytes()
    print('OpenJDK 25 runtime image, classfile 69, VM/archive/provider and JNI package bytes: PASS')
else:
    assert not (dist / 'openjdk25-horizon-runtime.tar.gz').exists()
    assert 'not built' in manifest['java25_runtime'].lower()

with zipfile.ZipFile(dist / 'java-switch-test-package.zip') as sd:
    for name in ['java-test', 'ffi-test', 'zero-services-test']:
        path = dist / (name + '.nro')
        if path.exists():
            assert sd.read(f'switch/{name}/{name}.nro') == path.read_bytes()
    if java25_exists:
        assert sd.read('switch/java25-test/java25-test.nro') == java25_nro_path.read_bytes()
        for rel, digest in image_proof['files'].items():
            name = 'switch/mc-runtime/runtimes/openjdk25/' + rel
            contents = sd.read(name)
            assert sha256(contents) == digest, name
        staged = {name.removeprefix('switch/mc-runtime/runtimes/openjdk25/')
                  for name in sd.namelist()
                  if name.startswith('switch/mc-runtime/runtimes/openjdk25/')}
        assert staged == set(image_proof['files'])
        if instrument:
            for name in ['java25-agent-smoke-agent.jar','java25-agent-smoke-app.jar']:
                packaged='switch/mc-runtime/tests/'+name
                assert sha256(sd.read(packaged))==sha256(root/'build'/name)
        else:
            assert not any(name.startswith('switch/mc-runtime/tests/java25-agent-smoke-')
                           for name in sd.namelist())
    else:
        assert 'switch/java25-test/java25-test.nro' not in sd.namelist()
        assert not any(name.startswith('switch/mc-runtime/runtimes/openjdk25/') for name in sd.namelist())
    assert sd.read('java-switch-source.tar.gz') == (dist / 'java-switch-source.tar.gz').read_bytes()
    print('SD ZIP binaries, verified Java 25 image and accompanying source: PASS')

if probe_exists or java25_exists:
    with tarfile.open(dist / 'java-switch-source.tar.gz') as bundle:
        upstream = tar_bytes(bundle, 'third-party/openjdk25-upstream.tar.gz')
        lock = json.loads(tar_bytes(bundle, 'java-switch/dependencies.lock.json'))
        assert sha256(upstream) == lock['openjdk25_source']['sha256']
        source_members = {p.name: p for p in bundle.getmembers()}
        patches = {p.name: bundle.extractfile(p).read() for p in bundle
                   if p.isfile() and p.name.startswith('java-switch/patches/openjdk25/') and p.name.endswith('.patch')}
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
            overlay_members = {p.name.removeprefix(prefix): p for p in bundle
                               if p.isfile() and p.name.startswith(prefix + 'src/')}
            from openjdk25 import port_inputs, PORT
            port_patches, overlay, files, digest = port_inputs()
            assert set(overlay_members) == {str(path.relative_to(overlay)) for path in files}
            if probe_exists:
                assert digest == json.loads((root / 'build/openjdk25-probe.json').read_text())['port_digest']
            if java25_exists:
                assert digest == runtime_proof['archive_port_digest']
            for name in originals | {'src/java.base/horizon/native/include/jni_md.h',
                                     'src/java.base/horizon/conf/net.properties'}:
                assert (target / name).read_bytes() == (PORT / name).read_bytes(), name
            for relative, member in overlay_members.items():
                expected_bytes = bundle.extractfile(member).read()
                assert expected_bytes == (overlay / relative).read_bytes(), relative
                destination = target / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(expected_bytes)
                assert destination.read_bytes() == expected_bytes, relative
        print('Full pinned OpenJDK archive, tracked zero-fuzz patches and every native overlay reconstructed: PASS')
print('All artifact SHA256 checks: PASS; Horizon hardware NOT TESTED')
