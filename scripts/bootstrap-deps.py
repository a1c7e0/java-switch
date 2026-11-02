#!/usr/bin/env python3
"""Acquire Java-only dependencies; no graphics stack or game files."""
import hashlib,json,pathlib,shutil,subprocess,tarfile,urllib.request
ROOT=pathlib.Path(__file__).resolve().parents[1]
DEPS=ROOT/".deps"
LOCK=json.loads((ROOT/"dependencies.lock.json").read_text())
def fetch(name):
    p=DEPS/(name+".archive");entry=LOCK[name]
    if not p.exists() or hashlib.sha256(p.read_bytes()).hexdigest()!=entry["sha256"]:
        urllib.request.urlretrieve(entry["url"],p)
    assert hashlib.sha256(p.read_bytes()).hexdigest()==entry["sha256"]
    return p
def main():
    DEPS.mkdir(exist_ok=True)
    source=DEPS/"minijvm-source"
    if not source.exists():
        with tarfile.open(fetch("minijvm")) as archive:
            for member in archive:
                name=member.name.split('/',1)[-1]
                if name.startswith('minijvm/') or name=='LICENSE':
                    member.name=name;archive.extract(member,source,filter='data')
    patch=ROOT/"patches/minijvm-horizon.patch"
    digest=hashlib.sha256(patch.read_bytes()+(ROOT/"src/minijvm_mman.h").read_bytes()).hexdigest()
    stamp=DEPS/"minijvm-patch.sha256"
    if not stamp.exists() or stamp.read_text()!=digest:
        target=DEPS/"minijvm"
        if target.exists():shutil.rmtree(target)
        shutil.copytree(source/"minijvm/c",target)
        subprocess.run(["patch","-p1","--batch","-i",str(patch)],cwd=target,check=True)
        shutil.copyfile(ROOT/"src/minijvm_mman.h",target/"jvm/sc_mman.h");stamp.write_text(digest)
    if not (DEPS/"host-jdk8/jdk8u504-b01/bin/javac").exists():
        with tarfile.open(fetch("host_jdk8")) as archive:archive.extractall(DEPS/"host-jdk8",filter='data')
    print("Pinned Java dependencies verified")
if __name__=='__main__':main()
