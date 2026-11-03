/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "jni.h"
#include "jni_util.h"
#include "nio.h"
#include "sun_nio_ch_SocketDispatcher.h"
#include "sun_nio_ch_DatagramDispatcher.h"

static int exception_count;
static const char *exception_name;

jint fdval(JNIEnv *env, jobject descriptor) {
    (void)env;
    return (jint)(intptr_t)descriptor;
}

jint convertReturnVal(JNIEnv *env, jint result, jboolean reading) {
    (void)env;
    if (result > 0) return result;
    if (result == 0) return reading ? IOS_EOF : 0;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return IOS_UNAVAILABLE;
    if (errno == EINTR) return IOS_INTERRUPTED;
    return IOS_THROWN;
}

jlong convertLongReturnVal(JNIEnv *env, jlong result, jboolean reading) {
    (void)env;
    if (result > 0) return result;
    if (result == 0) return reading ? IOS_EOF : 0;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return IOS_UNAVAILABLE;
    if (errno == EINTR) return IOS_INTERRUPTED;
    return IOS_THROWN;
}

void JNU_ThrowByName(JNIEnv *env, const char *name, const char *message) {
    (void)env; (void)message;
    exception_count++;
    exception_name = name;
}

void JNU_ThrowIOExceptionWithLastError(JNIEnv *env, const char *message) {
    (void)env; (void)message;
    exception_count++;
    exception_name = "java/io/IOException";
}

static jobject descriptor(int fd) {
    return (jobject)(intptr_t)fd;
}

static void test_stream_dispatcher(void) {
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    const char initial[] = "abc";
    assert(Java_sun_nio_ch_SocketDispatcher_write0(NULL, NULL,
            descriptor(fds[0]), (jlong)(intptr_t)initial, 3) == 3);

    char first[2] = {0};
    char second[3] = {0};
    struct iovec vectors[] = {
        { .iov_base = first, .iov_len = 1 },
        { .iov_base = second, .iov_len = 2 },
    };
    assert(Java_sun_nio_ch_SocketDispatcher_readv0(NULL, NULL,
            descriptor(fds[1]), (jlong)(intptr_t)vectors, 2) == 3);
    assert(first[0] == 'a' && memcmp(second, "bc", 2) == 0);

    vectors[0].iov_base = (void *)"12";
    vectors[0].iov_len = 2;
    vectors[1].iov_base = (void *)"34";
    vectors[1].iov_len = 2;
    assert(Java_sun_nio_ch_SocketDispatcher_writev0(NULL, NULL,
            descriptor(fds[0]), (jlong)(intptr_t)vectors, 2) == 4);
    memset(first, 0, sizeof(first));
    memset(second, 0, sizeof(second));
    assert(Java_sun_nio_ch_SocketDispatcher_read0(NULL, NULL,
            descriptor(fds[1]), (jlong)(intptr_t)first, 2) == 2);
    assert(memcmp(first, "12", 2) == 0);
    assert(Java_sun_nio_ch_SocketDispatcher_read0(NULL, NULL,
            descriptor(fds[1]), (jlong)(intptr_t)second, 2) == 2);
    assert(memcmp(second, "34", 2) == 0);

    vectors[0].iov_len = 0;
    vectors[1].iov_len = 0;
    assert(Java_sun_nio_ch_SocketDispatcher_readv0(NULL, NULL,
            descriptor(fds[1]), (jlong)(intptr_t)vectors, 2) == 0);

    int flags = fcntl(fds[1], F_GETFL);
    assert(flags >= 0 && fcntl(fds[1], F_SETFL, flags | O_NONBLOCK) == 0);
    assert(Java_sun_nio_ch_SocketDispatcher_read0(NULL, NULL,
            descriptor(fds[1]), (jlong)(intptr_t)first, 1) == IOS_UNAVAILABLE);
    shutdown(fds[0], SHUT_WR);
    assert(Java_sun_nio_ch_SocketDispatcher_read0(NULL, NULL,
            descriptor(fds[1]), (jlong)(intptr_t)first, 1) == IOS_EOF);
    close(fds[1]);
    const char after_close = 'z';
    assert(Java_sun_nio_ch_SocketDispatcher_write0(NULL, NULL,
            descriptor(fds[0]), (jlong)(intptr_t)&after_close, 1) == IOS_THROWN);
    assert(exception_count == 1);
    assert(strcmp(exception_name, "sun/net/ConnectionResetException") == 0);
    exception_count = 0;

    assert(Java_sun_nio_ch_SocketDispatcher_readv0(NULL, NULL,
            descriptor(fds[1]), (jlong)(intptr_t)vectors, -1) == IOS_THROWN);
    assert(exception_count == 1);
    assert(strcmp(exception_name, "java/io/IOException") == 0);
    exception_count = 0;

    close(fds[0]);
}

