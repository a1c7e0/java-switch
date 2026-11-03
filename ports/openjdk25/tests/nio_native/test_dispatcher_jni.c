/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "jni.h"
#include "jni_util.h"
#include "horizon_file_cursor.h"
#include "horizon_file_io.h"
#include "sun_nio_ch_UnixFileDispatcherImpl.h"

static pthread_mutex_t host_cursor_mutex = PTHREAD_MUTEX_INITIALIZER;

int js_horizon_file_cursor_lock(void) {
    int error = pthread_mutex_lock(&host_cursor_mutex);
    if (error != 0) {
        errno = error;
        return -1;
    }
    return 0;
}
void js_horizon_file_cursor_unlock(void) {
    assert(pthread_mutex_unlock(&host_cursor_mutex) == 0);
}

jint fdval(JNIEnv *env, jobject descriptor) {
    (void)env;
    return (jint)(intptr_t)descriptor;
}
jint convertReturnVal(JNIEnv *env, jint result, jboolean reading) {
    (void)env;
    if (result > 0) return result;
    if (result == 0) return reading ? -1 : 0;
    if (errno == EINTR) return -3;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return -2;
    return -4;
}
jlong convertLongReturnVal(JNIEnv *env, jlong result, jboolean reading) {
    (void)env;
    if (result > 0) return result;
    if (result == 0) return reading ? -1 : 0;
    if (errno == EINTR) return -3;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return -2;
    return -4;
}
void JNU_ThrowByName(JNIEnv *env, const char *name, const char *message) {
    (void)env; (void)name; (void)message;
}
void JNU_ThrowIOException(JNIEnv *env, const char *message) {
    (void)env; (void)message;
}
void JNU_ThrowIOExceptionWithLastError(JNIEnv *env, const char *message) {
    (void)env; (void)message;
}
void JNU_ThrowOutOfMemoryError(JNIEnv *env, const char *message) {
    (void)env; (void)message;
}

struct cursor_gate {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int holder_ready;
    int release_holder;
    int reader_started;
    int reader_done;
    int fd;
    jint read_result;
    char read_buffer[4];
};

static void *cursor_holder(void *arg) {
    struct cursor_gate *gate = arg;
    assert(js_horizon_file_cursor_lock() == 0);
    assert(lseek(gate->fd, 4, SEEK_SET) == 4);
    assert(pthread_mutex_lock(&gate->mutex) == 0);
    gate->holder_ready = 1;
    assert(pthread_cond_broadcast(&gate->condition) == 0);
    while (!gate->release_holder)
        assert(pthread_cond_wait(&gate->condition, &gate->mutex) == 0);
    assert(pthread_mutex_unlock(&gate->mutex) == 0);
    js_horizon_file_cursor_unlock();
    return NULL;
}

static void *positional_reader(void *arg) {
    struct cursor_gate *gate = arg;
    assert(pthread_mutex_lock(&gate->mutex) == 0);
    while (!gate->holder_ready)
        assert(pthread_cond_wait(&gate->condition, &gate->mutex) == 0);
    gate->reader_started = 1;
    assert(pthread_cond_broadcast(&gate->condition) == 0);
    assert(pthread_mutex_unlock(&gate->mutex) == 0);

    gate->read_result = Java_sun_nio_ch_UnixFileDispatcherImpl_pread0(
            NULL, NULL, (jobject)(intptr_t)gate->fd,
            (jlong)(intptr_t)gate->read_buffer, 3, 0);

    assert(pthread_mutex_lock(&gate->mutex) == 0);
    gate->reader_done = 1;
    assert(pthread_cond_broadcast(&gate->condition) == 0);
    assert(pthread_mutex_unlock(&gate->mutex) == 0);
    return NULL;
}

