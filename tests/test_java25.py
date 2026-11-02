import pathlib,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class Java25InvocationTests(unittest.TestCase):
    def test_unavailable_vm_and_wrong_java_are_blocked_before_execution(self):
        p=subprocess.run([str(ROOT/'build/runtime-gate-host')],capture_output=True,text=True,timeout=5)
        self.assertEqual(p.returncode,0,p.stdout+p.stderr);self.assertIn('RUNTIME GATES PASS',p.stdout)
    def test_jni_creation_native_registration_and_ffm(self):
        with tempfile.TemporaryDirectory() as d:
            p=subprocess.run([str(ROOT/'build/jni25-host'),str(ROOT/'build/java25-tests.jar'),d],capture_output=True,text=True,timeout=30)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            self.assertIn('JAVA25 JNI FFM SUITE PASS',p.stdout+p.stderr)
            self.assertIn('JNI result=0 stage=completed',p.stdout)
            self.assertEqual(list(pathlib.Path(d).iterdir()),[])
