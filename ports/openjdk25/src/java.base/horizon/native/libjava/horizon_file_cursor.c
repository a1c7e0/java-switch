/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
#include "horizon_file_cursor.h"

#include <errno.h>

/* Keep the libnx monitor behind HotSpot's C bridge so libnx declarations do
 * not enter java.base C sources. These declarations match horizon_bridge.h. */
typedef struct js_horizon_monitor js_horizon_monitor;
extern js_horizon_monitor *js_horizon_monitor_create(void);
extern void js_horizon_monitor_lock(js_horizon_monitor *);
extern void js_horizon_monitor_unlock(js_horizon_monitor *);

/* 0 = uninitialized, 1 = one thread is initializing, 2 = ready. The monitor
 * intentionally lives for the process lifetime; destroying it would race
 * active Java I/O. GCC/Clang atomics are available in the Horizon toolchain. */
static volatile int monitor_state;
static js_horizon_monitor *cursor_monitor;

static js_horizon_monitor *get_cursor_monitor(void)
{
    for (;;) {
        int state = __atomic_load_n(&monitor_state, __ATOMIC_ACQUIRE);
        if (state == 2) return cursor_monitor;
        if (state == 0) {
            int expected = 0;
            if (__atomic_compare_exchange_n(&monitor_state, &expected, 1, 0,
                                           __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                js_horizon_monitor *created = js_horizon_monitor_create();
                if (created == NULL) {
                    __atomic_store_n(&monitor_state, 0, __ATOMIC_RELEASE);
                    return NULL;
                }
                cursor_monitor = created;
                __atomic_store_n(&monitor_state, 2, __ATOMIC_RELEASE);
                return created;
            }
        }
    }
}

int js_horizon_file_cursor_lock(void)
{
    js_horizon_monitor *monitor = get_cursor_monitor();
    if (monitor == NULL) {
        errno = ENOMEM;
        return -1;
    }
    js_horizon_monitor_lock(monitor);
    return 0;
}

void js_horizon_file_cursor_unlock(void)
{
    /* Callers invoke unlock only after lock returned success. */
    js_horizon_monitor_unlock(cursor_monitor);
}
