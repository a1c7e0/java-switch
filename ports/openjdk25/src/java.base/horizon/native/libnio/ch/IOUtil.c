/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include "jni.h"
#include "jni_util.h"
#include "jvm.h"
#include "jlong.h"
#include "sun_nio_ch_IOUtil.h"
#include "java_lang_Integer.h"
#include "java_lang_Long.h"
#include "nio.h"
#include "nio_util.h"

static jfieldID fd_fdID;

/* Implemented by Horizon's libnet service initializer. */
extern int NET_PlatformInit(void);

JNIEXPORT void JNICALL
Java_sun_nio_ch_IOUtil_initIDs(JNIEnv *env, jclass clazz) {
    clazz = (*env)->FindClass(env, "java/io/FileDescriptor");
    if (clazz == NULL) return;
    fd_fdID = (*env)->GetFieldID(env, clazz, "fd", "I");
}

JNIEXPORT jboolean JNICALL
Java_sun_nio_ch_IOUtil_randomBytes(JNIEnv *env, jclass clazz,
                                  jbyteArray bytes) {
    (void)clazz;
    (void)bytes;
    JNU_ThrowByName(env, "java/lang/UnsupportedOperationException",
                    "Native random byte generation is unavailable on Horizon");
    return JNI_FALSE;
}

jint fdval(JNIEnv *env, jobject descriptor) {
    return (*env)->GetIntField(env, descriptor, fd_fdID);
}

