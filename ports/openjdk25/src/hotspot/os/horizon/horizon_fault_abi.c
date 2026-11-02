/* SPDX-License-Identifier: GPL-2.0-only */
/* Fail compilation if the installed public libnx frame ABI disagrees with ASM. */
#include <stddef.h>
#include <switch/arm/thread_context.h>
_Static_assert(offsetof(ThreadExceptionFrameA64, cpu_gprs[1]) == 8, "saved x1 ABI");
_Static_assert(offsetof(ThreadExceptionFrameA64, lr) == 0x48, "LR ABI");
_Static_assert(offsetof(ThreadExceptionFrameA64, sp) == 0x50, "SP ABI");
_Static_assert(offsetof(ThreadExceptionFrameA64, elr_el1) == 0x58, "PC ABI");
_Static_assert(offsetof(ThreadExceptionFrameA64, pstate) == 0x60, "PSTATE ABI");
_Static_assert(offsetof(ThreadExceptionFrameA64, esr) == 0x6c, "ESR ABI");
_Static_assert(offsetof(ThreadExceptionFrameA64, far) == 0x70, "FAR ABI");
_Static_assert(sizeof(ThreadExceptionFrameA64) == 0x78, "frame size ABI");
_Static_assert(ThreadExceptionDesc_Other == 0x101, "descriptor ABI");
