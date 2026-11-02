/* SPDX-License-Identifier: GPL-2.0-only */
#include "runtime/os.hpp"
#include "mutex_horizon.hpp"
#include "semaphore_horizon.hpp"
#include "park_horizon.hpp"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
extern "C" void js_mock_set(unsigned, unsigned);
extern "C" uint64_t js_mock_last_timeout();
[[noreturn]] void report_vm_error(const char*, int, const char* error, const char* format, ...) {
    fprintf(stderr, "%s: ", error); va_list args; va_start(args, format);
    vfprintf(stderr, format, args); va_end(args); abort();
}
static void check(bool ok) { if (!ok) abort(); }
struct State { PlatformMonitor monitor; unsigned count = 0, ready = 0; bool release = false; };
static void* increment(void* arg) {
    auto* s = static_cast<State*>(arg);
    for (unsigned i = 0; i < 20000; i++) { s->monitor.lock(); s->count++; s->monitor.unlock(); }
    return nullptr;
}
static void* waiter(void* arg) {
    auto* s = static_cast<State*>(arg);
    s->monitor.lock(); s->ready++; s->monitor.notify_all();
    while (!s->release) check(s->monitor.wait(0) == OS_OK);
    s->count++; s->monitor.unlock(); return nullptr;
}
struct PingPong { HorizonParkPermit ping, pong; unsigned count = 0; };
static void* ping_worker(void* arg) {
    auto* s = static_cast<PingPong*>(arg);
    for (unsigned i = 0; i < 5000; i++) {
        check(s->ping.park(1000000000) == OS_OK); s->count++; s->pong.unpark();
    }
    return nullptr;
}
int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "oom")) { js_mock_set(0, 1); PlatformMutex m; return 1; }
    if (argc > 1 && !strcmp(argv[1], "semaphore-oom")) { js_mock_set(0, 1); HorizonSemaphore s; return 1; }
    if (argc > 1 && !strcmp(argv[1], "wait-error")) {
        PlatformMonitor m; m.lock(); js_mock_set(2, 0xf601); m.wait(1); return 1;
    }
    State s;
    check(s.monitor.try_lock()); check(!s.monitor.try_lock()); s.monitor.unlock();
    pthread_t threads[4];
    for (auto& t : threads) check(!pthread_create(&t, nullptr, increment, &s));
    for (auto& t : threads) check(!pthread_join(t, nullptr));
    check(s.count == 80000); s.count = 0;
    for (auto& t : threads) check(!pthread_create(&t, nullptr, waiter, &s));
    s.monitor.lock();
    while (s.ready != 4) check(s.monitor.wait(1000) == OS_OK);
    s.release = true; s.monitor.notify_all(); s.monitor.unlock();
    for (auto& t : threads) check(!pthread_join(t, nullptr));
    check(s.count == 4);
    s.monitor.lock();
    check(s.monitor.wait(2) == OS_TIMEOUT); check(!s.monitor.try_lock());
    js_mock_set(1, 1);
    check(s.monitor.wait(UINT64_MAX) == OS_TIMEOUT); check(js_mock_last_timeout() == INT64_MAX);
    check(s.monitor.wait(1) == OS_TIMEOUT); check(js_mock_last_timeout() == 1000000);
    check(s.monitor.wait(0) == OS_TIMEOUT); check(js_mock_last_timeout() == UINT64_MAX);
    js_mock_set(1, 0); s.monitor.unlock();
    HorizonParkPermit permit;
    permit.unpark(); permit.unpark(); check(permit.fired() == 1);
    check(permit.park(0) == OS_OK); check(permit.park(0) == OS_TIMEOUT);
    permit.unpark(); check(permit.try_consume()); check(!permit.try_consume());
    permit.unpark(); permit.reset(); check(!permit.fired());
    js_mock_set(3, 10); check(permit.park(1000000) == OS_TIMEOUT);
    PingPong handoff; pthread_t worker;
    check(!pthread_create(&worker, nullptr, ping_worker, &handoff));
    for (unsigned i = 0; i < 5000; i++) {
        handoff.ping.unpark(); check(handoff.pong.park(UINT64_MAX) == OS_OK);
    }
    check(!pthread_join(worker, nullptr)); check(handoff.count == 5000);
    puts("real PlatformMutex/Monitor + C bridge: HOST-PASSED (pthread libnx mocks)");
    puts("80000 protected increments, predicate/broadcast waits, timeout ownership, overflow/infinite conversion: PASS");
    puts("coalesced permits, reset, spurious wakes, 5000 two-thread parking handoffs: PASS");
}
