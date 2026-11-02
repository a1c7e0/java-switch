/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HORIZON_MEMORY_TEST_SWITCH_H
#define HORIZON_MEMORY_TEST_SWITCH_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t Result;
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct { pthread_mutex_t mutex; } Mutex;
typedef struct {
  u64 addr;
  u64 size;
  u32 type;
  u32 attr;
  u32 perm;
  u32 ipc_refcount;
  u32 device_refcount;
  u32 padding;
} MemoryInfo;
typedef struct VirtmemReservation {
  uintptr_t address;
  size_t size;
  struct VirtmemReservation* next;
} VirtmemReservation;

enum {
  MemType_Unmapped = 0,
  MemType_Io = 1,
  MemType_Normal = 2,
  MemType_CodeStatic = 3,
  MemType_CodeMutable = 4,
  MemType_Heap = 5,
  MemType_SharedMem = 6,
  MemType_WeirdMappedMem = 7,
  MemType_ModuleCodeStatic = 8,
  MemType_ModuleCodeMutable = 9,
  MemType_IpcBuffer0 = 10,
  MemType_MappedMemory = 11,
  MemType_ThreadLocal = 12,
  MemType_TransferMemIsolated = 13,
  MemType_TransferMem = 14,
  MemType_ProcessMem = 15,
  MemType_Reserved = 16,
  MemType_IpcBuffer1 = 17,
  MemType_IpcBuffer3 = 18,
  MemType_KernelStack = 19,
  MemType_CodeReadOnly = 20,
  MemType_CodeWritable = 21,
  MemType_Coverage = 22,
  MemType_Insecure = 23
};
enum { Perm_None = 0, Perm_R = 1, Perm_W = 2, Perm_Rw = 3 };
enum {
  InfoType_AliasRegionAddress = 1,
  InfoType_AliasRegionSize = 2,
  InfoType_AliasRegionExtraSize = 3
};
#define CUR_PROCESS_HANDLE UINT64_MAX
enum { Module_Kernel = 1, KernelError_NotImplemented = 33 };
#define R_SUCCEEDED(result) ((result) == 0)
#define R_FAILED(result) ((result) != 0)
#define MAKERESULT(module, description) (((module) & 0x1ff) | (((description) & 0x1fff) << 9))
#define KERNELRESULT(description) MAKERESULT(Module_Kernel, KernelError_##description)

void mutexLock(Mutex* mutex);
void mutexUnlock(Mutex* mutex);
void virtmemLock(void);
void virtmemUnlock(void);
void* virtmemFindAslr(size_t size, size_t guard_size);
VirtmemReservation* virtmemAddReservation(void* address, size_t size);
void virtmemRemoveReservation(VirtmemReservation* reservation);
Result svcMapPhysicalMemory(void* address, uint64_t size);
Result svcUnmapPhysicalMemory(void* address, uint64_t size);
Result svcSetMemoryPermission(void* address, uint64_t size, u32 permission);
Result svcGetInfo(u64* out, unsigned id, uint64_t handle, uint64_t sub_id);
Result svcQueryMemory(MemoryInfo* info, u32* page_info, u64 address);

#endif
