/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "io_util_md.h"

jfieldID IO_fd_fdID;
jfieldID IO_handle_fdID;
jfieldID IO_append_fdID;
static char process_handle_token;
static pthread_mutex_t cursor_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t cursor_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cursor_state_changed = PTHREAD_COND_INITIALIZER;
static unsigned cursor_waiters;
static int positional_ready;

int js_horizon_file_cursor_lock(void) {
    int result = pthread_mutex_trylock(&cursor_mutex);
    if (result == 0) return 0;
    if (result != EBUSY) {
        errno = result;
        return -1;
    }
    assert(pthread_mutex_lock(&cursor_state_mutex) == 0);
    cursor_waiters++;
    assert(pthread_cond_broadcast(&cursor_state_changed) == 0);
    assert(pthread_mutex_unlock(&cursor_state_mutex) == 0);
    result = pthread_mutex_lock(&cursor_mutex);
    if (result != 0) {
        errno = result;
        return -1;
    }
    return 0;
}

void js_horizon_file_cursor_unlock(void) {
    assert(pthread_mutex_unlock(&cursor_mutex) == 0);
}

void *js_horizon_process_handle(void) {
    return &process_handle_token;
}

jstring JNU_NewStringPlatform(JNIEnv *env, const char *value) {
    (void)env;
    (void)value;
    return (jstring)&process_handle_token;
}

void JNU_ThrowIOExceptionWithLastError(JNIEnv *env, const char *message) {
    (void)env;
    fprintf(stderr, "unexpected IOException: %s (errno=%d)\n",
            message != NULL ? message : "(no message)", errno);
    abort();
}

void JNU_ThrowOutOfMemoryError(JNIEnv *env, const char *message) {
    (void)env;
    fprintf(stderr, "unexpected OutOfMemoryError: %s\n",
            message != NULL ? message : "(no message)");
    abort();
}

void JNU_ThrowNullPointerException(JNIEnv *env, const char *message) {
    (void)env;
    (void)message;
    abort();
}

const char *JNU_GetStringPlatformChars(JNIEnv *env, jstring value,
                                       jboolean *copied) {
    (void)env;
    (void)value;
    (void)copied;
    abort();
}

void JNU_ReleaseStringPlatformChars(JNIEnv *env, jstring value,
                                    const char *chars) {
    (void)env;
    (void)value;
    (void)chars;
}

void throwFileNotFoundException(JNIEnv *env, jstring path) {
    (void)env;
    (void)path;
    abort();
}

struct positional_read_test {
    int fd;
    off_t offset;
    off_t original;
    char value;
    int failed;
};

static void *positional_read_worker(void *opaque) {
    struct positional_read_test *test = opaque;
    if (js_horizon_file_cursor_lock() != 0) {
        test->failed = 1;
        return NULL;
    }
    test->original = lseek(test->fd, 0, SEEK_CUR);
    if (test->original == (off_t)-1 ||
        lseek(test->fd, test->offset, SEEK_SET) != test->offset) {
        test->failed = 2;
        js_horizon_file_cursor_unlock();
        return NULL;
    }

    assert(pthread_mutex_lock(&cursor_state_mutex) == 0);
    positional_ready = 1;
    assert(pthread_cond_broadcast(&cursor_state_changed) == 0);
    struct timespec deadline;
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 2;
    int wait_result = 0;
    while (cursor_waiters == 0 && wait_result == 0) {
        wait_result = pthread_cond_timedwait(&cursor_state_changed,
                                            &cursor_state_mutex, &deadline);
    }
    int observed_waiter = cursor_waiters != 0;
    assert(pthread_mutex_unlock(&cursor_state_mutex) == 0);
    if (!observed_waiter) test->failed = 3;

    ssize_t count;
    do {
        count = read(test->fd, &test->value, 1);
    } while (count < 0 && errno == EINTR);
    if (count != 1 || lseek(test->fd, test->original, SEEK_SET) != test->original)
        test->failed = 4;
    js_horizon_file_cursor_unlock();
    return NULL;
}

