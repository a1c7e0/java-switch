/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "jni.h"
#include "jni_util.h"
#include "nio.h"
#include "java_nio_MappedMemoryUtils.h"
#include "sun_nio_ch_UnixDomainSockets.h"

static int thrown;
static const char *exception_class;
static JNINativeMethod registered[4];
static jint registered_count;

static jint JNICALL mock_register_natives(JNIEnv *env, jclass clazz,
                                          const JNINativeMethod *methods,
                                          jint count) {
    (void)env; (void)clazz;
    assert(count == 4);
    registered_count = count;
    for (jint i = 0; i < count; i++) registered[i] = methods[i];
    return JNI_OK;
}

void JNU_ThrowByName(JNIEnv *env, const char *name, const char *message) {
    (void)env; (void)message;
    thrown++;
    exception_class = name;
}

static void assert_unsupported(void) {
    assert(thrown > 0);
    assert(strcmp(exception_class,
                  "java/lang/UnsupportedOperationException") == 0);
    thrown = 0;
}

int main(void) {
    static const struct JNINativeInterface_ functions = {
        .RegisterNatives = mock_register_natives,
    };
    JNIEnv env = &functions;
    JNIEnv *envp = &env;
    jclass cls = (jclass)(uintptr_t)1;

    assert(Java_sun_nio_ch_UnixDomainSockets_init(envp, cls) == JNI_FALSE);
    assert(thrown == 0);
    assert(Java_sun_nio_ch_UnixDomainSockets_localAddress0(envp, cls, NULL) == NULL);
    assert_unsupported();
    assert(Java_sun_nio_ch_UnixDomainSockets_socket0(envp, cls) == -1);
    assert_unsupported();
    Java_sun_nio_ch_UnixDomainSockets_bind0(envp, cls, NULL, NULL);
    assert_unsupported();
    assert(Java_sun_nio_ch_UnixDomainSockets_connect0(envp, cls, NULL, NULL)
            == IOS_THROWN);
    assert_unsupported();
    assert(Java_sun_nio_ch_UnixDomainSockets_accept0(envp, cls, NULL, NULL, NULL)
            == IOS_THROWN);
    assert_unsupported();

    Java_java_nio_MappedMemoryUtils_registerNatives(envp, cls);
    assert(registered_count == 4);
    assert(strcmp(registered[0].name, "isLoaded0") == 0);
    assert(strcmp(registered[1].name, "load0") == 0);
    assert(strcmp(registered[2].name, "unload0") == 0);
    assert(strcmp(registered[3].name, "force0") == 0);

    assert(Java_java_nio_MappedMemoryUtils_isLoaded0(envp, cls, 1, 1, 1)
            == JNI_FALSE);
    assert_unsupported();
    Java_java_nio_MappedMemoryUtils_load0(envp, cls, 1, 1);
    assert_unsupported();
    Java_java_nio_MappedMemoryUtils_unload0(envp, cls, 1, 1);
    assert_unsupported();
    Java_java_nio_MappedMemoryUtils_force0(envp, cls, NULL, 1, 1);
    assert_unsupported();

    puts("Horizon unavailable NIO service JNI host test: PASS");
    return 0;
}
