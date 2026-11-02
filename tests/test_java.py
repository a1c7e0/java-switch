import pathlib
import struct
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT=pathlib.Path(__file__).resolve().parents[1]

class JavaTests(unittest.TestCase):
    def test_loader_rejects_newer_or_invalid_classfile_headers(self):
        cases = [('major69', 69, 0, 'classfile version 69.0'),
                 ('major53', 53, 0, 'classfile version 53.0'),
                 ('major44', 44, 0, 'classfile version 44.0'),
                 ('minor52_1', 52, 1, 'classfile version 52.1'),
                 ('magic', 52, 0, 'invalid classfile magic'),
                 ('truncated', 52, 0, 'truncated classfile header')]
        for label, major, minor, message in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                root=pathlib.Path(directory)
                for path in ['runtimes/minijvm', 'tests', 'instances/java-smoke']:
                    (root/path).mkdir(parents=True,exist_ok=True)
                shutil.copyfile(ROOT/'build/minijvm_rt.jar',root/'runtimes/minijvm/minijvm_rt.jar')
                shutil.copyfile(ROOT/'build/dependency.jar',root/'tests/dependency.jar')
                with zipfile.ZipFile(ROOT/'build/diagnostic-tests.jar') as source, \
                     zipfile.ZipFile(root/'tests/diagnostic-tests.jar','w') as jar:
                    for name in source.namelist():
                        data=bytearray(source.read(name))
                        if name=='DiagnosticSmoke.class':
                            struct.pack_into('>HH',data,4,minor,major)
                            if label=='magic':data[0]=0
                            if label=='truncated':data=data[:7]
                        jar.writestr(name,data)
                p=subprocess.run([str(ROOT/'build/java-test-host'),str(root)],capture_output=True,text=True,timeout=30)
                self.assertNotEqual(p.returncode,0,p.stdout+p.stderr)
                self.assertIn(message,p.stdout+p.stderr)
                self.assertNotIn('DIAGNOSTIC SUITE PASS',p.stdout+p.stderr)
                self.assertIn('JVM teardown completed',p.stdout+p.stderr)

    def test_interpreter_jar_io_thread_reflection_and_native_bridge(self):
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)
            (root/"runtimes/minijvm").mkdir(parents=True)
            (root/"tests").mkdir()
            (root/"instances/java-smoke").mkdir(parents=True)
            shutil.copyfile(ROOT/"build/minijvm_rt.jar",root/"runtimes/minijvm/minijvm_rt.jar")
            for name in ["diagnostic-tests.jar","dependency.jar"]:shutil.copyfile(ROOT/"build"/name,root/"tests"/name)
            p=subprocess.run([str(ROOT/"build/java-test-host"),str(root)],capture_output=True,text=True,timeout=30)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            self.assertIn("DIAGNOSTIC SUITE PASS",p.stdout+p.stderr)
            self.assertIn("PASS: miniJVM static native bridge",p.stdout+p.stderr)
            self.assertIn("gc backend : classic malloc",p.stdout+p.stderr)
            self.assertIn("JVM teardown completed",p.stdout+p.stderr)
            self.assertFalse((root/"instances/java-smoke/java-smoke.txt").exists())
