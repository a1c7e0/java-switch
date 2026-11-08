import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class Java8FilesystemTests(unittest.TestCase):
    def test_device_uri_and_exclusive_create_on_actual_minijvm(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            instance = root / "instances" / "java-smoke"
            instance.mkdir(parents=True)
            (root / "runtimes" / "minijvm").mkdir(parents=True)
            (root / "tests").mkdir()
            shutil.copyfile(ROOT / "build/minijvm_rt.jar",
                            root / "runtimes/minijvm/minijvm_rt.jar")
            for name in ("diagnostic-tests.jar", "dependency.jar"):
                shutil.copyfile(ROOT / "build" / name, root / "tests" / name)

            # This name belonged to the old test and may contain user data.
            preserved = instance / "java-smoke.txt"
            preserved.write_bytes(b"keep this file")
            result = subprocess.run(
                [str(ROOT / "build/java-test-host"), str(root)],
                capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            self.assertEqual(result.returncode, 0, output)
            for label in (
                    "sdmc URI remains absolute",
                    "exclusive file creation",
                    "existing file is preserved",
                    "exclusive create distinguishes I/O errors",
                    "Java file roundtrip"):
                self.assertIn("PASS: " + label, output)
            self.assertEqual(preserved.read_bytes(), b"keep this file")
            self.assertFalse(list(instance.glob("java-smoke-owned-*.tmp")))
            self.assertFalse(list(instance.glob("java-smoke-blocker-*.tmp")))


if __name__ == "__main__":
    unittest.main()
