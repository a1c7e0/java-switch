#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>

#include <string>

#include "jni.h"
#include "osSupport.hpp"
#include "horizon_bridge.h"
#include "horizon_memory_bridge.h"

struct FakeReservation {
    void* address;
    size_t size;
    int committed;
    int protected_read;
};

static FakeReservation reservation;
static unsigned int releases;
static unsigned int release_attempts;
static unsigned int monitor_creates;
static unsigned int monitor_locks;
static int fail_release;
static int fail_commit;
static int fail_protect;
static int fail_monitor_create;

struct js_horizon_monitor {
    pthread_mutex_t mutex;
};

extern "C" js_horizon_monitor* js_horizon_monitor_create(void) {
    if (fail_monitor_create) return NULL;
    js_horizon_monitor* monitor =
        static_cast<js_horizon_monitor*>(malloc(sizeof(*monitor)));
    if (monitor != NULL && pthread_mutex_init(&monitor->mutex, NULL) == 0) {
        monitor_creates++;
        return monitor;
    }
    free(monitor);
    return NULL;
}

extern "C" void js_horizon_monitor_destroy(js_horizon_monitor* monitor) {
    if (monitor != NULL) {
        pthread_mutex_destroy(&monitor->mutex);
        free(monitor);
    }
}

extern "C" void js_horizon_monitor_lock(js_horizon_monitor* monitor) {
    assert(monitor != NULL);
    assert(pthread_mutex_lock(&monitor->mutex) == 0);
    monitor_locks++;
}

extern "C" void js_horizon_monitor_unlock(js_horizon_monitor* monitor) {
    assert(monitor != NULL);
    assert(pthread_mutex_unlock(&monitor->mutex) == 0);
}

extern "C" int js_horizon_monitor_trylock(js_horizon_monitor*) { return 0; }
extern "C" uint32_t js_horizon_monitor_wait(js_horizon_monitor*, uint64_t) {
    return 0;
}
extern "C" void js_horizon_monitor_notify(js_horizon_monitor*, int) {}

extern "C" js_horizon_memory_status js_horizon_memory_reserve(
        size_t size, size_t alignment, void** address, uint32_t* raw_result) {
    *address = NULL;
    *raw_result = 0;
    if (reservation.address != NULL || size == 0 || alignment < sizeof(void*))
        return JS_HORIZON_MEMORY_STATE;
    void* memory = NULL;
    if (posix_memalign(&memory, alignment, size) != 0)
        return JS_HORIZON_MEMORY_NO_MEMORY;
    reservation.address = memory;
    reservation.size = size;
    reservation.committed = 0;
    reservation.protected_read = 0;
    *address = memory;
    return JS_HORIZON_MEMORY_OK;
}

extern "C" js_horizon_memory_status js_horizon_memory_commit(
        void* address, size_t size, uint32_t* raw_result) {
    *raw_result = 0;
    if (address != reservation.address || size != reservation.size)
        return JS_HORIZON_MEMORY_INVALID;
    if (fail_commit) {
        *raw_result = UINT32_C(0x12345678);
        return JS_HORIZON_MEMORY_SYSTEM;
    }
    reservation.committed = 1;
    return JS_HORIZON_MEMORY_OK;
}

extern "C" js_horizon_memory_status js_horizon_memory_protect(
        void* address, size_t size, js_horizon_memory_access access,
        uint32_t* raw_result) {
    *raw_result = 0;
    if (address != reservation.address || size != reservation.size ||
        !reservation.committed || access != JS_HORIZON_MEMORY_READ)
        return JS_HORIZON_MEMORY_INVALID;
    if (fail_protect) {
        *raw_result = UINT32_C(0x87654321);
        return JS_HORIZON_MEMORY_SYSTEM;
    }
    reservation.protected_read = 1;
    return JS_HORIZON_MEMORY_OK;
}

extern "C" js_horizon_memory_status js_horizon_memory_release(
        void* address, size_t size, uint32_t* raw_result) {
    *raw_result = 0;
    if (address != reservation.address || size != reservation.size)
        return JS_HORIZON_MEMORY_INVALID;
    release_attempts++;
    if (fail_release) {
        *raw_result = UINT32_C(0xdeadbeef);
        return JS_HORIZON_MEMORY_SYSTEM;
    }
    free(reservation.address);
    memset(&reservation, 0, sizeof(reservation));
    releases++;
    return JS_HORIZON_MEMORY_OK;
}

extern "C" js_horizon_memory_status js_horizon_memory_uncommit(
        void*, size_t, uint32_t*) {
    return JS_HORIZON_MEMORY_UNSUPPORTED;
}

