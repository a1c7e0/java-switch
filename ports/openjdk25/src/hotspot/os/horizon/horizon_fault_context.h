/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OS_HORIZON_FAULT_CONTEXT_H
#define OS_HORIZON_FAULT_CONTEXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stable, libnx-type-free view of the public AArch64 ThreadExceptionDump. */
typedef struct js_horizon_fault_context {
  uint64_t gpr[29];
  uint64_t fp;
  uint64_t lr;
  uint64_t sp;
  uint64_t pc;
  uint32_t pstate;
  uint32_t esr;
  uint32_t error_desc;
  uint64_t far;
} js_horizon_fault_context;

/* Returns 1 for an AArch64 dump copied to out, 0 for null/unsupported data. */
int js_horizon_fault_context_read(const void *dump,
                                  js_horizon_fault_context *out);

#ifdef __cplusplus
}
#endif
#endif
