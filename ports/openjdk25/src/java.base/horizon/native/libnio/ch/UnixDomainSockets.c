/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */

#include "jni.h"
#include "jni_util.h"
#include "nio.h"
#include "sun_nio_ch_UnixDomainSockets.h"

static void throw_unsupported(JNIEnv *env) {
    JNU_ThrowByName(env, "java/lang/UnsupportedOperationException",
                    "Unix-domain sockets are unavailable on Horizon");
}

JNIEXPORT jboolean JNICALL
Java_sun_nio_ch_UnixDomainSockets_init(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    return JNI_FALSE;
}

JNIEXPORT jbyteArray JNICALL
Java_sun_nio_ch_UnixDomainSockets_localAddress0(JNIEnv *env, jclass clazz,
                                                jobject descriptor) {
    (void)clazz;
    (void)descriptor;
    throw_unsupported(env);
    return NULL;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_UnixDomainSockets_socket0(JNIEnv *env, jclass clazz) {
    (void)clazz;
    throw_unsupported(env);
    return -1;
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_UnixDomainSockets_bind0(JNIEnv *env, jclass clazz,
                                        jobject descriptor, jbyteArray path) {
    (void)clazz;
    (void)descriptor;
    (void)path;
    throw_unsupported(env);
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_UnixDomainSockets_connect0(JNIEnv *env, jclass clazz,
                                           jobject descriptor, jbyteArray path) {
    (void)clazz;
    (void)descriptor;
    (void)path;
    throw_unsupported(env);
    return IOS_THROWN;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_UnixDomainSockets_accept0(JNIEnv *env, jclass clazz,
                                          jobject server, jobject client,
                                          jobjectArray address) {
    (void)clazz;
    (void)server;
    (void)client;
    (void)address;
    throw_unsupported(env);
    return IOS_THROWN;
}
