#!/usr/bin/env python3
import datetime,hashlib,json,os,pathlib,shutil,struct,subprocess,tarfile,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
DIST=ROOT/'dist';nro=DIST/'java-test.nro';data=nro.read_bytes()
elf=(ROOT/'build/java-test.elf').read_bytes()
assert data[16:20]==b'NRO0' and elf[:6]==b'\x7fELF\x02\x01'
assert struct.unpack_from('<HH',elf,16)==(3,183)
size=struct.unpack_from('<I',data,24)[0];assert data[size:size+4]==b'ASET'
apps=['java-test','ffi-test']
probe=DIST/'zero-services-test.nro'
if probe.exists():
    from openjdk25 import port_inputs
    port_patches,_,port_files,port_digest=port_inputs()
    tracked=set(subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode().split('\0'))
    for path in port_patches+port_files:
        assert str(path.relative_to(ROOT)) in tracked, 'Stage all port sources before packaging: '+str(path)
    proof=json.loads((ROOT/'build/openjdk25-probe.json').read_text())
    assert proof['port_digest']==port_digest and proof['java_vm_linked'] is False
    for name,digest in proof['inputs'].items():assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest
    assert hashlib.sha256(probe.read_bytes()).hexdigest()==proof['nro_sha256']
    apps.append('zero-services-test')
assert not (DIST/'java25-test.nro').exists(), 'No complete Horizon Java25 VM is implemented'
for app in apps:
    linked_path=ROOT/'build'/(app+'.elf') if app!='zero-services-test' else ROOT/'build/openjdk25-horizon/components/zero-services-test.elf'
    binary=(DIST/(app+'.nro')).read_bytes();linked=linked_path.read_bytes()
    if app=='zero-services-test':assert hashlib.sha256(linked).hexdigest()==proof['elf_sha256']
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
for path in ['logs','tests','runtimes/minijvm','instances/java-smoke','licenses']:(runtime/path).mkdir(parents=True)
shutil.copyfile(ROOT/'build/minijvm_rt.jar',runtime/'runtimes/minijvm/minijvm_rt.jar')
for name in ['diagnostic-tests.jar','dependency.jar']:shutil.copyfile(ROOT/'build'/name,runtime/'tests'/name)
shutil.copyfile(ROOT/'build/java25-tests.jar',runtime/'tests/java25-tests.jar')
shutil.copyfile(ROOT/'build/java25-hello.jar',runtime/'tests/java25-hello.jar')
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
    if probe.exists():
        upstream=ROOT/'.deps/openjdk25.archive'
        locked=json.loads((ROOT/'dependencies.lock.json').read_text())['openjdk25_source']
        assert hashlib.sha256(upstream.read_bytes()).hexdigest()==locked['sha256']
        archive.add(upstream,arcname='third-party/openjdk25-upstream.tar.gz')
shutil.copyfile(DIST/'java-switch-source.tar.gz',sd/'java-switch-source.tar.gz')
with zipfile.ZipFile(DIST/'java-switch-test-package.zip','w',zipfile.ZIP_DEFLATED) as z:
    for path in sorted(sd.rglob('*')):
        if path.is_file():z.write(path,path.relative_to(sd))
manifest={'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
    'source_tree_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT)),
    'horizon_execution':'NOT TESTED','java8_interpreter':'built and host-tested','java25_runtime':'NOT IMPLEMENTED',
    'openjdk25_services_probe':'CROSS-BUILT; time/semaphore/atomic services only; no VM' if probe.exists() else 'not built',
    'standard_jni_invocation':'Host Java 25 JNI and FFM tested; no Horizon JVM 25 linked','sdk_abi':1,'artifacts':[]}
for p in sorted(DIST.iterdir()):
    if p.is_file() and p.name not in ['manifest.json','SHA256SUMS']:
        dk=pathlib.Path(os.environ.get('DEVKITPRO',str(ROOT/'.toolchain/opt/devkitpro')))
        compiler=subprocess.check_output([str(dk/'devkitA64/bin/aarch64-none-elf-gcc'),'--version'],text=True).splitlines()[0]
        manifest['artifacts'].append({'name':p.name,'purpose':{'java-test.nro':'Java8 interpreter diagnostic','ffi-test.nro':'Native AAPCS64/libnx W^X and page-mapping tests','zero-services-test.nro':'OpenJDK25 native time/semaphore/atomic probe; NO Java VM','javaswitch-horizon-sdk.tar.gz':'Reusable runtime/JNI/FFI SDK; no Horizon Java25 VM','java-switch-source.tar.gz':'Corresponding source','java-switch-test-package.zip':'Installable diagnostics'}.get(p.name,'Build artifact'),
            'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'source_revision':manifest['source_revision'],'build_date':datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'toolchain':compiler,'bundled_dependencies':['libnx/newlib','miniJVM Java8 or libffi component as applicable'],'build_dependencies':json.loads((ROOT/'dependencies.lock.json').read_text()),'build_verification':'PASS','tested_environment':'Linux cross-build and host runtime tests',
            'hardware_verification':'NOT TESTED','known_limitations':['No Horizon Java25 VM linked','Native CPU/JIT permissions NOT TESTED','No Minecraft execution on Horizon']})
(DIST/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(DIST/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in sorted(DIST.iterdir()) if p.is_file() and p.name!='SHA256SUMS'))
print('Java NRO architecture/container and package checksums: PASS')
