#!/usr/bin/env python3
"""Exercise Horizon's direct jimage-to-libzip binding with real host zlib."""
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
JAVA_BASE = ROOT / '.deps/openjdk25-source/src/java.base'
HOTSPOT = ROOT / '.deps/openjdk25-source/src/hotspot'
GENERATED = ROOT / 'build/openjdk25-horizon'
PATCH = ROOT / 'patches/openjdk25/0017-horizon-jimage-static-zlib.patch'


def include_args(paths):
    return [flag for path in paths for flag in ('-I', str(path))]


def main():
    libjimage_source = JAVA_BASE / 'share/native/libjimage'
    libzip_source = JAVA_BASE / 'share/native/libzip'
    required = [libjimage_source / 'imageDecompressor.cpp',
                libzip_source / 'zip_util.c', libzip_source / 'zip_util.h',
                JAVA_BASE / 'share/native/include/jni.h']
    if any(not item.is_file() for item in required):
        raise SystemExit('run scripts/openjdk25.py prepare first (pinned source required)')

    with tempfile.TemporaryDirectory(prefix='jimage-zlib-host-') as temp:
        temp_root = pathlib.Path(temp)
        patched = temp_root / 'openjdk'
        jimage = patched / 'src/java.base/share/native/libjimage'
        zip_headers = patched / 'src/java.base/share/native/libzip'
        core_libraries = patched / 'make/modules/java.base/lib'
        jimage.mkdir(parents=True)
        zip_headers.mkdir(parents=True)
        core_libraries.mkdir(parents=True)
        shutil.copyfile(libjimage_source / 'imageDecompressor.cpp',
                        jimage / 'imageDecompressor.cpp')
        shutil.copyfile(libzip_source / 'zip_util.h', zip_headers / 'zip_util.h')
        shutil.copyfile(ROOT / '.deps/openjdk25-source/make/modules/java.base/lib/CoreLibraries.gmk',
                        core_libraries / 'CoreLibraries.gmk')
        subprocess.run(['patch', '-p1', '--batch', '--fuzz=0', '-i', str(PATCH)],
                       cwd=patched, check=True, stdout=subprocess.DEVNULL)

        java_includes = [
            JAVA_BASE / 'share/native/include',
            JAVA_BASE / 'unix/native/include',
            libjimage_source,
            libzip_source,
            libzip_source / 'zlib',
            JAVA_BASE / 'share/native/libjava',
            JAVA_BASE / 'unix/native/libjava',
            JAVA_BASE / 'share/native/libjli',
            GENERATED / 'support/modules_include/java.base',
            GENERATED / 'jdk/include',
            HOTSPOT / 'share/include',
            HOTSPOT / 'os/posix/include',
        ]
        obj = temp_root / 'imageDecompressor.o'
        endian_obj = temp_root / 'endian.o'
        zip_obj = temp_root / 'zip_util.o'
        harness_obj = temp_root / 'jimage-zlib-host.o'
        binary = temp_root / 'jimage-zlib-host'
        common = ['-ffunction-sections', '-fdata-sections']
        subprocess.run(['c++', '-std=c++14', '-D__SWITCH__', *common,
                        *include_args([jimage, *java_includes]),
                        '-c', jimage / 'imageDecompressor.cpp', '-o', obj], check=True)
        subprocess.run(['c++', '-std=c++14', *common,
                        *include_args(java_includes), '-c',
                        libjimage_source / 'endian.cpp', '-o', endian_obj], check=True)
        subprocess.run(['cc', '-std=gnu11', '-D_GNU_SOURCE', *common,
                        *include_args(java_includes), '-c',
                        libzip_source / 'zip_util.c', '-o', zip_obj], check=True)
        subprocess.run(['c++', '-std=c++14', '-D__SWITCH__', *common,
                        *include_args([jimage, *java_includes]), '-c',
                        ROOT / 'tests/openjdk25_jimage_zlib_host.cpp', '-o', harness_obj],
                       check=True)
        subprocess.run(['c++', '-Wl,--gc-sections', obj, endian_obj, zip_obj,
                        harness_obj, '-lz', '-o', binary], check=True)
        subprocess.run([binary], check=True, timeout=20)
    print('Horizon jimage direct libzip binding passed with host zlib; not Horizon execution')


if __name__ == '__main__':
    main()