void setfdval(JNIEnv *env, jobject descriptor, jint value) {
    (*env)->SetIntField(env, descriptor, fd_fdID, value);
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_IOUtil_fdVal(JNIEnv *env, jclass clazz, jobject descriptor) {
    (void)clazz;
    return fdval(env, descriptor);
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_IOUtil_setfdVal(JNIEnv *env, jclass clazz,
                                jobject descriptor, jint value) {
    (void)clazz;
    setfdval(env, descriptor, value);
}

jint convertReturnVal(JNIEnv *env, jint result, jboolean reading) {
    if (result > 0) return result;
    if (result == 0) return reading ? IOS_EOF : 0;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return IOS_UNAVAILABLE;
    if (errno == EINTR) return IOS_INTERRUPTED;
    JNU_ThrowIOExceptionWithLastError(env, reading ? "Read failed" : "Write failed");
    return IOS_THROWN;
}

jlong convertLongReturnVal(JNIEnv *env, jlong result, jboolean reading) {
    if (result > 0) return result;
    if (result == 0) return reading ? IOS_EOF : 0;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return IOS_UNAVAILABLE;
    if (errno == EINTR) return IOS_INTERRUPTED;
    JNU_ThrowIOExceptionWithLastError(env, reading ? "Read failed" : "Write failed");
    return IOS_THROWN;
}

static int configure_blocking(int fd, jboolean blocking) {
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) return -1;
    int new_flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    if (flags == new_flags) return 0;
    return fcntl(fd, F_SETFL, new_flags);
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_IOUtil_configureBlocking(JNIEnv *env, jclass clazz,
                                         jobject descriptor,
                                         jboolean blocking) {
    (void)clazz;
    int fd = fdval(env, descriptor);
    if (!(*env)->ExceptionCheck(env) && configure_blocking(fd, blocking) < 0)
        JNU_ThrowIOExceptionWithLastError(env, "Configure blocking failed");
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_IOUtil_makePipe(JNIEnv *env, jobject object,
                                jboolean blocking) {
    (void)object;
    int listener = -1;
    int writer = -1;
    int reader = -1;
    int saved_errno;
    struct sockaddr_in address;
    struct sockaddr_in writer_address;
    struct sockaddr_in peer_address;
    socklen_t address_length;

    if (NET_PlatformInit() != 0) {
        if (errno == 0) errno = EIO;
        JNU_ThrowIOExceptionWithLastError(env, "Initialize selector wakeup sockets failed");
        return 0;
    }

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) goto fail;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
#ifdef __SWITCH__
    address.sin_len = sizeof(address);
#endif
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0)
        goto fail;
    if (listen(listener, 1) < 0) goto fail;
    address_length = sizeof(address);
    if (getsockname(listener, (struct sockaddr *)&address, &address_length) < 0)
        goto fail;

    writer = socket(AF_INET, SOCK_STREAM, 0);
    if (writer < 0) goto fail;
    if (connect(writer, (struct sockaddr *)&address, sizeof(address)) < 0)
        goto fail;
    address_length = sizeof(writer_address);
    if (getsockname(writer, (struct sockaddr *)&writer_address,
                    &address_length) < 0)
        goto fail;

    /* This listener is private to the pair. One accept is sufficient; reject
     * an unexpected peer rather than retrying indefinitely. */
    do {
        reader = accept(listener, (struct sockaddr *)&peer_address,
                        &(socklen_t){ sizeof(peer_address) });
    } while (reader < 0 && errno == EINTR);
    if (reader < 0) goto fail;
    if (peer_address.sin_family != AF_INET ||
        peer_address.sin_addr.s_addr != writer_address.sin_addr.s_addr ||
        peer_address.sin_port != writer_address.sin_port) {
        errno = ECONNABORTED;
        goto fail;
    }

    {
        int enabled = 1;
        if (setsockopt(writer, IPPROTO_TCP, TCP_NODELAY, &enabled,
                       sizeof(enabled)) < 0 ||
            setsockopt(reader, IPPROTO_TCP, TCP_NODELAY, &enabled,
                       sizeof(enabled)) < 0)
            goto fail;
    }
    if (configure_blocking(reader, blocking) < 0 ||
        configure_blocking(writer, blocking) < 0)
        goto fail;

    if (close(listener) < 0) {
        listener = -1; /* close may already have released it */
        goto fail;
    }
    listener = -1;
    return ((jlong)(uint32_t)reader << 32) | (uint32_t)writer;

fail:
    saved_errno = errno;
    if (reader >= 0) (void)close(reader);
    if (writer >= 0) (void)close(writer);
    if (listener >= 0) (void)close(listener);
    errno = saved_errno;
    JNU_ThrowIOExceptionWithLastError(env, "Create selector wakeup sockets failed");
    return 0;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_IOUtil_write1(JNIEnv *env, jclass clazz, jint fd, jbyte byte) {
    (void)clazz;
    ssize_t result;
    do { result = write(fd, &byte, 1); } while (result < 0 && errno == EINTR);
    return convertReturnVal(env, (jint)result, JNI_FALSE);
}

JNIEXPORT jboolean JNICALL
Java_sun_nio_ch_IOUtil_drain(JNIEnv *env, jclass clazz, jint fd) {
    (void)clazz;
    char buffer[16];
    size_t total = 0;
    for (;;) {
        ssize_t count;
        do { count = read(fd, buffer, sizeof(buffer)); }
        while (count < 0 && errno == EINTR);
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return total == 0 ? JNI_FALSE : JNI_TRUE;
            JNU_ThrowIOExceptionWithLastError(env, "Drain failed");
            return JNI_FALSE;
        }
        if (count == 0) return total == 0 ? JNI_FALSE : JNI_TRUE;
        total += (size_t)count;
        if (count < (ssize_t)sizeof(buffer)) return JNI_TRUE;
    }
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_IOUtil_drain1(JNIEnv *env, jclass clazz, jint fd) {
    (void)clazz;
    char byte;
    ssize_t result = read(fd, &byte, 1);
    if (result >= 0) return (jint)result;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    if (errno == EINTR) return IOS_INTERRUPTED;
    JNU_ThrowIOExceptionWithLastError(env, "Drain failed");
    return IOS_THROWN;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_IOUtil_fdLimit(JNIEnv *env, jclass clazz) {
    (void)clazz;
    errno = ENOTSUP;
    JNU_ThrowIOExceptionWithLastError(env, "File descriptor limit query is unavailable on Horizon");
    return -1;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_IOUtil_iovMax(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    /* Matches horizon_vector_io's checked software cap. */
    return 1024;
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_IOUtil_writevMax(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    /* horizon_vector_io rejects aggregate lengths above INT_MAX. */
    return (jlong)java_lang_Integer_MAX_VALUE;
}
