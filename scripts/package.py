#!/usr/bin/env python3
import datetime,hashlib,io,json,os,pathlib,shutil,struct,subprocess,tarfile,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
DIST=ROOT/'dist';nro=DIST/'java-test.nro';data=nro.read_bytes()
elf=(ROOT/'build/java-test.elf').read_bytes()

def sha256(path):return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()

def copy_verified_image(proof, destination):
    assert proof['classfile_major']==69 and proof['modules']==['java.base']
    assert proof['native_vm_included'] is False and proof['horizon_execution']=='NOT TESTED'
    image_root=ROOT/'build/openjdk25-runtime'
    assert set(proof['files'])
    actual={path.relative_to(image_root).as_posix() for path in image_root.rglob('*') if path.is_file()}
    assert actual==set(proof['files']), 'Java 25 runtime image contents differ from its proof'
    for rel,digest in proof['files'].items():
        source=image_root/rel
        assert source.is_file() and sha256(source)==digest, 'Java 25 image file hash mismatch: '+rel
        target=destination/rel;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source,target)

def validate_java25_runtime():
    proof=json.loads((ROOT/'build/openjdk25-runtime-link.json').read_text())
    assert proof['link_exit_status']==0 and proof['missing_symbols']==[]
    assert proof['java_execution']=='NOT TESTED' and proof['horizon_execution']=='NOT TESTED'
    elf_path=ROOT/'build/openjdk25-horizon/runtime-link/java25-test.elf'
    nro_path=DIST/'java25-test.nro'
    archive_path=ROOT/'build/openjdk25-horizon/hotspot/variant-zero/libjvm/objs/static/libjvm.a'
    sdk_path=ROOT/'build/sdk/libjavaswitch.a'
    sdk_contract=json.loads((ROOT/'build/sdk/runtime-contract.json').read_text())
    launcher_path=ROOT/'src/java25_vm_test.c'
    for path in [elf_path,nro_path,archive_path,sdk_path,launcher_path]:assert path.is_file(), 'Missing Java 25 runtime input: '+str(path)
    assert sha256(elf_path)==proof['elf_sha256'] and sha256(nro_path)==proof['nro_sha256']
    assert sha256(archive_path)==proof['archive_sha256'] and sha256(sdk_path)==proof['sdk_sha256']
    assert sha256(launcher_path)==proof['launcher_sha256']
    archive_proof=json.loads((ROOT/'build/openjdk25-archive.json').read_text())
    assert archive_proof['full_upstream_archive_target'] is True
    assert archive_proof['archive_sha256']==proof['archive_sha256']
    assert archive_proof['port_digest']==proof['archive_port_digest']
    assert sdk_contract['abi']==1 and sdk_contract['built_backends']['openjdk25'] is False
    linked=elf_path.read_bytes();binary=nro_path.read_bytes()
    assert linked[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<HH',linked,16)==(3,183), 'Java 25 ELF must be AArch64 PIE'
    assert binary[16:20]==b'NRO0'
    dk=pathlib.Path(os.environ.get('DEVKITPRO',str(ROOT/'.toolchain/opt/devkitpro')))
    nm=dk/'devkitA64/bin/aarch64-none-elf-nm'
    symbols=subprocess.check_output([str(nm),str(elf_path)],text=True)
    assert ' T JNI_CreateJavaVM' in symbols and ' T JNI_OnLoad_java' in symbols

    provider=proof['class_library_provider']
    resolver_json=ROOT/'build/openjdk25-horizon/runtime-link/native_resolver.json'
    resolver_c=ROOT/'build/openjdk25-horizon/runtime-link/native_resolver.c'
    assert resolver_json.is_file() and resolver_c.is_file()
    assert json.loads(resolver_json.read_text())==provider
    assert provider['modules']==['fallbackLinker','java','jimage','net','nio','syslookup','verify','zip']
    assert provider['generator_sha256']==sha256(ROOT/'scripts/generate-openjdk25-native-resolver.py')
    assert provider['source_sha256']==sha256(resolver_c)
    expected_modules={'java','zip','jimage','verify','net','nio','fallbackLinker','syslookup'}
    native_archives={};system_archives={}
    for item in provider['archives']:
        path=pathlib.Path(item['path'])
        assert path.is_file() and sha256(path)==item['sha256'], 'Provider archive hash mismatch: '+str(path)
        if path.name in ('libc.a','libm.a'):
            system_archives[path.name]=path
        else:
            module=item['module'];assert module in expected_modules and module not in native_archives
            assert path.name=='lib'+module+'.a'
            native_archives[module]=path
    assert set(native_archives)==expected_modules and set(system_archives)=={'libc.a','libm.a'}

    image_proof=json.loads((ROOT/'build/openjdk25-image.json').read_text())
    java_proof=json.loads((ROOT/'build/openjdk25-java-base.json').read_text())
    assert image_proof['java_base_port_digest']==java_proof['port_digest']
    assert image_proof['classfile_major']==java_proof['classfile_major']==69
    assert java_proof['files']
    image_root=ROOT/'build/openjdk25-runtime'
    actual={path.relative_to(image_root).as_posix() for path in image_root.rglob('*') if path.is_file()}
    assert actual==set(image_proof['files']), 'Java 25 runtime image contents differ from its proof'
    for rel,digest in image_proof['files'].items():
        path=image_root/rel
        assert path.is_file() and sha256(path)==digest, 'Java 25 runtime image hash mismatch: '+rel
    return proof,elf_path,nro_path,archive_path,native_archives,resolver_json,resolver_c,image_proof
assert data[16:20]==b'NRO0' and elf[:6]==b'\x7fELF\x02\x01'
assert struct.unpack_from('<HH',elf,16)==(3,183)
size=struct.unpack_from('<I',data,24)[0];assert data[size:size+4]==b'ASET'
apps=['java-test','ffi-test']
probe=DIST/'zero-services-test.nro'
java25_nro=DIST/'java25-test.nro'
java25=None
if probe.exists() or java25_nro.exists():
    from openjdk25 import port_inputs
    port_patches,_,port_files,port_digest=port_inputs()
    tracked=set(subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode().split('\0'))
    for path in port_patches+port_files:
        assert str(path.relative_to(ROOT)) in tracked, 'Stage all port sources before packaging: '+str(path)
    if probe.exists():
        proof=json.loads((ROOT/'build/openjdk25-probe.json').read_text())
        assert proof['port_digest']==port_digest and proof['java_vm_linked'] is False
        for name,digest in proof['inputs'].items():assert sha256(ROOT/name)==digest
        assert sha256(probe)==proof['nro_sha256']
        apps.append('zero-services-test')
if java25_nro.exists():
    java25=validate_java25_runtime()
    apps.append('java25-test')
for app in apps:
    linked_path=(ROOT/'build/openjdk25-horizon/components/zero-services-test.elf' if app=='zero-services-test'
                 else java25[1] if app=='java25-test' else ROOT/'build'/(app+'.elf'))
    binary=(DIST/(app+'.nro')).read_bytes();linked=linked_path.read_bytes()
    if app=='zero-services-test':assert hashlib.sha256(linked).hexdigest()==proof['elf_sha256']
    if app=='java25-test':assert hashlib.sha256(linked).hexdigest()==java25[0]['elf_sha256']
    assert linked[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<HH',linked,16)==(3,183)
    assert binary[16:20]==b'NRO0'
    length=struct.unpack_from('<I',binary,24)[0];assert length<len(binary) and binary[length:length+4]==b'ASET'
    assert b'GNU\0'+binary[64:84] in linked
    off,n=struct.unpack_from('<QQ',binary,length+24);assert n==0x4000
    nacp=binary[length+off:length+off+n]
    assert nacp[:0x200].split(b'\0')[0].decode()==app
    assert nacp[0x200:0x300].split(b'\0')[0]==b'JavaSwitch'
    assert nacp[0x3060:0x3070].split(b'\0')[0]==b'0.2.0'
sd=DIST/'COPY_TO_SD';runtime=sd/'switch/mc-runtime'
if sd.exists():shutil.rmtree(sd)
(sd/'switch/java-test').mkdir(parents=True);shutil.copyfile(nro,sd/'switch/java-test/java-test.nro')
(sd/'switch/ffi-test').mkdir(parents=True);shutil.copyfile(DIST/'ffi-test.nro',sd/'switch/ffi-test/ffi-test.nro')
if probe.exists():
    (sd/'switch/zero-services-test').mkdir(parents=True)
    shutil.copyfile(probe,sd/'switch/zero-services-test/zero-services-test.nro')
if java25:
    (sd/'switch/java25-test').mkdir(parents=True)
    shutil.copyfile(java25_nro,sd/'switch/java25-test/java25-test.nro')
for path in ['logs','tests','runtimes/minijvm','instances/java-smoke','licenses','tmp']:(runtime/path).mkdir(parents=True)
shutil.copyfile(ROOT/'build/minijvm_rt.jar',runtime/'runtimes/minijvm/minijvm_rt.jar')
for name in ['diagnostic-tests.jar','dependency.jar']:shutil.copyfile(ROOT/'build'/name,runtime/'tests'/name)
shutil.copyfile(ROOT/'build/java25-tests.jar',runtime/'tests/java25-tests.jar')
shutil.copyfile(ROOT/'build/java25-hello.jar',runtime/'tests/java25-hello.jar')
if java25:
    copy_verified_image(java25[7],runtime/'runtimes/openjdk25')
with tarfile.open(DIST/'javaswitch-horizon-sdk.tar.gz','w:gz',dereference=True) as archive:
    archive.add(ROOT/'include',arcname='include')
    archive.add(ROOT/'build/sdk/libjavaswitch.a',arcname='lib/libjavaswitch.a')
    archive.add(ROOT/'build/sdk/runtime-contract.json',arcname='runtime-contract.json')
    archive.add(ROOT/'build/libffi/include/ffi.h',arcname='include/ffi.h')
    archive.add(ROOT/'build/libffi/include/ffitarget.h',arcname='include/ffitarget.h')
    archive.add(ROOT/'LICENSE',arcname='LICENSE')
    archive.add(ROOT/'licenses',arcname='licenses')
shutil.copytree(ROOT/'licenses',runtime/'licenses',dirs_exist_ok=True)
shutil.copyfile(ROOT/'README.md',sd/'JAVA-SWITCH-README.md')
with tarfile.open(DIST/'java-switch-source.tar.gz','w:gz') as archive:
    for name in subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).split(b'\0'):
        if name:archive.add(ROOT/name.decode(),arcname='java-switch/'+name.decode())
    archive.add(ROOT/'.deps/minijvm-source',arcname='third-party/minijvm-source')
    archive.add(ROOT/'.deps/minijvm',arcname='third-party/minijvm-horizon-c')
    archive.add(ROOT/'.deps/libffi-source',arcname='third-party/libffi-source')
    if probe.exists() or java25:
        upstream=ROOT/'.deps/openjdk25.archive'
        locked=json.loads((ROOT/'dependencies.lock.json').read_text())['openjdk25_source']
        assert hashlib.sha256(upstream.read_bytes()).hexdigest()==locked['sha256']
        archive.add(upstream,arcname='third-party/openjdk25-upstream.tar.gz')
if java25:
    proof,elf_path,nro_path,archive_path,native_archives,resolver_json,resolver_c,image_proof=java25
    package_root=pathlib.Path('openjdk25-horizon-runtime')
    jdk_include=ROOT/'build/openjdk25-horizon/jdk/include'
    jni_h=jdk_include/'jni.h';jni_md=jdk_include/'horizon/jni_md.h'
    assert jni_h.is_file() and jni_md.is_file()
    sdk_headers=ROOT/'include'
    sdk_note=('The baseline Horizon JavaSwitch SDK remains a separate dependency: '
              'use javaswitch-horizon-sdk.tar.gz for libjavaswitch.a and the public SDK headers. '
              'Its runtime ABI remains version 1 and contains no Java VM.\n')
    pkg_manifest={
        'runtime':'Experimental OpenJDK 25 Zero AArch64 Horizon static integration package',
        'cross_built_genuine_vm':True,
        'runtime_startup':'UNVERIFIED',
        'classfile_execution':'NOT TESTED',
        'experimental_hardware':'NOT TESTED',
        'minecraft_or_title_execution':'NOT CLAIMED',
        'link_exit_status':proof['link_exit_status'],
        'elf_sha256':sha256(elf_path),'nro_sha256':sha256(nro_path),
        'upstream_libjvm_archive_sha256':sha256(archive_path),
        'archive_port_digest':proof['archive_port_digest'],
        'sdk_libjavaswitch_sha256':sha256(ROOT/'build/sdk/libjavaswitch.a'),
        'launcher_sha256':sha256(ROOT/'src/java25_vm_test.c'),
        'native_archives':{module:{'name':path.name,'sha256':sha256(path)} for module,path in sorted(native_archives.items())},
        'system_archives_verified_not_bundled':{path.name:sha256(path) for path in sorted(
            [pathlib.Path(item['path']) for item in proof['class_library_provider']['archives']
             if pathlib.Path(item['path']).name in {'libc.a','libm.a'}])},
        'native_resolver':{'c_sha256':sha256(resolver_c),'json_sha256':sha256(resolver_json)},
        'jni_headers':{'jni.h':sha256(jni_h),'horizon/jni_md.h':sha256(jni_md)},
        'runtime_image_files':image_proof['files'],
        'sdk_dependency':'javaswitch-horizon-sdk.tar.gz (ABI 1; no VM)'
    }
    pkg_path=DIST/'openjdk25-horizon-runtime.tar.gz'
    with tarfile.open(pkg_path,'w:gz',dereference=True) as archive:
        def add_file(path, name):archive.add(path,arcname=str(package_root/name))
        add_file(archive_path,'native/libjvm.a')
        for module,path in sorted(native_archives.items()):add_file(path,'native/lib'+module+'.a')
        add_file(resolver_c,'native/native_resolver.c')
        add_file(resolver_json,'native/native_resolver.json')
        add_file(jni_h,'include/jni.h')
        add_file(jni_md,'include/horizon/jni_md.h')
        for path in sorted(sdk_headers.rglob('*')):
            if path.is_file():add_file(path,path.relative_to(ROOT))
        # The runtime image proof is also the exact allowlist for staged files.
        for rel in sorted(image_proof['files']):add_file(ROOT/'build/openjdk25-runtime'/rel,'runtime/'+rel)
        for license_name in ['LICENSE','ADDITIONAL_LICENSE_INFO']:
            source=ROOT/'.deps/openjdk25-source'/license_name
            if source.is_file():add_file(source,'source-notices/'+license_name)
        add_file(ROOT/'licenses','source-notices/java-switch-licenses')
        note=tarfile.TarInfo(str(package_root/'SDK-DEPENDENCY.txt'));note.size=len(sdk_note.encode())
        archive.addfile(note,io.BytesIO(sdk_note.encode()))
        payload=json.dumps(pkg_manifest,indent=2).encode()+b'\n'
        info=tarfile.TarInfo(str(package_root/'runtime-package.json'));info.size=len(payload)
        archive.addfile(info,io.BytesIO(payload))
else:
    (DIST/'openjdk25-horizon-runtime.tar.gz').unlink(missing_ok=True)
shutil.copyfile(DIST/'java-switch-source.tar.gz',sd/'java-switch-source.tar.gz')
with zipfile.ZipFile(DIST/'java-switch-test-package.zip','w',zipfile.ZIP_DEFLATED) as z:
    for path in sorted(sd.rglob('*')):
        if path.is_file():z.write(path,path.relative_to(sd))
manifest={'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
    'source_tree_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT)),
    'horizon_execution':'NOT TESTED','java8_interpreter':'built and host-tested',
    'java25_runtime':('EXPERIMENTAL: genuine cross-built OpenJDK 25 Zero VM linked into an AArch64 Horizon PIE and NRO; runtime startup UNVERIFIED; classfile execution NOT TESTED; hardware NOT TESTED' if java25 else 'not built'),
    'java25_sdk_availability':'NOT INCLUDED; baseline Horizon SDK ABI 1 remains VM-free',
    'baseline_jni_host_tests':'Host Java 25 JNI/FFM tests are separate from Horizon runtime startup and execution evidence',
    'openjdk25_services_probe':'CROSS-BUILT; time/synchronization/native-thread/SafeFetch probes; no VM; faults NOT TESTED on hardware' if probe.exists() else 'not built',
    'standard_jni_invocation':('JNI_CreateJavaVM is present in a cross-built Horizon ELF/NRO; startup and Java execution NOT TESTED' if java25 else 'Host Java 25 JNI/FFM tested; no Horizon JVM 25 linked'),
    'sdk_abi':1,'artifacts':[]}
for p in sorted(DIST.iterdir()):
    if p.is_file() and p.name not in ['manifest.json','SHA256SUMS']:
        dk=pathlib.Path(os.environ.get('DEVKITPRO',str(ROOT/'.toolchain/opt/devkitpro')))
        compiler=subprocess.check_output([str(dk/'devkitA64/bin/aarch64-none-elf-gcc'),'--version'],text=True).splitlines()[0]
        java25_artifact=p.name in {'java25-test.nro','openjdk25-horizon-runtime.tar.gz'}
        limitations=(['Cross-built genuine VM; runtime startup UNVERIFIED','Classfile execution NOT TESTED',
                       'Experimental hardware execution NOT TESTED','No Minecraft or title-execution claim']
                     if java25_artifact else
                     ['Horizon Java25 startup and execution remain unverified','Native CPU/JIT permissions NOT TESTED','No Minecraft execution on Horizon'])
        bundled=(['OpenJDK Zero VM and native java.base providers linked into Horizon NRO']
                 if p.name=='java25-test.nro' else
                 ['OpenJDK Zero VM archive and eight native java.base archives; libc/libm excluded']
                 if p.name=='openjdk25-horizon-runtime.tar.gz' else
                 ['libnx/newlib','miniJVM Java8 or libffi component as applicable'])
        verification=('PASS: AArch64 PIE link and NRO container; runtime execution unverified'
                      if p.name=='java25-test.nro' else
                      'PASS: archive/image/provider hashes validated; runtime execution unverified'
                      if p.name=='openjdk25-horizon-runtime.tar.gz' else 'PASS')
        tested='Cross-build/link checks only; no Horizon runtime execution' if java25_artifact else 'Linux cross-build and host runtime tests'
        manifest['artifacts'].append({'name':p.name,'purpose':{'java-test.nro':'Java8 interpreter diagnostic','ffi-test.nro':'Native AAPCS64/libnx W^X and page-mapping tests','zero-services-test.nro':'OpenJDK25 native synchronization/thread/fault probe; NO Java VM','java25-test.nro':'Experimental cross-built OpenJDK25 VM NRO; startup and classfile execution UNVERIFIED','openjdk25-horizon-runtime.tar.gz':'Experimental static OpenJDK25 Horizon integration inputs; startup and classfile execution NOT TESTED','javaswitch-horizon-sdk.tar.gz':'Reusable ABI 1 runtime/JNI/FFI SDK; no Horizon Java25 VM','java-switch-source.tar.gz':'Corresponding source','java-switch-test-package.zip':'Installable diagnostics'}.get(p.name,'Build artifact'),
            'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'source_revision':manifest['source_revision'],'build_date':datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'toolchain':compiler,'bundled_dependencies':bundled,'build_dependencies':json.loads((ROOT/'dependencies.lock.json').read_text()),'build_verification':verification,'tested_environment':tested,
            'hardware_verification':'NOT TESTED','known_limitations':limitations})
(DIST/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(DIST/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in sorted(DIST.iterdir()) if p.is_file() and p.name!='SHA256SUMS'))
print('Java NRO architecture/container and package checksums: PASS')
