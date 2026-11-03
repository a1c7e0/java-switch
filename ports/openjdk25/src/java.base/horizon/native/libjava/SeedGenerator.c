/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
#include <switch.h>

#include <stdlib.h>
#include <string.h>

#include "jni.h"
#include "jni_util.h"

/* Keep data in private native memory while the CSRNG IPC call is in progress;
 * do not hold a JNI array pin or critical section across the service call. */
JNIEXPORT jlong JNICALL
Java_sun_security_provider_NativeSeedGenerator_nativeGenerateSeed(
    JNIEnv* env, jclass clazz, jbyteArray result_array) {
    (void)clazz;
    if (result_array == NULL) {
        JNU_ThrowNullPointerException(env, "seed result array is null");
        return 0;
    }
    const jsize length = (*env)->GetArrayLength(env, result_array);
    if ((*env)->ExceptionCheck(env)) return 0;
    if (length == 0) return 0;

    unsigned char* bytes = malloc((size_t)length);
    if (bytes == NULL) {
        JNU_ThrowOutOfMemoryError(env, "Native CSRNG buffer allocation failed");
        return 0;
    }

    Result result = csrngInitialize();
    if (R_FAILED(result)) {
        memset(bytes, 0, (size_t)length);
        free(bytes);
        return (jlong)(uint64_t)(uint32_t)result;
    }

    result = csrngGetRandomBytes(bytes, (size_t)length);
    csrngExit();
    if (R_SUCCEEDED(result)) {
        (*env)->SetByteArrayRegion(env, result_array, 0, length,
                                   (const jbyte*)bytes);
    }
    memset(bytes, 0, (size_t)length);
    free(bytes);
    if ((*env)->ExceptionCheck(env)) return 0;
    return R_SUCCEEDED(result) ? 0 : (jlong)(uint64_t)(uint32_t)result;
}
