/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include "jni.h"
#include "jdk_internal_io_JdkConsoleImpl.h"

static int tty_state;
static int captured_errno;

int isatty(int fd) {
    assert(fd == fileno(stdin));
    return tty_state;
}

void JNU_ThrowIOExceptionWithLastError(JNIEnv *env, const char *message) {
    (void)env;
    (void)message;
    captured_errno = errno;
}

int main(void) {
    tty_state = 0;
    captured_errno = 0;
    assert(Java_jdk_internal_io_JdkConsoleImpl_echo(NULL, NULL, JNI_FALSE) == JNI_FALSE);
    assert(captured_errno == ENOTTY);

    tty_state = 1;
    captured_errno = 0;
    assert(Java_jdk_internal_io_JdkConsoleImpl_echo(NULL, NULL, JNI_TRUE) == JNI_FALSE);
    assert(captured_errno == ENOTSUP);
    return 0;
}
