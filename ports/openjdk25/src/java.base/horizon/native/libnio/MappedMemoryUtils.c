/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */

#include "jni.h"
#include "jni_util.h"
#include "java_nio_MappedMemoryUtils.h"

static void throw_unsupported(JNIEnv *env) {
    JNU_ThrowByName(env, "java/lang/UnsupportedOperationException",
                    "Mapped memory operations are unavailable on Horizon");
}

JNIEXPORT jboolean JNICALL
Java_java_nio_MappedMemoryUtils_isLoaded0(JNIEnv *env, jclass clazz,
                                          jlong address, jlong length,
                                          jlong pages) {
    (void)clazz;
    (void)address;
    (void)length;
    (void)pages;
    throw_unsupported(env);
    return JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_java_nio_MappedMemoryUtils_load0(JNIEnv *env, jclass clazz,
                                      jlong address, jlong length) {
    (void)clazz;
    (void)address;
    (void)length;
    throw_unsupported(env);
}

JNIEXPORT void JNICALL
Java_java_nio_MappedMemoryUtils_unload0(JNIEnv *env, jclass clazz,
                                        jlong address, jlong length) {
    (void)clazz;
    (void)address;
    (void)length;
    throw_unsupported(env);
}

JNIEXPORT void JNICALL
Java_java_nio_MappedMemoryUtils_force0(JNIEnv *env, jclass clazz,
                                       jobject descriptor, jlong address,
                                       jlong length) {
    (void)clazz;
    (void)descriptor;
    (void)address;
    (void)length;
    throw_unsupported(env);
}

JNIEXPORT void JNICALL
Java_java_nio_MappedMemoryUtils_registerNatives(JNIEnv *env, jclass clazz) {
    static const JNINativeMethod methods[] = {
        {"isLoaded0", "(JJJ)Z", (void *)&Java_java_nio_MappedMemoryUtils_isLoaded0},
        {"load0", "(JJ)V", (void *)&Java_java_nio_MappedMemoryUtils_load0},
        {"unload0", "(JJ)V", (void *)&Java_java_nio_MappedMemoryUtils_unload0},
        {"force0", "(Ljava/io/FileDescriptor;JJ)V",
         (void *)&Java_java_nio_MappedMemoryUtils_force0},
    };
    (void)(*env)->RegisterNatives(env, clazz, methods,
                                 (jint)(sizeof(methods) / sizeof(methods[0])));
}