static void test_datagram_dispatcher(void) {
    int receiver = socket(AF_INET, SOCK_DGRAM, 0);
    int sender = socket(AF_INET, SOCK_DGRAM, 0);
    assert(receiver >= 0 && sender >= 0);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    assert(bind(receiver, (struct sockaddr *)&address, sizeof(address)) == 0);
    socklen_t address_length = sizeof(address);
    assert(getsockname(receiver, (struct sockaddr *)&address, &address_length) == 0);
    assert(connect(sender, (struct sockaddr *)&address, sizeof(address)) == 0);

    struct iovec out[] = {
        { .iov_base = (void *)"hello", .iov_len = 5 },
        { .iov_base = (void *)"-world", .iov_len = 6 },
    };
    assert(Java_sun_nio_ch_DatagramDispatcher_writev0(NULL, NULL,
            descriptor(sender), (jlong)(intptr_t)out, 2) == 11);

    char left[4] = {0};
    char right[10] = {0};
    char byte = 0;
    struct iovec in[] = {
        { .iov_base = left, .iov_len = 3 },
        { .iov_base = right, .iov_len = 8 },
    };
    assert(Java_sun_nio_ch_DatagramDispatcher_readv0(NULL, NULL,
            descriptor(receiver), (jlong)(intptr_t)in, 2) == 11);
    assert(memcmp(left, "hel", 3) == 0);
    assert(memcmp(right, "lo-world", 8) == 0);

    assert(send(sender, "x", 1, 0) == 1);
    in[0].iov_len = 0;
    in[1].iov_len = 0;
    assert(Java_sun_nio_ch_DatagramDispatcher_readv0(NULL, NULL,
            descriptor(receiver), (jlong)(intptr_t)in, 2) == 0);
    in[0].iov_len = 3;
    in[1].iov_len = 8;
    assert(Java_sun_nio_ch_DatagramDispatcher_read0(NULL, NULL,
            descriptor(receiver), 0, 0) == 0);
    assert(Java_sun_nio_ch_DatagramDispatcher_read0(NULL, NULL,
            descriptor(receiver), (jlong)(intptr_t)&byte, 1) == 1);
    assert(byte == 'x');

    assert(send(sender, "", 0, 0) == 0);
    assert(Java_sun_nio_ch_DatagramDispatcher_read0(NULL, NULL,
            descriptor(receiver), (jlong)(intptr_t)&byte, 1) == 0);
    assert(send(sender, "", 0, 0) == 0);
    assert(Java_sun_nio_ch_DatagramDispatcher_readv0(NULL, NULL,
            descriptor(receiver), (jlong)(intptr_t)in, 2) == 0);
    assert(Java_sun_nio_ch_DatagramDispatcher_writev0(NULL, NULL,
            descriptor(sender), 0, 0) == 0);
    assert(Java_sun_nio_ch_DatagramDispatcher_read0(NULL, NULL,
            descriptor(receiver), (jlong)(intptr_t)&byte, 1) == 0);

    struct iovec invalid = { .iov_base = NULL, .iov_len = 1 };
    assert(Java_sun_nio_ch_DatagramDispatcher_writev0(NULL, NULL,
            descriptor(sender), (jlong)(intptr_t)&invalid, 1) == IOS_THROWN);
    assert(exception_count == 1);
    assert(strcmp(exception_name, "java/io/IOException") == 0);
    exception_count = 0;
    invalid.iov_base = (void *)"x";
    invalid.iov_len = SIZE_MAX;
    assert(Java_sun_nio_ch_DatagramDispatcher_readv0(NULL, NULL,
            descriptor(receiver), (jlong)(intptr_t)&invalid, 1) == IOS_THROWN);
    assert(exception_count == 1);
    exception_count = 0;
    assert(Java_sun_nio_ch_DatagramDispatcher_readv0(NULL, NULL,
            descriptor(receiver), (jlong)(intptr_t)&invalid, 1025) == IOS_THROWN);
    assert(exception_count == 1);
    exception_count = 0;

    close(sender);
    close(receiver);
}

int main(void) {
    test_stream_dispatcher();
    test_datagram_dispatcher();
    puts("Horizon socket/datagram JNI loopback host test: PASS");
    return 0;
}
