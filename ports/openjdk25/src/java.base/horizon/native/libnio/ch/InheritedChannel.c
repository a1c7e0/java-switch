/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */

#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "jni.h"
#include "jni_util.h"
#include "net_util.h"
#include "sun_nio_ch_InheritedChannel.h"
#include "horizon_file_cursor.h"

/* Horizon does not provide descriptor duplication for inherited channels. */
static void throw_unsupported(JNIEnv *env, const char *operation) {
    errno = ENOTSUP;
    JNU_ThrowByName(env, "java/lang/UnsupportedOperationException", operation);
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_InheritedChannel_initIDs(JNIEnv *env, jclass clazz) {
    (void)clazz;
    initInetAddressIDs(env);
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_InheritedChannel_soType0(JNIEnv *env, jclass clazz, jint fd) {
    (void)env;
    (void)clazz;
    int socket_type;
    socklen_t length = sizeof(socket_type);

    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &socket_type, &length) != 0) {
        return sun_nio_ch_InheritedChannel_UNKNOWN;
    }
    if (socket_type == SOCK_STREAM) {
        return sun_nio_ch_InheritedChannel_SOCK_STREAM;
    }
    if (socket_type == SOCK_DGRAM) {
        return sun_nio_ch_InheritedChannel_SOCK_DGRAM;
    }
    return sun_nio_ch_InheritedChannel_UNKNOWN;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_InheritedChannel_addressFamily(JNIEnv *env, jclass clazz,
                                               jint fd) {
    (void)env;
    (void)clazz;
    SOCKETADDRESS address;
    socklen_t length = sizeof(address);

    memset(&address, 0, sizeof(address));
    if (getsockname(fd, &address.sa, &length) != 0) {
        return sun_nio_ch_InheritedChannel_AF_UNKNOWN;
    }
    if (address.sa.sa_family == AF_INET) {
        return sun_nio_ch_InheritedChannel_AF_INET;
    }
    if (address.sa.sa_family == AF_INET6) {
        return sun_nio_ch_InheritedChannel_AF_INET6;
    }
    /* Unix sockets and unknown families are not implemented on Horizon. */
    return sun_nio_ch_InheritedChannel_AF_UNKNOWN;
}

JNIEXPORT jobject JNICALL
Java_sun_nio_ch_InheritedChannel_inetPeerAddress0(JNIEnv *env, jclass clazz,
                                                  jint fd) {
    (void)clazz;
    SOCKETADDRESS address;
    socklen_t length = sizeof(address);
    int port = -1;

    memset(&address, 0, sizeof(address));
    if (getpeername(fd, &address.sa, &length) != 0 ||
        (address.sa.sa_family != AF_INET && address.sa.sa_family != AF_INET6)) {
        return NULL;
    }
    return NET_SockaddrToInetAddress(env, &address, &port);
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_InheritedChannel_peerPort0(JNIEnv *env, jclass clazz,
                                           jint fd) {
    (void)env;
    (void)clazz;
    SOCKETADDRESS address;
    socklen_t length = sizeof(address);
    int port = -1;

    memset(&address, 0, sizeof(address));
    if (getpeername(fd, &address.sa, &length) != 0 ||
        (address.sa.sa_family != AF_INET && address.sa.sa_family != AF_INET6)) {
        return -1;
    }
    if (address.sa.sa_family == AF_INET) {
        port = ntohs(address.sa4.sin_port);
    } else {
        port = ntohs(address.sa6.sin6_port);
    }
    return port;
}

JNIEXPORT jboolean JNICALL
Java_sun_nio_ch_InheritedChannel_isConnected(JNIEnv *env, jclass clazz,
                                              jint fd) {
    (void)env;
    (void)clazz;
    SOCKETADDRESS address;
    socklen_t length = sizeof(address);

    memset(&address, 0, sizeof(address));
    return getpeername(fd, &address.sa, &length) == 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jbyteArray JNICALL
Java_sun_nio_ch_InheritedChannel_unixPeerAddress0(JNIEnv *env, jclass clazz,
                                                  jint fd) {
    (void)clazz;
    (void)fd;
    throw_unsupported(env, "Unix-domain inherited channels are unavailable on Horizon");
    return NULL;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_InheritedChannel_dup(JNIEnv *env, jclass clazz, jint fd) {
    (void)clazz;
    (void)fd;
    throw_unsupported(env, "Inherited file descriptor duplication is unavailable on Horizon");
    return -1;
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_InheritedChannel_dup2(JNIEnv *env, jclass clazz, jint fd,
                                      jint fd2) {
    (void)clazz;
    (void)fd;
    (void)fd2;
    throw_unsupported(env, "Inherited file descriptor duplication is unavailable on Horizon");
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_InheritedChannel_open0(JNIEnv *env, jclass clazz,
                                       jstring path, jint flags) {
    (void)clazz;
    int open_flags;
    switch (flags) {
        case sun_nio_ch_InheritedChannel_O_RDONLY:
            open_flags = O_RDONLY;
            break;
        case sun_nio_ch_InheritedChannel_O_WRONLY:
            open_flags = O_WRONLY;
            break;
        case sun_nio_ch_InheritedChannel_O_RDWR:
            open_flags = O_RDWR;
            break;
        default:
            JNU_ThrowInternalError(env, "Unrecognized file mode");
            return -1;
    }

    const char *native_path = JNU_GetStringPlatformChars(env, path, NULL);
    if (native_path == NULL) {
        return -1;
    }
    int fd = open(native_path, open_flags);
    int error = errno;
    JNU_ReleaseStringPlatformChars(env, path, native_path);
    if (fd < 0) {
        errno = error;
        JNU_ThrowIOExceptionWithLastError(env, "open failed");
    }
    return fd;
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_InheritedChannel_close0(JNIEnv *env, jclass clazz, jint fd) {
    (void)clazz;
    if (js_horizon_file_cursor_lock() < 0) {
        JNU_ThrowIOExceptionWithLastError(env, "File cursor lock failed");
        return;
    }
    int result = close(fd);
    int error = errno;
    js_horizon_file_cursor_unlock();
    errno = error;
    if (result < 0) {
        JNU_ThrowIOExceptionWithLastError(env, "close failed");
    }
}
