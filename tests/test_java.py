import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]

class JavaTests(unittest.TestCase):
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