static void test_shared_cursor_lock(void) {
    char path[] = "/tmp/javaswitch-cursor-XXXXXX";
    int raw_fd = mkstemp(path);
    assert(raw_fd >= 0);
    char bytes[128];
    for (size_t i = 0; i < sizeof(bytes); i++) bytes[i] = (char)('A' + i % 26);
    assert(write(raw_fd, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes));
    assert(close(raw_fd) == 0);

    FD fd = handleOpen(path, O_RDWR, 0600);
    assert(fd >= 0);
    struct positional_read_test positional = {fd, 50, 0, 0, 0};
    pthread_t thread;
    assert(pthread_mutex_lock(&cursor_state_mutex) == 0);
    cursor_waiters = 0;
    positional_ready = 0;
    assert(pthread_mutex_unlock(&cursor_state_mutex) == 0);
    assert(pthread_create(&thread, NULL, positional_read_worker, &positional) == 0);
    assert(pthread_mutex_lock(&cursor_state_mutex) == 0);
    while (!positional_ready)
        assert(pthread_cond_wait(&cursor_state_changed, &cursor_state_mutex) == 0);
    assert(pthread_mutex_unlock(&cursor_state_mutex) == 0);

    char sequential = 0;
    assert(handleRead(fd, &sequential, 1) == 1);
    assert(sequential == bytes[0]);
    assert(pthread_join(thread, NULL) == 0);
    assert(positional.failed == 0 && positional.value == bytes[50]);
    assert(IO_Lseek(fd, 0, SEEK_CUR) == 1);

    positional = (struct positional_read_test){fd, 80, 0, 0, 0};
    assert(pthread_mutex_lock(&cursor_state_mutex) == 0);
    cursor_waiters = 0;
    positional_ready = 0;
    assert(pthread_mutex_unlock(&cursor_state_mutex) == 0);
    assert(pthread_create(&thread, NULL, positional_read_worker, &positional) == 0);
    assert(pthread_mutex_lock(&cursor_state_mutex) == 0);
    while (!positional_ready)
        assert(pthread_cond_wait(&cursor_state_changed, &cursor_state_mutex) == 0);
    assert(pthread_mutex_unlock(&cursor_state_mutex) == 0);
    assert(IO_Lseek(fd, 0, SEEK_CUR) == 1);
    assert(pthread_join(thread, NULL) == 0);
    assert(positional.failed == 0 && positional.value == bytes[80]);

    assert(close(fd) == 0);
    assert(unlink(path) == 0);
}

int main(void) {
    assert(getProcessHandle() == &process_handle_token);
    char error_text[128];
    assert(getErrorString(ENOENT, error_text, sizeof(error_text)) == 0);
    assert(error_text[0] != '\0');

    char path[] = "/tmp/javaswitch-io-XXXXXX";
    int seed = mkstemp(path);
    assert(seed >= 0);
    assert(close(seed) == 0);

    FD fd = handleOpen(path, O_RDWR | O_TRUNC, 0600);
    assert(fd >= 0);
    const char initial[] = "base";
    assert(handleWrite(fd, initial, 4) == 4);
    assert(lseek(fd, 0, SEEK_SET) == 0);

    jlong available = -1;
    assert(handleAvailable(fd, &available) == 1 && available == 4);
    char readback[5] = {0};
    assert(handleRead(fd, readback, 2) == 2);
    assert(memcmp(readback, "ba", 2) == 0);
    assert(handleAvailable(fd, &available) == 1 && available == 2);
    assert(handleGetLength(fd) == 4);
    assert(handleIsRegularFile(NULL, fd) == JNI_TRUE);
    assert(IO_Sync(fd) == 0);

    FD append_fd = handleOpen(path, O_WRONLY | O_APPEND, 0600);
    assert(append_fd >= 0);
    assert(lseek(append_fd, 0, SEEK_SET) == 0);
    assert(handleWrite(append_fd, "!", 1) == 1);
    assert(close(append_fd) == 0);
    assert(handleAvailable(fd, &available) == 1 && available == 3);

    assert(handleSetLength(fd, 3) == 0);
    assert(handleGetLength(fd) == 3);
    assert(close(fd) == 0);

    errno = 0;
    assert(handleOpen("/tmp", O_RDONLY, 0) == -1 && errno == EISDIR);

    int pipe_fds[2];
    assert(pipe(pipe_fds) == 0);
    assert(handleWrite(pipe_fds[1], "pipe", 4) == 4);
    char pipe_readback[4];
    assert(handleRead(pipe_fds[0], pipe_readback, sizeof(pipe_readback)) == 4);
    assert(memcmp(pipe_readback, "pipe", 4) == 0);
    errno = 0;
    assert(handleAvailable(pipe_fds[0], &available) == 0 && errno == ESPIPE);
    assert(close(pipe_fds[0]) == 0);
    assert(close(pipe_fds[1]) == 0);

    assert(unlink(path) == 0);
    test_shared_cursor_lock();
    return 0;
}
