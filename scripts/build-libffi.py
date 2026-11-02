#!/usr/bin/env python3
import hashlib,json,os,pathlib,shutil,subprocess,tarfile,urllib.request
ROOT=pathlib.Path(__file__).resolve().parents[1];os.chdir(ROOT);deps=ROOT/'.deps';lock=json.loads((ROOT/'dependencies.lock.json').read_text())['libffi']
archive=deps/'libffi.archive'
if not archive.exists():urllib.request.urlretrieve(lock['url'],archive)
assert hashlib.sha256(archive.read_bytes()).hexdigest()==lock['sha256']
source=deps/'libffi-source'
if not source.exists():
    with tarfile.open(archive) as t:
        for m in t:
            m.name=m.name.split('/',1)[-1]
            if m.name:t.extract(m,source,filter='data')
build=ROOT/'build/libffi';build.mkdir(parents=True,exist_ok=True)
dk=pathlib.Path(os.environ.get('DEVKITPRO',str(ROOT/'.toolchain/opt/devkitpro')));prefix=str(dk/'devkitA64/bin/aarch64-none-elf-')
env=dict(os.environ,CC=prefix+'gcc',AR=prefix+'ar',RANLIB=prefix+'ranlib',CFLAGS='-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -O2 -ffunction-sections -fdata-sections')
if not (build/'Makefile').exists():subprocess.run([str(source/'configure'),'--host=aarch64-none-elf','--disable-shared','--enable-static','--disable-docs'],cwd=build,env=env,check=True)
subprocess.run(['make','-j4'],cwd=build,env=env,check=True)
# Replace malloc-backed executable closures with real libnx code-memory APIs.
objects=[build/'src'/name for name in ['prep_cif.o','types.o','raw_api.o','java_raw_api.o','tramp.o','aarch64/ffi.o','aarch64/sysv.o']]
patched=build/'ffi_horizon_arch.o';shutil.copyfile(build/'src/aarch64/ffi.o',patched)
subprocess.run([prefix+'objcopy','--redefine-sym','ffi_prep_closure_loc=js_ffi_prep_closure_impl',str(patched)],check=True)
objects[5]=patched
subprocess.run([prefix+'ar','rcs',str(ROOT/'build/libffi-horizon.a'),*map(str,objects)],check=True)
print('AAPCS64 libffi archive built; closure allocator supplied by ffi_horizon.c')
