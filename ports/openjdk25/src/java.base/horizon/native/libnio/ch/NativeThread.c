/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */

#include "jni.h"
#include "jni_util.h"
#include "sun_nio_ch_NativeThread.h"

JNIEXPORT void JNICALL
Java_sun_nio_ch_NativeThread_init(JNIEnv *env, jclass clazz) {
    /* No signal handler is installed: Horizon native-thread interruption is
       not implemented, and the Java contract permits current0() to return 0. */
    (void)env;
    (void)clazz;
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_NativeThread_current0(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    return 0;
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_NativeThread_signal0(JNIEnv *env, jclass clazz, jlong thread) {
    (void)clazz;
    (void)thread;
    JNU_ThrowByName(env, "java/lang/UnsupportedOperationException",
                    "Native thread signaling is unavailable on Horizon");
}

JNIEXPORT jboolean JNICALL
Java_sun_nio_ch_NativeThread_supportPendingSignals0(JNIEnv *env,
                                                     jclass clazz) {
    (void)env;
    (void)clazz;
    return JNI_FALSE;
}
