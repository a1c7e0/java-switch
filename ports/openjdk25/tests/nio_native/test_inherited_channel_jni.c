/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */

#include <assert.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "jni.h"
#include "jni_util.h"
#include "net_util.h"
#include "sun_nio_ch_InheritedChannel.h"

static const char *exception_name;
static int io_exception_errno;
static int conversions;
static int init_calls;
static int cursor_locks;
static int cursor_unlocks;

JNIEXPORT void JNICALL JNU_ThrowByName(JNIEnv *env, const char *name,
                                       const char *message) {
    (void)env;
    (void)message;
    exception_name = name;
}

JNIEXPORT void JNICALL JNU_ThrowIOExceptionWithLastError(JNIEnv *env,
                                                          const char *message) {
    (void)env;
    (void)message;
    exception_name = "java/io/IOException";
    io_exception_errno = errno;
}

JNIEXPORT void JNICALL JNU_ThrowInternalError(JNIEnv *env, const char *message) {
    (void)env;
    (void)message;
    exception_name = "java/lang/InternalError";
}

JNIEXPORT const char *JNICALL
JNU_GetStringPlatformChars(JNIEnv *env, jstring string, jboolean *is_copy) {
    (void)env;
    (void)is_copy;
    return (const char *)string;
}

JNIEXPORT void JNICALL
JNU_ReleaseStringPlatformChars(JNIEnv *env, jstring string, const char *chars) {
    (void)env;
    (void)string;
    (void)chars;
}

int js_horizon_file_cursor_lock(void) {
    cursor_locks++;
    return 0;
}

void js_horizon_file_cursor_unlock(void) {
    cursor_unlocks++;
    errno = EIO; /* Verify close0 restores the close operation's errno. */
}

void initInetAddressIDs(JNIEnv *env) {
    (void)env;
    init_calls++;
}

jobject NET_SockaddrToInetAddress(JNIEnv *env, SOCKETADDRESS *address,
                                  int *port) {
    (void)env;
    conversions++;
    if (address->sa.sa_family == AF_INET) {
        *port = ntohs(address->sa4.sin_port);
    } else if (address->sa.sa_family == AF_INET6) {
        *port = ntohs(address->sa6.sin6_port);
    } else {
        assert(0 && "only internet socket addresses are converted");
    }
    return (jobject)(uintptr_t)1;
}

