/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Host test of the real JNI wakeup socket pair and IOUtil wrappers. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "jni.h"
#include "jni_util.h"
#include "nio.h"
#include "sun_nio_ch_IOUtil.h"

static unsigned exceptions;

static jboolean JNICALL mock_exception_check(JNIEnv *env) {
    (void)env;
    return JNI_FALSE;
}

void JNU_ThrowByName(JNIEnv *env, const char *name, const char *message) {
    (void)env;
    (void)name;
    (void)message;
    exceptions++;
}

void JNU_ThrowIOExceptionWithLastError(JNIEnv *env, const char *message) {
    (void)env;
    (void)message;
    exceptions++;
}

/* The test uses the host TCP stack; the Horizon initializer is separately
 * exercised by the target build and supplied here only as a service stub. */
int NET_PlatformInit(void) { return 0; }

static void verify_pair(jlong pair, int should_block) {
    int reader = (int)(uint32_t)((uint64_t)pair >> 32);
    int writer = (int)(uint32_t)pair;
    struct sockaddr_in local;
    struct sockaddr_in peer;
    struct sockaddr_in reader_local;
    struct sockaddr_in reader_peer;
    socklen_t length = sizeof(local);
    struct pollfd pfd = { .fd = reader, .events = POLLIN };

    assert(reader >= 0 && writer >= 0 && reader != writer);
    assert(getsockname(writer, (struct sockaddr *)&local, &length) == 0);
    length = sizeof(peer);
    assert(getpeername(writer, (struct sockaddr *)&peer, &length) == 0);
    length = sizeof(reader_local);
    assert(getsockname(reader, (struct sockaddr *)&reader_local, &length) == 0);
    length = sizeof(reader_peer);
    assert(getpeername(reader, (struct sockaddr *)&reader_peer, &length) == 0);
    assert(local.sin_family == AF_INET && peer.sin_family == AF_INET);
    assert(local.sin_addr.s_addr == htonl(INADDR_LOOPBACK));
    assert(peer.sin_addr.s_addr == htonl(INADDR_LOOPBACK));
    assert(reader_local.sin_port == peer.sin_port);
    assert(reader_local.sin_addr.s_addr == peer.sin_addr.s_addr);
    assert(reader_peer.sin_port == local.sin_port);
    assert(reader_peer.sin_addr.s_addr == local.sin_addr.s_addr);

    int reader_flags = fcntl(reader, F_GETFL);
    int writer_flags = fcntl(writer, F_GETFL);
    assert(reader_flags >= 0 && writer_flags >= 0);
    assert(((reader_flags & O_NONBLOCK) == 0) == should_block);
    assert(((writer_flags & O_NONBLOCK) == 0) == should_block);

    if (!should_block) {
        assert(poll(&pfd, 1, 0) == 0);
        assert(Java_sun_nio_ch_IOUtil_write1(NULL, NULL, writer,
                                             (jbyte)'w') == 1);
        assert(poll(&pfd, 1, 1000) == 1);
        assert((pfd.revents & POLLIN) != 0);
        assert(Java_sun_nio_ch_IOUtil_drain1(NULL, NULL, reader) == 1);
        assert(poll(&pfd, 1, 0) == 0);
    }
    assert(close(reader) == 0);
    assert(close(writer) == 0);
}

int main(void) {
    static const struct JNINativeInterface_ functions = {
        .ExceptionCheck = mock_exception_check,
    };
    JNIEnv env = &functions;
    JNIEnv *envp = &env;

    verify_pair(Java_sun_nio_ch_IOUtil_makePipe(envp, NULL, JNI_FALSE), 0);
    verify_pair(Java_sun_nio_ch_IOUtil_makePipe(envp, NULL, JNI_TRUE), 1);
    assert(exceptions == 0);
    puts("Horizon selector wakeup TCP host test: PASS");
    return 0;
}
