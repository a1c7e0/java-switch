#!/usr/bin/env python3
"""Exercise maintained class-library code with host syscalls and service mocks."""
import pathlib
import shlex
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
UP = ROOT / '.deps/openjdk25-source/src'
PORT = ROOT / 'ports/openjdk25/src/java.base/horizon/native'
TESTS = ROOT / 'ports/openjdk25/tests'
JDK = next((ROOT / '.deps/host-jdk25').glob('*/bin/javac')).parent.parent
OUT = ROOT / 'build/openjdk25-classlib-host'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    with (ROOT / 'build/openjdk25-classlib-host.log').open('w') as log:
        def run(command):
            log.write('$ ' + shlex.join(map(str, command)) + '\n')
            log.flush()
            subprocess.run(list(map(str, command)), cwd=ROOT, stdout=log,
                           stderr=subprocess.STDOUT, check=True, timeout=120)

        for name in ['java_io/test-host.sh', 'java_props/test-host.sh',
                     'java_paths/test-host.sh', 'java_paths/test-classpath-host.sh',
                     'ffm_platform/test-host.sh',
                     'nio_native/test-host.sh', 'nio_native/test-inherited-channel-host.sh',
                     'nio_native/test-wakeup.sh']:
            run(['bash', TESTS / name])
        for name in ['scripts/test-openjdk25-jimage-host.py',
                     'scripts/test-openjdk25-jimage-zlib-host.py']:
            run(['python3', ROOT / name])

        common = ['gcc', '-std=gnu11', '-O2', '-Wall', '-Wextra',
                  '-Wno-unused-parameter', '-ffunction-sections', '-fdata-sections']
        includes = [PORT / 'libnet', PORT / 'libjava',
                    UP / 'java.base/share/native/libnet', UP / 'java.base/share/native/libjava',
                    UP / 'java.base/unix/native/libjava',
                    ROOT / 'build/openjdk25-horizon/support/headers/java.base',
                    UP / 'hotspot/share/include',
                    ROOT / 'ports/openjdk25/src/hotspot/os/horizon/include',
                    JDK / 'include', JDK / 'include/linux']
        flags = ['-I' + str(path) for path in includes]
        binary = OUT / 'net-init'
        run([*common, '-I' + str(TESTS / 'net_util/mocks'), *flags,
             PORT / 'libnet/net_util_md.c', TESTS / 'net_util/test_net_util_init.c',
             '-pthread', '-Wl,--gc-sections', '-o', binary])
        for mode in [[], ['already'], ['failure']]:
            run([binary, *mode])
        binary = OUT / 'net-unsupported'
        run([*common, *flags, PORT / 'libnet/DefaultProxySelector.c',
             PORT / 'libnet/NetworkInterface.c', TESTS / 'net_util/test_unsupported_queries.c',
             '-Wl,--gc-sections', '-o', binary])
        run([binary])
        binary = OUT / 'resolver-config'
        run([*common, *flags, PORT / 'libnet/ResolverConfigurationImpl.c',
             TESTS / 'net_util/test_resolver_fallback.c', '-Wl,--wrap=gethostname',
             '-Wl,--gc-sections', '-o', binary])
        run([binary])
        binary = OUT / 'seed-generator'
        run([*common, '-Werror', '-I' + str(TESTS / 'seed_generator/mocks'), *flags,
             PORT / 'libjava/SeedGenerator.c', TESTS / 'seed_generator/test_seed_generator.c',
             '-o', binary])
        run([binary])
        log.write('HOST-PASSED: host syscalls and mocked JNI/Horizon services; Switch NOT TESTED\n')
    print('OpenJDK25 class-library tests: HOST-PASSED; Switch NOT TESTED')


if __name__ == '__main__':
    main()
