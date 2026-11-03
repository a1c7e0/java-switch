/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "jni.h"
#include "jni_util.h"
#include "nio.h"
#include "sun_nio_ch_IOUtil.h"
#include "sun_nio_ch_NativeThread.h"

static int exception_count;
static const char *last_exception;
static int test_fd_field;

static jclass JNICALL mock_find_class(JNIEnv *env, const char *name) {
    (void)env; (void)name;
    return (jclass)(uintptr_t)1;
}
static jfieldID JNICALL mock_get_field_id(JNIEnv *env, jclass cls,
                                          const char *name, const char *sig) {
    (void)env; (void)cls; (void)name; (void)sig;
    return (jfieldID)(uintptr_t)1;
}
static jint JNICALL mock_get_int_field(JNIEnv *env, jobject obj, jfieldID id) {
    (void)env; (void)id;
    return *(int *)obj;
}
static void JNICALL mock_set_int_field(JNIEnv *env, jobject obj, jfieldID id,
                                       jint value) {
    (void)env; (void)id;
    *(int *)obj = value;
}
static jboolean JNICALL mock_exception_check(JNIEnv *env) {
    (void)env;
    return JNI_FALSE;
}

void JNU_ThrowByName(JNIEnv *env, const char *name, const char *message) {
    (void)env; (void)message;
    exception_count++;
    last_exception = name;
}
void JNU_ThrowIOExceptionWithLastError(JNIEnv *env, const char *message) {
    (void)env; (void)message;
    exception_count++;
    last_exception = "java/io/IOException";
}

/* Host socket service initialization is mocked; TCP itself is real here. */
int NET_PlatformInit(void) { return 0; }

int main(void) {
    static const struct JNINativeInterface_ functions = {
        .FindClass = mock_find_class,
        .GetFieldID = mock_get_field_id,
        .GetIntField = mock_get_int_field,
        .SetIntField = mock_set_int_field,
        .ExceptionCheck = mock_exception_check,
    };
    JNIEnv env = &functions;
    JNIEnv *envp = &env;

    Java_sun_nio_ch_IOUtil_initIDs(envp, NULL);
    test_fd_field = 17;
    jobject field = (jobject)&test_fd_field;
    assert(Java_sun_nio_ch_IOUtil_fdVal(envp, NULL, field) == 17);
    Java_sun_nio_ch_IOUtil_setfdVal(envp, NULL, field, 23);
    assert(test_fd_field == 23);
    assert(Java_sun_nio_ch_IOUtil_iovMax(envp, NULL) == 1024);
    assert(Java_sun_nio_ch_IOUtil_writevMax(envp, NULL) == INT32_MAX);

    int channel[2];
    assert(pipe(channel) == 0); /* host harness only */
    int descriptor = channel[0];
    Java_sun_nio_ch_IOUtil_configureBlocking(envp, NULL,
            (jobject)&descriptor, JNI_FALSE);
    assert(exception_count == 0);
    assert((fcntl(channel[0], F_GETFL) & O_NONBLOCK) != 0);
    Java_sun_nio_ch_IOUtil_setfdVal(envp, NULL, (jobject)&descriptor,
                                    channel[1]);
    assert(descriptor == channel[1]);
    assert(Java_sun_nio_ch_IOUtil_write1(envp, NULL, channel[1], (jbyte)'x') == 1);
    assert(Java_sun_nio_ch_IOUtil_drain1(envp, NULL, channel[0]) == 1);
    assert(write(channel[1], "yz", 2) == 2);
    assert(Java_sun_nio_ch_IOUtil_drain(envp, NULL, channel[0]) == JNI_TRUE);
    assert(Java_sun_nio_ch_IOUtil_drain(envp, NULL, channel[0]) == JNI_FALSE);

    assert(Java_sun_nio_ch_NativeThread_current0(envp, NULL) == 0);
    assert(Java_sun_nio_ch_NativeThread_supportPendingSignals0(envp, NULL)
            == JNI_FALSE);
    Java_sun_nio_ch_NativeThread_init(envp, NULL);
    assert(exception_count == 0);
    Java_sun_nio_ch_NativeThread_signal0(envp, NULL, 123);
    assert(exception_count == 1);
    assert(strcmp(last_exception,
                  "java/lang/UnsupportedOperationException") == 0);

    jlong wakeup = Java_sun_nio_ch_IOUtil_makePipe(envp, NULL, JNI_FALSE);
    assert(exception_count == 1);
    int wake_read = (int)((uint64_t)wakeup >> 32);
    int wake_write = (int)(uint32_t)wakeup;
    assert(Java_sun_nio_ch_IOUtil_write1(envp, NULL, wake_write, (jbyte)'w') == 1);
    assert(Java_sun_nio_ch_IOUtil_drain1(envp, NULL, wake_read) == 1);
    assert(close(wake_read) == 0);
    assert(close(wake_write) == 0);

    assert(close(channel[0]) == 0);
    assert(close(channel[1]) == 0);
    puts("Horizon IOUtil and NativeThread JNI host test: PASS");
    return 0;
}