extern "C" js_horizon_memory_status js_horizon_memory_committed_in_range(
        void*, size_t, void**, size_t*, uint32_t*) {
    return JS_HORIZON_MEMORY_UNSUPPORTED;
}

static std::string make_file() {
    char name[] = "/tmp/javaswitch-jimage-XXXXXX";
    int fd = mkstemp(name);
    assert(fd >= 0);
    const char contents[] = "0123456789abcdef";
    assert(write(fd, contents, sizeof(contents) - 1) ==
           static_cast<ssize_t>(sizeof(contents) - 1));
    assert(close(fd) == 0);
    return std::string(name);
}

int main() {
    assert(monitor_creates >= 2);
    const std::string path = make_file();
    assert(osSupport::size(path.c_str()) == 16);
    assert(osSupport::size("/tmp") == -1);
    jint fd = osSupport::openReadOnly(path.c_str());
    assert(fd >= 0);

    assert(lseek(fd, 7, SEEK_SET) == 7);
    char data[8] = {};
    assert(osSupport::read(fd, data, 4, 2) == 4);
    assert(memcmp(data, "2345", 4) == 0);
    assert(lseek(fd, 0, SEEK_CUR) == 7);
    memset(data, 0, sizeof(data));
    assert(osSupport::read(fd, data, 8, 13) == 3);
    assert(memcmp(data, "def", 3) == 0);
    assert(lseek(fd, 0, SEEK_CUR) == 7);
    assert(osSupport::read(fd, data, 1, -1) == -1);
    assert(osSupport::read(fd, data, 2, INT64_MAX) == -1);
    assert(lseek(fd, 0, SEEK_CUR) == 7);

    void* index = osSupport::map_memory(fd, path.c_str(), 3, 5);
    assert(index != NULL);
    assert((reinterpret_cast<uintptr_t>(index) & 4095u) == 0);
    assert(memcmp(index, "34567", 5) == 0);
    assert(reservation.size == 4096 && reservation.protected_read);
    assert(lseek(fd, 0, SEEK_CUR) == 7);
    assert(osSupport::unmap_memory(index, 1) == 0);
    assert(reservation.address == index);
    fail_release = 1;
    assert(osSupport::unmap_memory(index, 5) == 0);
    assert(reservation.address == index);
    fail_release = 0;
    assert(osSupport::unmap_memory(index, 5) == 1);
    assert(releases == 1 && reservation.address == NULL);
    assert(osSupport::unmap_memory(index, 5) == 0);

    /* A commit failure rolls back the reserved pages. */
    fail_commit = 1;
    assert(osSupport::map_memory(fd, path.c_str(), 3, 5) == NULL);
    fail_commit = 0;
    assert(releases == 2 && reservation.address == NULL);

    /* A protect failure also rolls back the reserved and committed pages. */
    fail_protect = 1;
    assert(osSupport::map_memory(fd, path.c_str(), 3, 5) == NULL);
    fail_protect = 0;
    assert(releases == 3 && reservation.address == NULL);

    /* A short image read must discard the entire unpublished snapshot. */
    void* partial = osSupport::map_memory(fd, path.c_str(), 14, 4);
    assert(partial == NULL);
    assert(releases == 4 && reservation.address == NULL);
    assert(release_attempts == 5);

    /* A system release error during rollback is fail-closed, with Result kept. */
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        fail_release = 1;
        (void)osSupport::map_memory(fd, path.c_str(), 14, 4);
        _exit(0);
    }
    int child_status = 0;
    assert(waitpid(child, &child_status, 0) == child);
    assert(WIFSIGNALED(child_status));

    /* Monitor allocation cannot silently produce an unlocked critical section. */
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        fail_monitor_create = 1;
        SimpleCriticalSection unavailable;
        (void)unavailable;
        _exit(0);
    }
    child_status = 0;
    assert(waitpid(child, &child_status, 0) == child);
    assert(WIFSIGNALED(child_status));

    /* A non-EINTR read error is reported as an error, with cursor restored. */
    int proc_mem = ::open("/proc/self/mem", O_RDONLY);
    if (proc_mem >= 0) {
        assert(lseek(proc_mem, 17, SEEK_SET) == 17);
        errno = 0;
        assert(osSupport::read(proc_mem, data, 1, 0) == -1);
        assert(errno != 0);
        assert(lseek(proc_mem, 0, SEEK_CUR) == 17);
        assert(::close(proc_mem) == 0);
    }

    assert(osSupport::close(fd) == 0);
    assert(unlink(path.c_str()) == 0);
    assert(monitor_locks > 0);
    return 0;
}
