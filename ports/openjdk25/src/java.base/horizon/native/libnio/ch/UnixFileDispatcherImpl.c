/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "jni.h"
#include "jni_util.h"
#include "jlong.h"
#include "nio.h"
#include "sun_nio_ch_UnixFileDispatcherImpl.h"
#include "horizon_file_io.h"
#include "horizon_file_cursor.h"

jint fdval(JNIEnv *env, jobject descriptor);
jint convertReturnVal(JNIEnv *env, jint result, jboolean reading);
jlong convertLongReturnVal(JNIEnv *env, jlong result, jboolean reading);

static void throw_unsupported(JNIEnv *env, const char *message) {
    JNU_ThrowByName(env, "java/lang/UnsupportedOperationException", message);
}

static jlong throw_io_error(JNIEnv *env, const char *message) {
    JNU_ThrowIOExceptionWithLastError(env, message);
    return IOS_THROWN;
}

static int lock_cursor(JNIEnv *env) {
    if (js_horizon_file_cursor_lock() < 0) {
        if (errno == ENOMEM) {
            JNU_ThrowOutOfMemoryError(env, "File cursor monitor allocation failed");
        } else {
            JNU_ThrowIOExceptionWithLastError(env, "File cursor lock failed");
        }
        return -1;
    }
    return 0;
}

static void unlock_cursor(void) {
    js_horizon_file_cursor_unlock();
}

