/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#ifdef __SWITCH__
#include <sys/syslimits.h>
#else
#ifndef IOV_MAX
#define IOV_MAX 1024 /* host harness mirrors the devkitA64 syslimits.h value */
#endif
#endif
#include <sys/types.h>
#include <unistd.h>

#include "jni.h"
#include "jni_util.h"
#include "jlong.h"
#include "nio.h"
#include "nio_util.h"
#include "sun_nio_ch_SocketDispatcher.h"

#define HORIZON_SSIZE_MAX (SIZE_MAX >> 1)

static int validate_iov(JNIEnv *env, struct iovec *iov, jint count,
                        size_t *total_out) {
    if (count < 0 || count > IOV_MAX || (count != 0 && iov == NULL)) {
        errno = EINVAL;
        JNU_ThrowIOExceptionWithLastError(env, "Invalid socket vector count");
        return -1;
    }
    size_t total = 0;
    for (jint i = 0; i < count; i++) {
        if (iov[i].iov_len != 0 && iov[i].iov_base == NULL) {
            errno = EFAULT;
            JNU_ThrowIOExceptionWithLastError(env, "Null socket vector element");
            return -1;
        }
        if (iov[i].iov_len > HORIZON_SSIZE_MAX - total) {
            errno = EOVERFLOW;
            JNU_ThrowIOExceptionWithLastError(env, "Socket vector length overflow");
            return -1;
        }
        total += iov[i].iov_len;
    }
    *total_out = total;
    return 0;
}

static int throw_connection_reset(JNIEnv *env, ssize_t result) {
    if (result < 0 && (errno == ECONNRESET || errno == EPIPE)) {
        JNU_ThrowByName(env, "sun/net/ConnectionResetException", "Connection reset");
        return 1;
    }
    return 0;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_SocketDispatcher_read0(JNIEnv *env, jclass clazz,
                                       jobject descriptor, jlong address,
                                       jint length) {
    (void)clazz;
    if (length < 0) {
        errno = EINVAL;
        JNU_ThrowIOExceptionWithLastError(env, "Invalid socket read length");
        return IOS_THROWN;
    }
    if (length == 0) return 0;
    int fd = fdval(env, descriptor);
    ssize_t result = recv(fd, jlong_to_ptr(address), (size_t)length, 0);
    if (throw_connection_reset(env, result)) return IOS_THROWN;
    return convertReturnVal(env, (jint)result, JNI_TRUE);
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_SocketDispatcher_readv0(JNIEnv *env, jclass clazz,
                                        jobject descriptor, jlong address,
                                        jint count) {
    (void)clazz;
    struct iovec *iov = jlong_to_ptr(address);
    size_t total;
    if (validate_iov(env, iov, count, &total) < 0) return IOS_THROWN;
    if (total == 0) return 0;
    struct msghdr message = {0};
    message.msg_iov = iov;
    message.msg_iovlen = count;
    int fd = fdval(env, descriptor);
    ssize_t result = recvmsg(fd, &message, 0);
    if (throw_connection_reset(env, result)) return IOS_THROWN;
    return convertLongReturnVal(env, (jlong)result, JNI_TRUE);
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_SocketDispatcher_write0(JNIEnv *env, jclass clazz,
                                        jobject descriptor, jlong address,
                                        jint length) {
    (void)clazz;
    if (length < 0) {
        errno = EINVAL;
        JNU_ThrowIOExceptionWithLastError(env, "Invalid socket write length");
        return IOS_THROWN;
    }
    if (length == 0) return 0;
    int fd = fdval(env, descriptor);
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    ssize_t result = send(fd, jlong_to_ptr(address), (size_t)length, flags);
    if (throw_connection_reset(env, result)) return IOS_THROWN;
    return convertReturnVal(env, (jint)result, JNI_FALSE);
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_SocketDispatcher_writev0(JNIEnv *env, jclass clazz,
                                         jobject descriptor, jlong address,
                                         jint count) {
    (void)clazz;
    struct iovec *iov = jlong_to_ptr(address);
    size_t total;
    if (validate_iov(env, iov, count, &total) < 0) return IOS_THROWN;
    if (total == 0) return 0;
    struct msghdr message = {0};
    message.msg_iov = iov;
    message.msg_iovlen = count;
    int fd = fdval(env, descriptor);
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    ssize_t result = sendmsg(fd, &message, flags);
    if (throw_connection_reset(env, result)) return IOS_THROWN;
    return convertLongReturnVal(env, (jlong)result, JNI_FALSE);
}
