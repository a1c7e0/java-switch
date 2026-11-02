/* SPDX-License-Identifier: GPL-2.0-only */
/* Fail compilation if the installed public libnx frame ABI disagrees with ASM. */
#include <stddef.h>
#include <stdint.h>
#include "horizon_fault_context.h"
#include <switch/arm/thread_context.h>
_Static_assert(offsetof(ThreadExceptionDump, error_desc) == 0, "dump descriptor ABI");
_Static_assert(offsetof(ThreadExceptionDump, cpu_gprs) == 0x10, "dump GPR ABI");
_Static_assert(offsetof(ThreadExceptionDump, fp) == 0xf8, "dump FP ABI");
_Static_assert(offsetof(ThreadExceptionDump, lr) == 0x100, "dump LR ABI");
_Static_assert(offsetof(ThreadExceptionDump, sp) == 0x108, "dump SP ABI");
_Static_assert(offsetof(ThreadExceptionDump, pc) == 0x110, "dump PC ABI");
_Static_assert(offsetof(ThreadExceptionDump, pstate) == 0x320, "dump PSTATE ABI");
_Static_assert(offsetof(ThreadExceptionDump, esr) == 0x32c, "dump ESR ABI");
_Static_assert(offsetof(ThreadExceptionDump, far) == 0x330, "dump FAR ABI");
_Static_assert(sizeof(ThreadExceptionDump) == 0x340, "dump size ABI");
_Static_assert(offsetof(ThreadExceptionFrameA64, cpu_gprs[1]) == 8, "saved x1 ABI");
_Static_assert(offsetof(ThreadExceptionFrameA64, lr) == 0x48, "LR ABI");
_Static_assert(offsetof(ThreadExceptionFrameA64, sp) == 0x50, "SP ABI");
_Static_assert(offsetof(ThreadExceptionFrameA64, elr_el1) == 0x58, "PC ABI");
_Static_assert(offsetof(ThreadExceptionFrameA64, pstate) == 0x60, "PSTATE ABI");
_Static_assert(offsetof(ThreadExceptionFrameA64, esr) == 0x6c, "ESR ABI");
_Static_assert(offsetof(ThreadExceptionFrameA64, far) == 0x70, "FAR ABI");
_Static_assert(sizeof(ThreadExceptionFrameA64) == 0x78, "frame size ABI");
_Static_assert(ThreadExceptionDesc_Other == 0x101, "descriptor ABI");

int js_horizon_fault_context_read(const void *dump,
                                  js_horizon_fault_context *out) {
    if (dump == NULL || out == NULL) return 0;
    const ThreadExceptionDump *ctx = (const ThreadExceptionDump *)dump;
    if (!threadExceptionIsAArch64(ctx)) return 0;
    for (size_t i = 0; i < 29; ++i) out->gpr[i] = ctx->cpu_gprs[i].x;
    out->fp = ctx->fp.x;
    out->lr = ctx->lr.x;
    out->sp = ctx->sp.x;
    out->pc = ctx->pc.x;
    out->pstate = (uint32_t)ctx->pstate;
    out->esr = (uint32_t)ctx->esr;
    out->error_desc = (uint32_t)ctx->error_desc;
    out->far = ctx->far.x;
    return 1;
}
