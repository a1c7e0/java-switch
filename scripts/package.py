#!/usr/bin/env python3
import hashlib,json,pathlib,shutil,struct,subprocess,tarfile,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
DIST=ROOT/'dist';nro=DIST/'java-test.nro';data=nro.read_bytes()
elf=(ROOT/'build/java-test.elf').read_bytes()
assert data[16:20]==b'NRO0' and elf[:6]==b'\x7fELF\x02\x01'
assert struct.unpack_from('<HH',elf,16)==(3,183)
size=struct.unpack_from('<I',data,24)[0];assert data[size:size+4]==b'ASET'
sd=DIST/'COPY_TO_SD';runtime=sd/'switch/mc-runtime'
if sd.exists():shutil.rmtree(sd)
(sd/'switch/java-test').mkdir(parents=True);shutil.copyfile(nro,sd/'switch/java-test/java-test.nro')
for path in ['logs','tests','runtimes/minijvm','instances/java-smoke','licenses']:(runtime/path).mkdir(parents=True)
shutil.copyfile(ROOT/'build/minijvm_rt.jar',runtime/'runtimes/minijvm/minijvm_rt.jar')
for name in ['diagnostic-tests.jar','dependency.jar']:shutil.copyfile(ROOT/'build'/name,runtime/'tests'/name)
shutil.copytree(ROOT/'licenses',runtime/'licenses',dirs_exist_ok=True)
shutil.copyfile(ROOT/'README.md',sd/'JAVA-SWITCH-README.md')
with tarfile.open(DIST/'java-switch-source.tar.gz','w:gz') as archive:
    for name in subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).split(b'\0'):
        if name:archive.add(ROOT/name.decode(),arcname='java-switch/'+name.decode())
    archive.add(ROOT/'.deps/minijvm-source',arcname='third-party/minijvm-source')
    archive.add(ROOT/'.deps/minijvm',arcname='third-party/minijvm-horizon-c')
shutil.copyfile(DIST/'java-switch-source.tar.gz',sd/'java-switch-source.tar.gz')
with zipfile.ZipFile(DIST/'java-switch-test-package.zip','w',zipfile.ZIP_DEFLATED) as z:
    for path in sorted(sd.rglob('*')):
        if path.is_file():z.write(path,path.relative_to(sd))
manifest={'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
    'horizon_execution':'NOT TESTED','java8_interpreter':'built and host-tested','java25_runtime':'NOT IMPLEMENTED',
    'standard_jni_invocation':'NOT IMPLEMENTED','artifacts':[]}
for p in sorted(DIST.iterdir()):
    if p.is_file() and p.name not in ['manifest.json','SHA256SUMS']:
        manifest['artifacts'].append({'name':p.name,'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(DIST/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(DIST/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in sorted(DIST.iterdir()) if p.is_file() and p.name!='SHA256SUMS'))
print('Java NRO architecture/container and package checksums: PASS')
