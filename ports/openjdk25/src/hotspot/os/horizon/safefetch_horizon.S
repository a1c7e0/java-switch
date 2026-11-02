/* SPDX-License-Identifier: GPL-2.0-only */
/* Experimental AArch64 fault islands. Kernel resume behavior is NOT TESTED on
 * Switch. The debug-policy save/restore sequence follows libnx exception.s.
 * libnx Copyright 2017-2018 libnx Authors.
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */
#if !defined(__aarch64__)
#error Horizon SafeFetch requires AArch64
#endif
.section .text.SafeFetch32_impl,"ax",%progbits
.balign 4
.global SafeFetch32_impl
.type SafeFetch32_impl,%function
SafeFetch32_impl:
.global js_horizon_safefetch32_fault
.hidden js_horizon_safefetch32_fault
js_horizon_safefetch32_fault:
    ldr w0, [x0]
.global js_horizon_safefetch32_continuation
.hidden js_horizon_safefetch32_continuation
js_horizon_safefetch32_continuation:
    ret
.size SafeFetch32_impl, .-SafeFetch32_impl

.section .text.SafeFetchN_impl,"ax",%progbits
.balign 4
.global SafeFetchN_impl
.type SafeFetchN_impl,%function
SafeFetchN_impl:
.global js_horizon_safefetchN_fault
.hidden js_horizon_safefetchN_fault
js_horizon_safefetchN_fault:
    ldr x0, [x0]
.global js_horizon_safefetchN_continuation
.hidden js_horizon_safefetchN_continuation
js_horizon_safefetchN_continuation:
    ret
.size SafeFetchN_impl, .-SafeFetchN_impl

/* CRT exception ABI: x0=descriptor, x1=ThreadExceptionFrameA64*. x0..8, LR/SP
 * are kernel-saved in the frame; x9..29 must be preserved by this entry. */
.section .text.__libnx_exception_entry,"ax",%progbits
.balign 4
.global __libnx_exception_entry
.type __libnx_exception_entry,%function
__libnx_exception_entry:
    cmp x0, #0x101
    b.ne .Lfatal
    cbz x1, .Lfatal
    ldr w2, [x1, #0x60] // PSTATE
    tbnz w2, #4, .Lfatal // reject AArch32
    ldr w2, [x1, #0x6c] // ESR
    tbnz w2, #6, .Lfatal // reject writes
    lsr w2, w2, #26
    cmp w2, #0x24 // Data Abort from a lower exception level
    b.ne .Lfatal
    ldr x2, [x1, #0x58] // saved PC
    adrp x3, js_horizon_safefetch32_fault
    add x3, x3, :lo12:js_horizon_safefetch32_fault
    cmp x2, x3
    b.eq .Ldebug_policy
    adrp x3, js_horizon_safefetchN_fault
    add x3, x3, :lo12:js_horizon_safefetchN_fault
    cmp x2, x3
    b.ne .Lfatal
.Ldebug_policy:
    /* Preserve the stock debugger policy, including failed GetInfo. The same
     * small native-stack scratch sequence is used by libnx's stock entry. */
    stp x9, x10, [sp, #-16]!
    stp x11, x12, [sp, #-16]!
    stp x13, x14, [sp, #-16]!
    stp x15, x16, [sp, #-16]!
    stp x17, x18, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    str x21, [sp, #-16]!
    stp x0, x1, [sp, #-16]!
    sub sp, sp, #16
    mov x0, sp
    mov x1, #8 // InfoType_IsCurrentProcessBeingDebugged
    mov w2, wzr
    mov x3, #0
    bl svcGetInfo
    mov w6, w0
    ldr x7, [sp], #16
    ldp x0, x1, [sp], #16
    ldr x21, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x17, x18, [sp], #16
    ldp x15, x16, [sp], #16
    ldp x13, x14, [sp], #16
    ldp x11, x12, [sp], #16
    ldp x9, x10, [sp], #16
    cbnz w6, .Lfatal_abort
    adrp x2, __nx_exception_ignoredebug
    ldr w2, [x2, :lo12:__nx_exception_ignoredebug]
    cbnz w2, .Lrecover
    cbnz x7, .Lfatal_abort
.Lrecover:
    ldr x2, [x1, #0x58]
    adrp x3, js_horizon_safefetch32_fault
    add x3, x3, :lo12:js_horizon_safefetch32_fault
    cmp x2, x3
    b.ne .LrecoverN
    adrp x3, js_horizon_safefetch32_continuation
    add x3, x3, :lo12:js_horizon_safefetch32_continuation
    b .Lresume
.LrecoverN:
    adrp x3, js_horizon_safefetchN_continuation
    add x3, x3, :lo12:js_horizon_safefetchN_continuation
.Lresume:
    ldr x2, [x1, #8] // original x1 = caller's error value
    str x2, [x1]     // restore as SafeFetch return value
    str x3, [x1, #0x58]
    mov w0, wzr
    bl svcReturnFromException
    b . // noreturn; never RET if exception return unexpectedly fails
/* Conditional branches cannot span a full statically linked VM: TB[N]Z has
 * only 32 KiB reach. Keep their targets local, then use an unconditional branch
 * (which also permits the linker to insert a range-extension thunk).
 */
.Lfatal:
    b js_horizon_unhandled_exception_entry
.Lfatal_abort:
    b js_horizon_unhandled_exception_entry_abort
.size __libnx_exception_entry, .-__libnx_exception_entry
.section .note.GNU-stack,"",%progbits
