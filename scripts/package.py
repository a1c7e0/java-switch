#!/usr/bin/env python3
import datetime,hashlib,json,os,pathlib,shutil,struct,subprocess,tarfile,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
DIST=ROOT/'dist';nro=DIST/'java-test.nro';data=nro.read_bytes()
elf=(ROOT/'build/java-test.elf').read_bytes()
assert data[16:20]==b'NRO0' and elf[:6]==b'\x7fELF\x02\x01'
assert struct.unpack_from('<HH',elf,16)==(3,183)
size=struct.unpack_from('<I',data,24)[0];assert data[size:size+4]==b'ASET'
for app in ['java-test','ffi-test']:
    binary=(DIST/(app+'.nro')).read_bytes();linked=(ROOT/'build'/(app+'.elf')).read_bytes()
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
for path in ['logs','tests','runtimes/minijvm','instances/java-smoke','licenses']:(runtime/path).mkdir(parents=True)
shutil.copyfile(ROOT/'build/minijvm_rt.jar',runtime/'runtimes/minijvm/minijvm_rt.jar')
for name in ['diagnostic-tests.jar','dependency.jar']:shutil.copyfile(ROOT/'build'/name,runtime/'tests'/name)
shutil.copyfile(ROOT/'build/java25-tests.jar',runtime/'tests/java25-tests.jar')
with tarfile.open(DIST/'javaswitch-horizon-sdk.tar.gz','w:gz',dereference=True) as archive:
    archive.add(ROOT/'include',arcname='include')
    archive.add(ROOT/'build/sdk/libjavaswitch.a',arcname='lib/libjavaswitch.a')
    archive.add(ROOT/'build/sdk/runtime-contract.json',arcname='runtime-contract.json')
    archive.add(ROOT/'build/libffi/include/ffi.h',arcname='include/ffi.h')
    archive.add(ROOT/'build/libffi/include/ffitarget.h',arcname='include/ffitarget.h')
    archive.add(ROOT/'LICENSE',arcname='LICENSE')
shutil.copytree(ROOT/'licenses',runtime/'licenses',dirs_exist_ok=True)
shutil.copyfile(ROOT/'README.md',sd/'JAVA-SWITCH-README.md')
with tarfile.open(DIST/'java-switch-source.tar.gz','w:gz') as archive:
    for name in subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).split(b'\0'):
        if name:archive.add(ROOT/name.decode(),arcname='java-switch/'+name.decode())
    archive.add(ROOT/'.deps/minijvm-source',arcname='third-party/minijvm-source')
    archive.add(ROOT/'.deps/minijvm',arcname='third-party/minijvm-horizon-c')
    archive.add(ROOT/'.deps/libffi-source',arcname='third-party/libffi-source')
shutil.copyfile(DIST/'java-switch-source.tar.gz',sd/'java-switch-source.tar.gz')
with zipfile.ZipFile(DIST/'java-switch-test-package.zip','w',zipfile.ZIP_DEFLATED) as z:
    for path in sorted(sd.rglob('*')):
        if path.is_file():z.write(path,path.relative_to(sd))
manifest={'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
    'horizon_execution':'NOT TESTED','java8_interpreter':'built and host-tested','java25_runtime':'NOT IMPLEMENTED',
    'standard_jni_invocation':'Host Java 25 JNI and FFM tested; no Horizon JVM 25 linked','sdk_abi':1,'artifacts':[]}
for p in sorted(DIST.iterdir()):
    if p.is_file() and p.name not in ['manifest.json','SHA256SUMS']:
        dk=pathlib.Path(os.environ.get('DEVKITPRO',str(ROOT/'.toolchain/opt/devkitpro')))
        compiler=subprocess.check_output([str(dk/'devkitA64/bin/aarch64-none-elf-gcc'),'--version'],text=True).splitlines()[0]
        manifest['artifacts'].append({'name':p.name,'purpose':{'java-test.nro':'Java8 interpreter diagnostic','ffi-test.nro':'Native AAPCS64/libnx W^X test','javaswitch-horizon-sdk.tar.gz':'Reusable runtime/JNI/FFI SDK; no Horizon Java25 VM','java-switch-source.tar.gz':'Corresponding source','java-switch-test-package.zip':'Installable diagnostics'}.get(p.name,'Build artifact'),
            'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'source_revision':manifest['source_revision'],'build_date':datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'toolchain':compiler,'bundled_dependencies':['libnx/newlib','miniJVM Java8 or libffi component as applicable'],'build_dependencies':json.loads((ROOT/'dependencies.lock.json').read_text()),'build_verification':'PASS','tested_environment':'Linux cross-build and host runtime tests',
            'hardware_verification':'NOT TESTED','known_limitations':['No Horizon Java25 VM linked','Native CPU/JIT permissions NOT TESTED','No Minecraft execution on Horizon']})
(DIST/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(DIST/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in sorted(DIST.iterdir()) if p.is_file() and p.name!='SHA256SUMS'))
print('Java NRO architecture/container and package checksums: PASS')
