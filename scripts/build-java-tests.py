#!/usr/bin/env python3
import os
import pathlib
import subprocess
import shutil
import zipfile

ROOT=pathlib.Path(__file__).resolve().parents[1]
os.chdir(ROOT)
JDK=ROOT/".deps/host-jdk8/jdk8u504-b01/bin"
SOURCE=ROOT/".deps/minijvm-source/minijvm/java/src/main"

def build(name,sources,resource=None):
    dest=ROOT/"build"/(name+"-classes")
    if dest.exists():shutil.rmtree(dest)
    dest.mkdir(parents=True,exist_ok=True)
    args=ROOT/"build"/(name+"-sources.txt")
    args.write_text('\n'.join(str(p) for p in sources))
    boot="." if name=="minijvm_rt" else str(ROOT/"build/minijvm_rt.jar")
    subprocess.run([str(JDK/"javac"),"-source","8","-target","8","-bootclasspath",boot,"-encoding","UTF-8","-d",str(dest),"@"+str(args)],check=True)
    # Fixed ZIP timestamps make the generated JAR reproducible.
    with zipfile.ZipFile(ROOT/"build"/(name+".jar"),"w",zipfile.ZIP_DEFLATED) as jar:
        for base in [dest]+([resource] if resource else []):
            for file in sorted(base.rglob('*')):
                if file.is_file():
                    info=zipfile.ZipInfo(file.relative_to(base).as_posix(),(2020,1,1,0,0,0))
                    info.compress_type=zipfile.ZIP_DEFLATED
                    jar.writestr(info,file.read_bytes())

build("minijvm_rt",sorted((SOURCE/"java").rglob("*.java")),SOURCE/"resource")
build("diagnostic-tests",[ROOT/"tests/java/DiagnosticSmoke.java"])
build("dependency",[ROOT/"tests/java/JarDependency.java"])