static int make_tcp_pair(int *client_fd, int *server_fd) {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    assert(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
    assert(listen(listener, 1) == 0);
    socklen_t length = sizeof(address);
    assert(getsockname(listener, (struct sockaddr *)&address, &length) == 0);

    *client_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(*client_fd >= 0);
    assert(connect(*client_fd, (struct sockaddr *)&address, sizeof(address)) == 0);
    *server_fd = accept(listener, NULL, NULL);
    assert(*server_fd >= 0);
    close(listener);
    return ntohs(address.sin_port);
}

int main(void) {
    static const struct JNINativeInterface_ functions = {0};
    JNIEnv env = &functions;
    JNIEnv *envp = &env;
    int client;
    int server;
    char temp_path[] = "/tmp/horizon-inherited-channel.XXXXXX";
    int temp_fd = mkstemp(temp_path);
    assert(temp_fd >= 0);
    assert(close(temp_fd) == 0);
    int opened_fd = Java_sun_nio_ch_InheritedChannel_open0(
            envp, NULL, (jstring)temp_path, sun_nio_ch_InheritedChannel_O_RDWR);
    assert(opened_fd >= 0);
    assert(write(opened_fd, "test", 4) == 4);
    exception_name = NULL;
    Java_sun_nio_ch_InheritedChannel_close0(envp, NULL, opened_fd);
    assert(exception_name == NULL);
    assert(cursor_locks == 1 && cursor_unlocks == 1);
    assert(fcntl(opened_fd, F_GETFD) == -1 && errno == EBADF);

    exception_name = NULL;
    assert(Java_sun_nio_ch_InheritedChannel_open0(
                   envp, NULL,
                   (jstring)"/tmp/horizon-inherited-channel-missing/file",
                   sun_nio_ch_InheritedChannel_O_RDONLY) == -1);
    assert(strcmp(exception_name, "java/io/IOException") == 0);
    assert(io_exception_errno == ENOENT);
    exception_name = NULL;
    Java_sun_nio_ch_InheritedChannel_close0(envp, NULL, -1);
    assert(strcmp(exception_name, "java/io/IOException") == 0);
    assert(io_exception_errno == EBADF);
    assert(cursor_locks == 2 && cursor_unlocks == 2);
    unlink(temp_path);

    int listener_port = make_tcp_pair(&client, &server);

    Java_sun_nio_ch_InheritedChannel_initIDs(envp, NULL);
    assert(init_calls == 1);
    assert(Java_sun_nio_ch_InheritedChannel_soType0(envp, NULL, client) ==
           sun_nio_ch_InheritedChannel_SOCK_STREAM);
    assert(Java_sun_nio_ch_InheritedChannel_addressFamily(envp, NULL, client) ==
           sun_nio_ch_InheritedChannel_AF_INET);
    assert(Java_sun_nio_ch_InheritedChannel_isConnected(envp, NULL, client) ==
           JNI_TRUE);
    assert(Java_sun_nio_ch_InheritedChannel_inetPeerAddress0(envp, NULL, client)
           == (jobject)(uintptr_t)1);
    assert(conversions == 1);
    assert(Java_sun_nio_ch_InheritedChannel_peerPort0(envp, NULL, client) ==
           listener_port);

    int datagram = socket(AF_INET, SOCK_DGRAM, 0);
    assert(datagram >= 0);
    assert(Java_sun_nio_ch_InheritedChannel_soType0(envp, NULL, datagram) ==
           sun_nio_ch_InheritedChannel_SOCK_DGRAM);
    assert(Java_sun_nio_ch_InheritedChannel_addressFamily(envp, NULL, datagram) ==
           sun_nio_ch_InheritedChannel_AF_INET);
    assert(Java_sun_nio_ch_InheritedChannel_isConnected(envp, NULL, datagram) ==
           JNI_FALSE);

    int unix_pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, unix_pair) == 0);
    assert(Java_sun_nio_ch_InheritedChannel_addressFamily(envp, NULL,
                                                          unix_pair[0]) ==
           sun_nio_ch_InheritedChannel_AF_UNKNOWN);
    assert(Java_sun_nio_ch_InheritedChannel_soType0(envp, NULL, -1) ==
           sun_nio_ch_InheritedChannel_UNKNOWN);
    assert(Java_sun_nio_ch_InheritedChannel_addressFamily(envp, NULL, -1) ==
           sun_nio_ch_InheritedChannel_AF_UNKNOWN);

    assert(Java_sun_nio_ch_InheritedChannel_dup(envp, NULL, client) == -1);
    assert(strcmp(exception_name, "java/lang/UnsupportedOperationException") == 0);
    assert(errno == ENOTSUP);
    exception_name = NULL;
    Java_sun_nio_ch_InheritedChannel_dup2(envp, NULL, client, server);
    assert(strcmp(exception_name, "java/lang/UnsupportedOperationException") == 0);
    assert(errno == ENOTSUP);
    exception_name = NULL;
    assert(Java_sun_nio_ch_InheritedChannel_unixPeerAddress0(envp, NULL,
                                                            unix_pair[0]) == NULL);
    assert(strcmp(exception_name, "java/lang/UnsupportedOperationException") == 0);
    assert(errno == ENOTSUP);

    close(client);
    close(server);
    close(datagram);
    close(unix_pair[0]);
    close(unix_pair[1]);
    puts("Horizon inherited channel JNI host test: PASS");
    return 0;
}
