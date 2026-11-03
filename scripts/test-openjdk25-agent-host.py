#!/usr/bin/env python3
"""Build and run a real java.instrument startup-agent smoke on pinned JDK 25."""
import json
import pathlib
import re
import shlex
import shutil
import subprocess
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
JDK_ROOT = ROOT / ".deps/host-jdk25"
OUT = ROOT / "build/openjdk25-agent-host"
LOG = ROOT / "build/openjdk25-agent-host.log"
AGENT_CLASSES = OUT / "agent-classes"
APP_CLASSES = OUT / "app-classes"
AGENT_JAR = ROOT / "build/java25-agent-smoke-agent.jar"
APP_JAR = ROOT / "build/java25-agent-smoke-app.jar"


def record_run(command, log, expect_success=True):
    command = [str(part) for part in command]
    log.write("$ " + shlex.join(command) + "\n")
    log.flush()
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            timeout=60)
    if result.stdout:
        log.write(result.stdout)
    if result.stderr:
        log.write(result.stderr)
    log.write(f"[exit {result.returncode}]\n")
    log.flush()
    if expect_success and result.returncode != 0:
        raise RuntimeError(f"command failed ({result.returncode}); see {LOG}")
    if not expect_success and result.returncode == 0:
        raise RuntimeError(f"command unexpectedly passed; see {LOG}")
    return result


def make_jar(path, classes, manifest):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as jar:
        jar.writestr("META-INF/MANIFEST.MF", manifest)
        for class_file in sorted(classes.rglob("*.class")):
            jar.write(class_file, class_file.relative_to(classes).as_posix())


def pinned_java_version():
    lock = json.loads((ROOT / "dependencies.lock.json").read_text())
    # The JDK archive URL is the repository's version pin; its archive hash is
    # checked by bootstrap-deps.py before extraction.
    match = re.search(r"jdk-(\d+\.\d+\.\d+)%2B", lock["host_jdk25"]["url"])
    if not match:
        raise RuntimeError("could not derive pinned JDK version from dependencies.lock.json")
    return match.group(1)


def main():
    javac = next(JDK_ROOT.glob("*/bin/javac"), None)
    if javac is None:
        raise SystemExit(f"pinned genuine JDK 25 is missing under {JDK_ROOT}")
    jdk = javac.parent.parent
    java = jdk / "bin/java"
    release_file = jdk / "release"
    if not java.is_file() or not release_file.is_file():
        raise SystemExit(f"incomplete JDK installation: {jdk}")
    release = dict(line.split("=", 1) for line in release_file.read_text().splitlines()
                   if "=" in line)
    version = pinned_java_version()
    actual_version = release.get("JAVA_VERSION", "").strip('"')
    if actual_version != version or not version.startswith("25."):
        raise SystemExit(f"expected pinned Java 25 version {version}, found {actual_version}")

    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    AGENT_CLASSES.mkdir()
    APP_CLASSES.mkdir()
    LOG.parent.mkdir(parents=True, exist_ok=True)
    with LOG.open("w") as log:
        log.write(f"Pinned JDK: {jdk}\nJAVA_VERSION={actual_version}\n")
        javac_version = record_run([javac, "-version"], log)
        if f"javac {version}" not in (javac_version.stdout + javac_version.stderr):
            raise RuntimeError(f"javac version does not match pinned {version}; see {LOG}")
        java_version = record_run([java, "-version"], log)
        if f'"{version}"' not in (java_version.stdout + java_version.stderr):
            raise RuntimeError(f"java version does not match pinned {version}; see {LOG}")

        record_run([javac, "--release", "25", "-d", AGENT_CLASSES,
                    ROOT / "tests/java25/AgentSmokeAgent.java"], log)
        record_run([javac, "--release", "25", "-d", APP_CLASSES,
                    ROOT / "tests/java25/AgentSmoke.java"], log)

        make_jar(AGENT_JAR, AGENT_CLASSES,
                 "Manifest-Version: 1.0\r\nPremain-Class: AgentSmokeAgent\r\n\r\n")
        make_jar(APP_JAR, APP_CLASSES,
                 "Manifest-Version: 1.0\r\nMain-Class: AgentSmoke\r\n\r\n")

        app_command = ["-Dagent.smoke.property=property-forwarded", "-cp", APP_JAR,
                       "AgentSmoke", "main-argument-forwarded"]
        success = record_run([java, f"-javaagent:{AGENT_JAR}=premain-argument-forwarded",
                              *app_command], log)
        output = success.stdout + success.stderr
        if "JAVA25_AGENT_SMOKE_PASS" not in output:
            raise RuntimeError(f"agent launch omitted its PASS marker; see {LOG}")

        # The sentinel assertion runs first in main so this control proves the
        # application cannot pass when the startup transformer is absent.
        no_agent = record_run([java, *app_command], log, expect_success=False)
        no_agent_output = no_agent.stdout + no_agent.stderr
        expected_failure = "ClassFile API transformation did not affect the initial definition"
        if expected_failure not in no_agent_output:
            raise RuntimeError(f"no-agent launch failed for an unexpected reason; see {LOG}")

    print(f"agent jar: {AGENT_JAR.relative_to(ROOT)}")
    print(f"app jar: {APP_JAR.relative_to(ROOT)}")
    print(f"host log: {LOG.relative_to(ROOT)}")
    print(f"OpenJDK {version} java.instrument host smoke: PASS; no-agent control: PASS; Horizon NOT TESTED")


if __name__ == "__main__":
    main()
