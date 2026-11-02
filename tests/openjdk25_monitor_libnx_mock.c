/* SPDX-License-Identifier: GPL-2.0-only */
/* Host synchronization substitute for the C bridge, never a Horizon provider. */
#include <switch/kernel/semaphore.h>
#include <switch/kernel/thread.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <time.h>
_Atomic unsigned js_mock_malloc_fail, js_mock_force_timeout, js_mock_wait_error;
_Atomic uint64_t js_mock_timeout;
static _Atomic unsigned spurious_wakes;
static unsigned tls_allocated;
static _Atomic unsigned tls_fail;
static _Thread_local void *tls_values[16];
static void (*tls_destructors[16])(void *);
int threadTlsAlloc(void (*destructor)(void *)) {
    if (atomic_load(&tls_fail) || tls_allocated == 16) return -1;
    unsigned slot = tls_allocated++;
    tls_destructors[slot] = destructor; tls_values[slot] = NULL;
    return (int)slot;
}
void *threadTlsGet(int slot) { assert(slot >= 0 && (unsigned)slot < tls_allocated); return tls_values[slot]; }
void threadTlsSet(int slot, void *value) { assert(slot >= 0 && (unsigned)slot < tls_allocated); tls_values[slot] = value; }
/* Model libnx's one-pass clear-before-callback exit order. */
void js_mock_tls_exit(void) {
    for (unsigned slot = 0; slot < tls_allocated; slot++) {
        void *value = tls_values[slot]; tls_values[slot] = NULL;
        if (value && tls_destructors[slot]) tls_destructors[slot](value);
    }
}
void *__real_malloc(size_t);
void *__wrap_malloc(size_t size) {
    return atomic_exchange(&js_mock_malloc_fail, 0) ? NULL : __real_malloc(size);
}
void mutexInit(Mutex *m) { assert(pthread_mutex_init(m, NULL) == 0); }
void mutexLock(Mutex *m) { assert(pthread_mutex_lock(m) == 0); }
void mutexUnlock(Mutex *m) { assert(pthread_mutex_unlock(m) == 0); }
bool mutexTryLock(Mutex *m) {
    int rc = pthread_mutex_trylock(m); assert(rc == 0 || rc == EBUSY); return rc == 0;
}
void condvarInit(CondVar *c) { assert(pthread_cond_init(c, NULL) == 0); }
uint32_t condvarWaitTimeout(CondVar *c, Mutex *m, uint64_t nanos) {
    atomic_store(&js_mock_timeout, nanos);
    unsigned error = atomic_load(&js_mock_wait_error);
    if (error) return error;
    if (atomic_load(&js_mock_force_timeout)) return 0xea01;
    if (atomic_load(&spurious_wakes)) { atomic_fetch_sub(&spurious_wakes, 1); return 0; }
    int rc;
    if (nanos == UINT64_MAX) rc = pthread_cond_wait(c, m);
    else {
        struct timespec limit; assert(clock_gettime(CLOCK_REALTIME, &limit) == 0);
        limit.tv_sec += nanos / 1000000000;
        limit.tv_nsec += nanos % 1000000000;
        if (limit.tv_nsec >= 1000000000) { limit.tv_sec++; limit.tv_nsec -= 1000000000; }
        rc = pthread_cond_timedwait(c, m, &limit);
    }
    assert(rc == 0 || rc == ETIMEDOUT);
    return rc == ETIMEDOUT ? 0xea01 : 0;
}
uint32_t condvarWake(CondVar *c, int count) {
    assert((count == 1 ? pthread_cond_signal(c) : pthread_cond_broadcast(c)) == 0);
    return 0;
}
void semaphoreInit(Semaphore *s, uint64_t value) {
    mutexInit(&s->mutex); condvarInit(&s->condition); s->count = value;
}
void semaphoreSignal(Semaphore *s) {
    mutexLock(&s->mutex); s->count++; condvarWake(&s->condition, 1); mutexUnlock(&s->mutex);
}
void semaphoreWait(Semaphore *s) {
    mutexLock(&s->mutex);
    while (!s->count) assert(condvarWaitTimeout(&s->condition, &s->mutex, UINT64_MAX) == 0);
    s->count--; mutexUnlock(&s->mutex);
}
bool semaphoreTryWait(Semaphore *s) {
    mutexLock(&s->mutex); bool ok = s->count != 0; if (ok) s->count--; mutexUnlock(&s->mutex); return ok;
}
/* Keep C11 atomics out of HotSpot's C++ namespace and headers. */
void js_mock_set(unsigned mode, unsigned value) {
    if (mode == 0) atomic_store(&js_mock_malloc_fail, value);
    if (mode == 1) atomic_store(&js_mock_force_timeout, value);
    if (mode == 2) atomic_store(&js_mock_wait_error, value);
    if (mode == 3) atomic_store(&spurious_wakes, value);
    if (mode == 4) atomic_store(&tls_fail, value);
}
uint64_t js_mock_last_timeout(void) { return atomic_load(&js_mock_timeout); }
