#!/usr/bin/env python3
import pathlib,subprocess,zipfile
root=pathlib.Path(__file__).resolve().parents[1];jdk=next((root/'.deps/host-jdk25').glob('*/bin/javac'));out=root/'build/java25-classes';out.mkdir(parents=True,exist_ok=True)
subprocess.run([str(jdk),'--release','25','-d',str(out),str(root/'tests/java25/Java25Smoke.java')],check=True)
with zipfile.ZipFile(root/'build/java25-tests.jar','w',zipfile.ZIP_DEFLATED) as jar:
    for p in sorted(out.rglob('*.class')):jar.write(p,p.relative_to(out))
