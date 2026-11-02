#!/usr/bin/env python3
import pathlib,subprocess,zipfile,struct
root=pathlib.Path(__file__).resolve().parents[1];jdk=next((root/'.deps/host-jdk25').glob('*/bin/javac'));out=root/'build/java25-classes';out.mkdir(parents=True,exist_ok=True)
subprocess.run([str(jdk),'--release','25','-d',str(out),str(root/'tests/java25/Java25Smoke.java')],check=True)
with zipfile.ZipFile(root/'build/java25-tests.jar','w',zipfile.ZIP_DEFLATED) as jar:
    for p in sorted(out.rglob('*.class')):jar.write(p,p.relative_to(out))
hello=root/'build/java25-hello-classes';hello.mkdir(parents=True,exist_ok=True)
subprocess.run([str(jdk),'--release','25','-d',str(hello),str(root/'tests/java25/HelloHorizon25.java')],check=True)
data=(hello/'HelloHorizon25.class').read_bytes()
assert data[:4]==b'\xca\xfe\xba\xbe' and struct.unpack_from('>H',data,6)[0]==69
with zipfile.ZipFile(root/'build/java25-hello.jar','w',zipfile.ZIP_DEFLATED) as jar:
    jar.writestr('META-INF/MANIFEST.MF','Manifest-Version: 1.0\r\nMain-Class: HelloHorizon25\r\n\r\n')
    jar.write(hello/'HelloHorizon25.class','HelloHorizon25.class')