int main(void) {
    char path[] = "/tmp/horizon-nio-jni-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    assert(write(fd, "abcdef", 6) == 6);
    assert(lseek(fd, 2, SEEK_SET) == 2);

    jobject descriptor = (jobject)(intptr_t)fd;
    char positional[4] = {0};
    jint bytes = Java_sun_nio_ch_UnixFileDispatcherImpl_pread0(
            NULL, NULL, descriptor, (jlong)(intptr_t)positional, 3, 0);
    assert(bytes == 3 && memcmp(positional, "abc", 3) == 0);
    assert(lseek(fd, 0, SEEK_CUR) == 2);

    bytes = Java_sun_nio_ch_UnixFileDispatcherImpl_pread0(
            NULL, NULL, descriptor, (jlong)(intptr_t)positional, 3, 32);
    assert(bytes == -1); /* the wrapper maps a zero read to IOStatus.EOF */
    assert(lseek(fd, 0, SEEK_CUR) == 2);

    assert(lseek(fd, 0, SEEK_SET) == 0);
    char left[3] = {0};
    char right[5] = {0};
    struct horizon_iovec read_vectors[] = {
        { .base = left, .length = 2 },
        { .base = right, .length = 4 },
    };
    jlong vector_bytes = Java_sun_nio_ch_UnixFileDispatcherImpl_readv0(
            NULL, NULL, descriptor,
            (jlong)(intptr_t)read_vectors, 2);
    assert(vector_bytes == 6);
    assert(memcmp(left, "ab", 2) == 0);
    assert(memcmp(right, "cdef", 4) == 0);
    assert(lseek(fd, 0, SEEK_CUR) == 6);

    assert(lseek(fd, 0, SEEK_SET) == 0);
    const char first[] = "12";
    const char second[] = "345";
    struct horizon_iovec write_vectors[] = {
        { .base = (void *)first, .length = 2 },
        { .base = (void *)second, .length = 3 },
    };
    vector_bytes = Java_sun_nio_ch_UnixFileDispatcherImpl_writev0(
            NULL, NULL, descriptor,
            (jlong)(intptr_t)write_vectors, 2);
    assert(vector_bytes == 5);
    assert(lseek(fd, 0, SEEK_CUR) == 5);

    struct cursor_gate gate = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .condition = PTHREAD_COND_INITIALIZER,
        .fd = fd,
    };
    pthread_t holder;
    pthread_t reader;
    assert(pthread_create(&holder, NULL, cursor_holder, &gate) == 0);
    assert(pthread_mutex_lock(&gate.mutex) == 0);
    while (!gate.holder_ready)
        assert(pthread_cond_wait(&gate.condition, &gate.mutex) == 0);
    assert(pthread_mutex_unlock(&gate.mutex) == 0);
    assert(pthread_create(&reader, NULL, positional_reader, &gate) == 0);
    assert(pthread_mutex_lock(&gate.mutex) == 0);
    while (!gate.reader_started)
        assert(pthread_cond_wait(&gate.condition, &gate.mutex) == 0);
    assert(pthread_mutex_unlock(&gate.mutex) == 0);
    struct timespec pause = { .tv_sec = 0, .tv_nsec = 10000000 };
    assert(nanosleep(&pause, NULL) == 0);
    assert(pthread_mutex_lock(&gate.mutex) == 0);
    assert(!gate.reader_done); /* positional wrapper waits for the shared lock */
    gate.release_holder = 1;
    assert(pthread_cond_broadcast(&gate.condition) == 0);
    assert(pthread_mutex_unlock(&gate.mutex) == 0);
    assert(pthread_join(holder, NULL) == 0);
    assert(pthread_join(reader, NULL) == 0);
    assert(gate.read_result == 3 && memcmp(gate.read_buffer, "123", 3) == 0);
    assert(lseek(fd, 0, SEEK_CUR) == 4);

    assert(pthread_cond_destroy(&gate.condition) == 0);
    assert(pthread_mutex_destroy(&gate.mutex) == 0);

    assert(close(fd) == 0);
    assert(unlink(path) == 0);
    puts("Horizon NIO JNI dispatcher host test: PASS");
    return 0;
}
