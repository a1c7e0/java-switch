#!/usr/bin/env python3
import os
import pathlib
import hashlib
import subprocess
import shutil
import zipfile

ROOT=pathlib.Path(__file__).resolve().parents[1]
os.chdir(ROOT)
JDK=ROOT/".deps/host-jdk8/jdk8u504-b01/bin"
SOURCE=ROOT/".deps/minijvm-source/minijvm/java/src/main"
JAVA_FS_PATCH=ROOT/"patches/minijvm-java8-fs.patch"
JAVA_FS_PATCH_SHA256=ROOT/"patches/minijvm-java8-fs.patch.sha256"

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

def prepare_runtime_sources():
    # Keep the pinned upstream tree pristine. Java 8 Horizon filesystem fixes
    # are maintained as a zero-fuzz patch over a private generated copy.
    dest=ROOT/"build"/"minijvm-java-source"
    if dest.exists():shutil.rmtree(dest)
    shutil.copytree(SOURCE/"java",dest)
    expected=JAVA_FS_PATCH_SHA256.read_text().strip()
    actual=hashlib.sha256(JAVA_FS_PATCH.read_bytes()).hexdigest()
    if actual!=expected:
        raise RuntimeError("Java 8 filesystem patch SHA-256 mismatch")
    subprocess.run(["patch","-p1","--batch","--fuzz=0","-i",str(JAVA_FS_PATCH)],cwd=dest,check=True)
    return dest

build("minijvm_rt",sorted(prepare_runtime_sources().rglob("*.java")),SOURCE/"resource")
build("diagnostic-tests",[ROOT/"tests/java/DiagnosticSmoke.java"])
build("dependency",[ROOT/"tests/java/JarDependency.java"])
