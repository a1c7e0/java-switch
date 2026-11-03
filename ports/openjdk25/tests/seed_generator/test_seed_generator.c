/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "jni.h"
#include "switch.h"

typedef struct {
    jsize length;
    jbyte bytes[64];
} FakeArray;

static Result init_result;
static Result random_result;
static unsigned int init_calls;
static unsigned int exit_calls;
static unsigned int random_calls;
static unsigned int array_writes;
static jboolean pending_exception;
static unsigned int null_throws;
static unsigned int oom_throws;
static unsigned int random_fill;

Result csrngInitialize(void) {
    ++init_calls;
    return init_result;
}

void csrngExit(void) {
    ++exit_calls;
}

Result csrngGetRandomBytes(void *out, size_t out_size) {
    ++random_calls;
    memset(out, (int)random_fill, out_size);
    return random_result;
}

void JNU_ThrowNullPointerException(JNIEnv *env, const char *message) {
    (void)env;
    (void)message;
    ++null_throws;
    pending_exception = JNI_TRUE;
}

void JNU_ThrowOutOfMemoryError(JNIEnv *env, const char *message) {
    (void)env;
    (void)message;
    ++oom_throws;
    pending_exception = JNI_TRUE;
}

static jsize JNICALL fake_get_array_length(JNIEnv *env, jarray array) {
    (void)env;
    return ((FakeArray *)array)->length;
}

static jboolean JNICALL fake_exception_check(JNIEnv *env) {
    (void)env;
    return pending_exception;
}

static void JNICALL fake_set_byte_array_region(JNIEnv *env, jbyteArray array,
        jsize start, jsize length, const jbyte *bytes) {
    (void)env;
    FakeArray *destination = (FakeArray *)array;
    assert(start == 0);
    assert(length <= destination->length);
    memcpy(destination->bytes, bytes, (size_t)length);
    ++array_writes;
}

JNIEXPORT jlong JNICALL
Java_sun_security_provider_NativeSeedGenerator_nativeGenerateSeed(
        JNIEnv *env, jclass clazz, jbyteArray result_array);

static struct JNINativeInterface_ jni_functions = {
    .GetArrayLength = fake_get_array_length,
    .ExceptionCheck = fake_exception_check,
    .SetByteArrayRegion = fake_set_byte_array_region,
};

static void reset(void) {
    init_result = 0;
    random_result = 0;
    init_calls = exit_calls = random_calls = array_writes = 0;
    pending_exception = JNI_FALSE;
    null_throws = oom_throws = 0;
    random_fill = 0xA7;
}

int main(void) {
    JNIEnv env = &jni_functions;
    FakeArray array = {.length = 8};

    reset();
    assert(Java_sun_security_provider_NativeSeedGenerator_nativeGenerateSeed(
            &env, NULL, (jbyteArray)&array) == 0);
    assert(init_calls == 1 && random_calls == 1 && exit_calls == 1);
    assert(array_writes == 1);
    for (jsize i = 0; i < array.length; ++i) assert(array.bytes[i] == (jbyte)0xA7);

    reset();
    memset(array.bytes, 0x31, sizeof(array.bytes));
    init_result = 0x12345678u;
    assert((uint32_t)Java_sun_security_provider_NativeSeedGenerator_nativeGenerateSeed(
            &env, NULL, (jbyteArray)&array) == 0x12345678u);
    assert(init_calls == 1 && random_calls == 0 && exit_calls == 0);
    assert(array_writes == 0 && array.bytes[0] == 0x31);

    reset();
    memset(array.bytes, 0x42, sizeof(array.bytes));
    random_result = 0x87654321u;
    assert((uint32_t)Java_sun_security_provider_NativeSeedGenerator_nativeGenerateSeed(
            &env, NULL, (jbyteArray)&array) == 0x87654321u);
    assert(init_calls == 1 && random_calls == 1 && exit_calls == 1);
    assert(array_writes == 0 && array.bytes[0] == 0x42);

    reset();
    array.length = 0;
    assert(Java_sun_security_provider_NativeSeedGenerator_nativeGenerateSeed(
            &env, NULL, (jbyteArray)&array) == 0);
    assert(init_calls == 0 && random_calls == 0 && exit_calls == 0);

    reset();
    assert(Java_sun_security_provider_NativeSeedGenerator_nativeGenerateSeed(
            &env, NULL, NULL) == 0);
    assert(null_throws == 1 && pending_exception);
    assert(init_calls == 0 && array_writes == 0);

    puts("SeedGenerator CSRNG bridge tests passed");
    return 0;
}