static int checked_fd(JNIEnv *env, jobject descriptor) {
    jint fd = fdval(env, descriptor);
    if (fd < 0 && !(*env)->ExceptionCheck(env)) {
        JNU_ThrowIOException(env, "Invalid file descriptor");
        return -1;
    }
    return fd;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_read0(JNIEnv *env, jclass cls,
                                              jobject descriptor, jlong address,
                                              jint length) {
    (void)cls;
    int fd = checked_fd(env, descriptor);
    if (fd < 0 || lock_cursor(env) < 0) return IOS_THROWN;
    if (length < 0) {
        errno = EINVAL;
        jint result = (jint)throw_io_error(env, "Invalid read length");
        unlock_cursor();
        return result;
    }
    jint result = convertReturnVal(env, read(fd, jlong_to_ptr(address), length), JNI_TRUE);
    unlock_cursor();
    return result;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_pread0(JNIEnv *env, jclass cls,
                                               jobject descriptor, jlong address,
                                               jint length, jlong position) {
    (void)cls;
    int fd = checked_fd(env, descriptor);
    if (fd < 0) return IOS_THROWN;
    off_t offset = (off_t)position;
    if ((jlong)offset != position || position < 0 || length < 0) {
        errno = EINVAL;
        return (jint)throw_io_error(env, "Invalid positional read offset");
    }
    if (lock_cursor(env) < 0) return IOS_THROWN;
    ssize_t count;
    int status = horizon_positional_io(fd, jlong_to_ptr(address),
                                       (size_t)length, offset, 0, &count);
    jint result;
    if (status < 0) {
        result = (jint)throw_io_error(env, "Positional read failed");
    } else {
        result = convertReturnVal(env, (jint)count, JNI_TRUE);
    }
    unlock_cursor();
    return result;
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_readv0(JNIEnv *env, jclass cls,
                                               jobject descriptor, jlong address,
                                               jint count) {
    (void)cls;
    int fd = checked_fd(env, descriptor);
    if (fd < 0 || lock_cursor(env) < 0) return IOS_THROWN;
    ssize_t bytes = 0;
    int status = horizon_vector_io(fd, jlong_to_ptr(address), count, 0, &bytes);
    jlong result;
    if (status < 0 && errno == ENOMEM) {
        JNU_ThrowOutOfMemoryError(env, "Temporary buffer for vector read");
        result = IOS_THROWN;
    } else if (status < 0 && (errno == EINVAL || errno == EOVERFLOW || errno == EFAULT)) {
        result = throw_io_error(env, "Invalid vector read request");
    } else {
        result = convertLongReturnVal(env, (jlong)bytes, JNI_TRUE);
    }
    unlock_cursor();
    return result;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_write0(JNIEnv *env, jclass cls,
                                               jobject descriptor, jlong address,
                                               jint length) {
    (void)cls;
    int fd = checked_fd(env, descriptor);
    if (fd < 0 || lock_cursor(env) < 0) return IOS_THROWN;
    if (length < 0) {
        errno = EINVAL;
        jint result = (jint)throw_io_error(env, "Invalid write length");
        unlock_cursor();
        return result;
    }
    jint result = convertReturnVal(env, write(fd, jlong_to_ptr(address), length), JNI_FALSE);
    unlock_cursor();
    return result;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_pwrite0(JNIEnv *env, jclass cls,
                                                jobject descriptor, jlong address,
                                                jint length, jlong position) {
    (void)cls;
    int fd = checked_fd(env, descriptor);
    if (fd < 0) return IOS_THROWN;
    off_t offset = (off_t)position;
    if ((jlong)offset != position || position < 0 || length < 0) {
        errno = EINVAL;
        return (jint)throw_io_error(env, "Invalid positional write offset");
    }
    if (lock_cursor(env) < 0) return IOS_THROWN;
    ssize_t count;
    int status = horizon_positional_io(fd, jlong_to_ptr(address),
                                       (size_t)length, offset, 1, &count);
    jint result;
    if (status < 0) {
        result = (jint)throw_io_error(env, "Positional write failed");
    } else {
        result = convertReturnVal(env, (jint)count, JNI_FALSE);
    }
    unlock_cursor();
    return result;
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_writev0(JNIEnv *env, jclass cls,
                                                jobject descriptor, jlong address,
                                                jint count) {
    (void)cls;
    int fd = checked_fd(env, descriptor);
    if (fd < 0 || lock_cursor(env) < 0) return IOS_THROWN;
    ssize_t bytes = 0;
    int status = horizon_vector_io(fd, jlong_to_ptr(address), count, 1, &bytes);
    jlong result;
    if (status < 0 && errno == ENOMEM) {
        JNU_ThrowOutOfMemoryError(env, "Temporary buffer for vector write");
        result = IOS_THROWN;
    } else if (status < 0 && (errno == EINVAL || errno == EOVERFLOW || errno == EFAULT)) {
        result = throw_io_error(env, "Invalid vector write request");
    } else {
        result = convertLongReturnVal(env, (jlong)bytes, JNI_FALSE);
    }
    unlock_cursor();
    return result;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_force0(JNIEnv *env, jclass cls,
                                               jobject descriptor,
                                               jboolean metadata) {
    (void)cls; (void)metadata;
    int fd = checked_fd(env, descriptor);
    if (fd < 0) return IOS_THROWN;
    int result;
    do { result = fsync(fd); } while (result < 0 && errno == EINTR);
    return result < 0 ? (jint)throw_io_error(env, "File sync failed") : 0;
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_seek0(JNIEnv *env, jclass cls,
                                              jobject descriptor, jlong offset) {
    (void)cls;
    int fd = checked_fd(env, descriptor);
    if (fd < 0 || lock_cursor(env) < 0) return IOS_THROWN;
    off_t position = offset < 0 ? lseek(fd, (off_t)0, SEEK_CUR)
                                : lseek(fd, (off_t)offset, SEEK_SET);
    if (position == (off_t)-1) {
        jlong result = throw_io_error(env, "File seek failed");
        unlock_cursor();
        return result;
    }
    unlock_cursor();
    return (jlong)position;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_truncate0(JNIEnv *env, jclass cls,
                                                  jobject descriptor,
                                                  jlong size) {
    (void)cls;
    int fd = checked_fd(env, descriptor);
    if (fd < 0) return IOS_THROWN;
    if (size < 0 || (jlong)(off_t)size != size) {
        errno = EINVAL;
        return (jint)throw_io_error(env, "Invalid file size");
    }
    int result;
    do { result = ftruncate(fd, (off_t)size); }
    while (result < 0 && errno == EINTR);
    return result < 0 ? (jint)throw_io_error(env, "File truncate failed") : 0;
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_size0(JNIEnv *env, jclass cls,
                                              jobject descriptor) {
    (void)cls;
    int fd = checked_fd(env, descriptor);
    if (fd < 0) return IOS_THROWN;
    struct stat st;
    if (fstat(fd, &st) < 0) return throw_io_error(env, "File size query failed");
    return (jlong)st.st_size;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_available0(JNIEnv *env, jclass cls,
                                                   jobject descriptor) {
    (void)cls;
    int fd = checked_fd(env, descriptor);
    if (fd < 0 || lock_cursor(env) < 0) return IOS_THROWN;
    struct stat st;
    off_t current = lseek(fd, (off_t)0, SEEK_CUR);
    if (current == (off_t)-1 || fstat(fd, &st) < 0) {
        unlock_cursor();
        return 0;
    }
    off_t end = st.st_size;
    if (current > end) {
        end = lseek(fd, (off_t)0, SEEK_END);
        if (end == (off_t)-1 || lseek(fd, current, SEEK_SET) == (off_t)-1) {
            unlock_cursor();
            return 0;
        }
    }
    off_t available = end - current;
    unlock_cursor();
    return available > INT_MAX ? INT_MAX : (jint)available;
}

JNIEXPORT jboolean JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_isOther0(JNIEnv *env, jclass cls,
                                                 jobject descriptor) {
    (void)cls;
    int fd = checked_fd(env, descriptor);
    if (fd < 0) return JNI_FALSE;
    struct stat st;
    if (fstat(fd, &st) < 0) {
        (void)throw_io_error(env, "File type query failed");
        return JNI_FALSE;
    }
    return S_ISREG(st.st_mode) || S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)
            ? JNI_FALSE : JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_lock0(JNIEnv *env, jclass cls,
                                              jobject descriptor,
                                              jboolean block, jlong position,
                                              jlong size, jboolean shared) {
    (void)cls; (void)descriptor; (void)block; (void)position; (void)size; (void)shared;
    errno = ENOTSUP;
    (void)throw_io_error(env, "File locking is not supported on Horizon");
    return IOS_THROWN;
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_release0(JNIEnv *env, jclass cls,
                                                 jobject descriptor,
                                                 jlong position, jlong size) {
    (void)cls; (void)descriptor; (void)position; (void)size;
    errno = ENOTSUP;
    JNU_ThrowIOExceptionWithLastError(env, "File locking is not supported on Horizon");
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_closeIntFD(JNIEnv *env, jclass cls,
                                                   jint fd) {
    (void)cls;
    if (fd == -1 || lock_cursor(env) < 0) return;
    int result = close(fd);
    int error = errno;
    unlock_cursor();
    if (result < 0 && error != EINTR) {
        errno = error;
        JNU_ThrowIOExceptionWithLastError(env, "Descriptor close failed");
    }
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_allocationGranularity0(JNIEnv *env,
                                                               jclass cls) {
    (void)cls;
    throw_unsupported(env, "File mapping is not supported on Horizon");
    return IOS_THROWN;
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_map0(JNIEnv *env, jclass cls,
                                             jobject descriptor, jint prot,
                                             jlong offset, jlong length,
                                             jboolean map_sync) {
    (void)cls; (void)descriptor; (void)prot; (void)offset; (void)length; (void)map_sync;
    throw_unsupported(env, "File mapping is not supported on Horizon");
    return IOS_THROWN;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_unmap0(JNIEnv *env, jclass cls,
                                               jlong address, jlong length) {
    (void)cls; (void)address; (void)length;
    throw_unsupported(env, "File mapping is not supported on Horizon");
    return IOS_THROWN;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_UnixFileDispatcherImpl_setDirect0(JNIEnv *env, jclass cls,
                                                   jobject descriptor) {
    (void)cls; (void)descriptor;
    throw_unsupported(env, "Direct I/O is not supported on Horizon");
    return -1;
}
