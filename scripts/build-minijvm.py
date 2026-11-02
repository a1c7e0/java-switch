#!/usr/bin/env python3
"""Build the pinned interpreter and the same diagnostic adapter for host/Switch."""
import concurrent.futures
import hashlib
import os
import pathlib
import subprocess
import sys

ROOT=pathlib.Path(__file__).resolve().parents[1]
os.chdir(ROOT)
cross="--host" not in sys.argv
dk=pathlib.Path(os.environ.get("DEVKITPRO",str(ROOT/".toolchain/opt/devkitpro")))
base=ROOT/".deps/minijvm"
out=ROOT/"build"/("minijvm" if cross else "minijvm-host-objects")
out.mkdir(parents=True,exist_ok=True)
cc=str(dk/"devkitA64/bin/aarch64-none-elf-gcc") if cross else "gcc"
flags=["-O2","-g","-ffunction-sections","-fdata-sections","-D_GNU_SOURCE","-DSC_INTERPRETER_ONLY",
       "-include",str(ROOT/"src/minijvm_compat.h")]
if cross:
    flags += ["-march=armv8-a+crc+crypto","-mtune=cortex-a57","-mtp=soft","-fPIE","-D__SWITCH__","-D__unix__","-I"+str(dk/"libnx/include")]
else:flags += ["-D__JVM_OS_SWITCH__=1"]
for directory in ["jvm","utils","utils/sljit","utils/https","utils/https/mbedtls/include"]:flags += ["-I"+str(base/directory)]
sources=[]
for directory in ["jvm","utils","utils/https","utils/https/mbedtls/library"]:sources+=sorted((base/directory).glob("*.c"))
headers=list(base.rglob("*.h"))+[ROOT/"src/minijvm_compat.h"]
newest_header=max(p.stat().st_mtime for p in headers)
signature=hashlib.sha256(repr([cc,*flags]).encode()).hexdigest()
stamp=out/"flags.sha256"
rebuild=not stamp.exists() or stamp.read_text()!=signature

def compile_source(source):
    obj=out/(source.stem+".o")
    if rebuild or not obj.exists() or obj.stat().st_mtime<max(newest_header,source.stat().st_mtime):
        result=subprocess.run([cc,*flags,"-c",str(source),"-o",str(obj)],capture_output=True,text=True)
        if result.returncode:raise RuntimeError(str(source)+"\n"+result.stderr)
    return obj

with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile_source,sources))
stamp.write_text(signature)
if cross:
    subprocess.run([str(dk/"devkitA64/bin/aarch64-none-elf-ar"),"rcs","build/libminijvm.a",*map(str,objects)],check=True)
else:
    subprocess.run([cc,*flags,"src/java_vm.c","src/java_host.c",*map(str,objects),"-pthread","-lm","-Wl,--gc-sections","-o","build/java-test-host"],check=True)
print("miniJVM", "AArch64 static library" if cross else "host diagnostic", "built")
