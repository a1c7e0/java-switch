#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run actual Horizon Zero AArch64 copy/spin routines under Unicorn.

This verifies instructions and copy contracts only. It is not Switch hardware
execution and does not establish native atomic-memory behavior.
"""
import os
import pathlib
import struct
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / '.deps/openjdk25-emulator'))
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X30

dk = pathlib.Path(os.environ.get('DEVKITPRO', root / '.toolchain/opt/devkitpro'))
cc = str(dk / 'devkitA64/bin/aarch64-none-elf-g++')
nm = str(dk / 'devkitA64/bin/aarch64-none-elf-nm')
objcopy = str(dk / 'devkitA64/bin/aarch64-none-elf-objcopy')
out = root / 'build/openjdk25-zero-copies'
out.mkdir(parents=True, exist_ok=True)
source = root / 'ports/openjdk25/src/hotspot/os_cpu/horizon_zero/os_horizon_zero.cpp'
command_file = root / 'build/openjdk25-horizon/hotspot/variant-zero/libjvm/objs/static/os_horizon_thread.o.cmdline'
if not command_file.exists():
    raise SystemExit('Run the full OpenJDK25 build first to record actual JDK compile flags')

args = __import__('shlex').split(command_file.read_text())
inputs = [i for i, value in enumerate(args)
          if value.endswith('.cpp') and pathlib.Path(value).is_file()]
if len(inputs) != 1:
    raise SystemExit(f'Cannot identify source in recorded compile command: {inputs}')
args[inputs[0]] = str(source)
args[args.index('-o') + 1] = str(out / 'os_horizon_zero.o')
args.insert(args.index('-c'), '-ffunction-sections')
args.insert(args.index('-c'), '-fdata-sections')
subprocess.run(args, check=True)

harness = out / 'keep_sections.S'
harness.write_text(r'''/* SPDX-License-Identifier: GPL-2.0-only */
.text
.global js_zero_test_entry
.type js_zero_test_entry,%function
js_zero_test_entry:
    ret
.section .note.GNU-stack,"",%progbits
''')
keep = ['SpinPause', '_Copy_conjoint_jshorts_atomic',
        '_Copy_conjoint_jints_atomic', '_Copy_conjoint_jlongs_atomic',
        '_Copy_arrayof_conjoint_bytes', '_Copy_arrayof_conjoint_jshorts',
        '_Copy_arrayof_conjoint_jints', '_Copy_arrayof_conjoint_jlongs']
elf = out / 'zero-copy-test.elf'
subprocess.run([str(dk / 'devkitA64/bin/aarch64-none-elf-gcc'),
                '-march=armv8-a+crc+crypto', '-c', str(harness),
                '-o', str(out / 'keep_sections.o')], check=True)
link = [cc, '-nostdlib', '-static', '-Wl,--gc-sections',
        '-Wl,-Ttext=0x100000', '-Wl,-e,js_zero_test_entry']
for symbol in keep:
    link.append('-Wl,-u,' + symbol)
link += [str(out / 'keep_sections.o'), str(out / 'os_horizon_zero.o'),
         '-lc', '-lgcc', '-o', str(elf)]
subprocess.run(link, check=True)

symbols = {}
for line in subprocess.check_output([nm, '-C', str(elf)], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = int(fields[0], 16)
blob = elf.read_bytes()
phoff = struct.unpack_from('<Q', blob, 32)[0]
phentsize, phnum = struct.unpack_from('<HH', blob, 54)
segments = []
for index in range(phnum):
    kind, flags, offset, address, physical, filesz, memsz, align = struct.unpack_from(
        '<IIQQQQQQ', blob, phoff + index * phentsize)
    if kind == 1 and memsz:
        segments.append((address, blob[offset:offset + filesz], memsz))
if not segments:
    raise SystemExit('Linked ARM64 test ELF contains no PT_LOAD segments')

uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
low = min(address for address, _, _ in segments) & ~0xfff
high = (max(address + memsz for address, _, memsz in segments) + 0xfff) & ~0xfff
uc.mem_map(low, high - low)
for address, data, _ in segments:
    uc.mem_write(address, data)

DATA = 0x60000000
DATA_SIZE = 0x20000
STACK = 0x70000000
EXIT = 0x80000000
uc.mem_map(DATA, DATA_SIZE)
uc.mem_map(STACK, 0x10000)
uc.mem_map(EXIT, 0x1000)
passed = 0

def execute(symbol, src, dst, count):
    stopped = {'value': False}
    def on_code(machine, address, size, user):
        if address == EXIT:
            stopped['value'] = True
            machine.emu_stop()
    hook = uc.hook_add(UC_HOOK_CODE, on_code)
    uc.reg_write(UC_ARM64_REG_X0, src)
    uc.reg_write(UC_ARM64_REG_X1, dst)
    uc.reg_write(UC_ARM64_REG_X2, count)
    uc.reg_write(UC_ARM64_REG_SP, STACK + 0x8000)
    uc.reg_write(UC_ARM64_REG_X30, EXIT)
    uc.emu_start(symbols[symbol], 0, count=100000)
    uc.hook_del(hook)
    assert stopped['value'], f'{symbol} did not return'

def verify_copy(symbol, width, count, src_off, dst_off):
    global passed
    before = bytearray((i * 37 + 11) & 0xff for i in range(DATA_SIZE))
    expected = bytearray(before)
    byte_count = count * width
    expected[dst_off:dst_off + byte_count] = before[src_off:src_off + byte_count]
    uc.mem_write(DATA, bytes(before))
    execute(symbol, DATA + src_off, DATA + dst_off, count)
    actual = bytes(uc.mem_read(DATA, DATA_SIZE))
    assert actual == bytes(expected), (symbol, width, count, src_off, dst_off)
    passed += 1

for symbol, width in [('_Copy_conjoint_jshorts_atomic', 2),
                      ('_Copy_conjoint_jints_atomic', 4),
                      ('_Copy_conjoint_jlongs_atomic', 8)]:
    for count in [0, 1, 3, 17]:
        verify_copy(symbol, width, count, 0x4000, 0x5000)  # disjoint, high destination
        verify_copy(symbol, width, count, 0x5000, 0x4000)  # disjoint, low destination
    verify_copy(symbol, width, 17, 0x4000, 0x4000)         # exact self-copy
    verify_copy(symbol, width, 17, 0x4000, 0x4000 + width)  # backward overlap
    verify_copy(symbol, width, 17, 0x4000 + width, 0x4000)  # forward overlap

for symbol, width in [('_Copy_arrayof_conjoint_bytes', 1),
                      ('_Copy_arrayof_conjoint_jshorts', 2),
                      ('_Copy_arrayof_conjoint_jints', 4),
                      ('_Copy_arrayof_conjoint_jlongs', 8)]:
    for count in [0, 1, 3, 17, 127]:
        verify_copy(symbol, width, count, 0x6000, 0x7000)
        verify_copy(symbol, width, count, 0x7000, 0x6000)
    verify_copy(symbol, width, 31, 0x6000, 0x6000)
    verify_copy(symbol, width, 31, 0x6000, 0x6000 + width)
    verify_copy(symbol, width, 31, 0x6000 + width, 0x6000)

execute('SpinPause', 0, 0, 0)
assert uc.reg_read(UC_ARM64_REG_X0) == 1
passed += 1
print(f'EMULATOR-PASSED: {passed} actual AArch64 Zero copy/spin cases; '
      'ELF code executed by Unicorn; Switch hardware and native atomic ordering NOT TESTED')
