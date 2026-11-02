/* SPDX-License-Identifier: GPL-2.0-only */
/* Test-only unhandled marker. Production uses the preserved libnx entry. */
.text
#ifndef PRODUCTION_FALLBACK
.global js_horizon_unhandled_exception_entry
js_horizon_unhandled_exception_entry:
    svc #0x7f
    b .
.global js_horizon_unhandled_exception_entry_abort
js_horizon_unhandled_exception_entry_abort:
    svc #0x7e
    b .
#else
.global __libnx_exception_handler
__libnx_exception_handler:
    svc #0x7d
    ret
.bss
.balign 16
.global __nx_exceptiondump
__nx_exceptiondump:
    .space 0x400
.global __nx_exception_stack
__nx_exception_stack:
    .space 0x1000
.data
.balign 8
.global __nx_exception_stack_size
__nx_exception_stack_size:
    .quad 0x1000
#endif
.data
.balign 4
.global __nx_exception_ignoredebug
__nx_exception_ignoredebug:
    .word 0
.section .note.GNU-stack,"",%progbits
