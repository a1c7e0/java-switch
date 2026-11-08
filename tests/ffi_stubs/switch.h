#ifndef JS_TEST_SWITCH_H
#define JS_TEST_SWITCH_H
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
typedef unsigned int Result;
typedef uint64_t u64;
typedef uint32_t u32;
typedef struct { uintptr_t unused; } ThreadExceptionDump;
#define R_FAILED(x) ((x) != 0)
#define R_SUCCEEDED(x) ((x) == 0)
typedef pthread_mutex_t Mutex;
static inline void mutexLock(Mutex *m) { pthread_mutex_lock(m); }
static inline void mutexUnlock(Mutex *m) { pthread_mutex_unlock(m); }
typedef struct { void *rw_addr, *rx_addr; size_t size; } Jit;
Result jitCreate(Jit *, size_t);
Result jitTransitionToWritable(Jit *);
Result jitTransitionToExecutable(Jit *);
Result jitClose(Jit *);
static inline void *jitGetRwAddr(Jit *j) { return j->rw_addr; }
static inline void *jitGetRxAddr(Jit *j) { return j->rx_addr; }
typedef struct VirtmemReservation VirtmemReservation;
enum { CUR_PROCESS_HANDLE = 0, InfoType_AliasRegionAddress = 2,
       InfoType_AliasRegionSize = 3, InfoType_SystemResourceSizeTotal = 16,
       InfoType_SystemResourceSizeUsed = 17,
       InfoType_AliasRegionExtraSize = 28,
       MemType_Unmapped = 0, MemType_Io, MemType_Normal, MemType_CodeStatic,
       MemType_CodeMutable, MemType_Heap, MemType_SharedMem,
       MemType_WeirdMappedMem, MemType_ModuleCodeStatic,
       MemType_ModuleCodeMutable, MemType_IpcBuffer0, MemType_MappedMemory,
       MemType_ThreadLocal, MemType_TransferMemIsolated, MemType_TransferMem,
       MemType_ProcessMem, MemType_Reserved, MemType_IpcBuffer1,
       MemType_IpcBuffer3, MemType_KernelStack, MemType_CodeReadOnly,
       MemType_CodeWritable, MemType_Coverage, MemType_Insecure };
typedef struct {
    u64 addr, size;
    u32 type, attr, perm, ipc_refcount, device_refcount, padding;
} MemoryInfo;
void virtmemLock(void);
void virtmemUnlock(void);
void *virtmemFindAslr(size_t, size_t);
VirtmemReservation *virtmemAddReservation(void *, size_t);
void virtmemRemoveReservation(VirtmemReservation *);
Result svcMapPhysicalMemory(void *, size_t);
Result svcUnmapPhysicalMemory(void *, size_t);
enum { Perm_None = 0, Perm_R = 1, Perm_Rw = 3 };
Result svcSetMemoryPermission(void *, size_t, unsigned);
Result svcGetInfo(u64 *, unsigned, uint64_t, uint64_t);
Result svcQueryMemory(MemoryInfo *, u32 *, u64);
#endif
