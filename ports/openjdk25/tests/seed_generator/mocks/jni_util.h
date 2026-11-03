/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TEST_JNI_UTIL_H
#define TEST_JNI_UTIL_H
#include "jni.h"
void JNU_ThrowNullPointerException(JNIEnv *env, const char *message);
void JNU_ThrowOutOfMemoryError(JNIEnv *env, const char *message);
#endif
