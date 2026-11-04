import pathlib
import subprocess
import tempfile
import unittest

from importlib.machinery import SourceFileLoader


ROOT = pathlib.Path(__file__).resolve().parents[1]
isolation = SourceFileLoader(
    "private_crypto_isolation", str(ROOT / "scripts/isolate-private-crypto.py")
).load_module()


class PrivateCryptoIsolationTest(unittest.TestCase):
    def test_private_definitions_and_references_are_namespaced(self):
        with tempfile.TemporaryDirectory(prefix="javaswitch-crypto-test-") as tmp:
            root = pathlib.Path(tmp)
            private = root / "private.c"
            runtime = root / "runtime.c"
            external = root / "external.c"
            main = root / "main.c"
            private.write_text("int mbedtls_sha256_init(void) { return 7; }\n")
            runtime.write_text(
                "extern int mbedtls_sha256_init(void);\n"
                "int js_runtime_query(void) { return mbedtls_sha256_init(); }\n"
            )
            external.write_text("int mbedtls_sha256_init(void) { return 9; }\n")
            main.write_text(
                "extern int js_runtime_query(void);\n"
                "extern int mbedtls_sha256_init(void);\n"
                "int main(void) { return js_runtime_query() == 7 && "
                "mbedtls_sha256_init() == 9 ? 0 : 1; }\n"
            )
            for name in ("private", "runtime", "external", "main"):
                subprocess.run(
                    ["cc", "-c", str(root / (name + ".c")), "-o", str(root / (name + ".o"))],
                    check=True,
                )
            archive = root / "libjavaswitch.a"
            subprocess.run(
                ["ar", "rcs", str(archive), str(root / "private.o"), str(root / "runtime.o")],
                check=True,
            )

            symbols = isolation.isolate(archive, "nm", "objcopy")

            self.assertEqual(symbols, ["mbedtls_sha256_init"])
            names = subprocess.check_output(["nm", "-g", str(archive)], text=True)
            self.assertIn("js_private_mbedtls_sha256_init", names)
            self.assertNotIn(" U mbedtls_sha256_init", names)
            self.assertIn(" T js_runtime_query", names)
            isolated_bytes = archive.read_bytes()
            self.assertEqual(isolation.isolate(archive, "nm", "objcopy"), [])
            self.assertEqual(archive.read_bytes(), isolated_bytes)
            executable = root / "composition-test"
            subprocess.run(
                ["cc", str(main), str(archive), str(root / "external.o"), "-o", str(executable)],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
