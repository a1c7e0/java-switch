#ifndef JS_TEST_SWITCH_H
#define JS_TEST_SWITCH_H
#include <stddef.h>
#include <pthread.h>
typedef unsigned int Result;
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
void virtmemLock(void);
void virtmemUnlock(void);
void *virtmemFindAslr(size_t, size_t);
VirtmemReservation *virtmemAddReservation(void *, size_t);
void virtmemRemoveReservation(VirtmemReservation *);
Result svcMapPhysicalMemory(void *, size_t);
Result svcUnmapPhysicalMemory(void *, size_t);
#endif
