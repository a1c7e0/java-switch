/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TEST_SWITCH_KERNEL_SVC_H
#define TEST_SWITCH_KERNEL_SVC_H
#include <stdint.h>
typedef int32_t Result;
typedef int32_t Handle;
typedef uint64_t u64;
typedef int64_t s64;
#define CUR_PROCESS_HANDLE ((Handle)0xFFFF8001)
#define INVALID_HANDLE ((Handle)-1)
#define R_FAILED(r) ((r) < 0)
enum { InfoType_CoreMask = 0, InfoType_TotalMemorySize = 6,
       InfoType_UsedMemorySize = 7, InfoType_AslrRegionAddress = 12 };
Result svcGetInfo(u64* out, uint32_t id, Handle handle, u64 sub_id);
Result svcGetProcessId(u64* out, Handle handle);
void svcSleepThread(s64 nanoseconds);
void svcExitProcess(void) __attribute__((noreturn));
#endif
