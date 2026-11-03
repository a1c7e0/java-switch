/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
#ifndef HORIZON_NIO_UTIL_H
#define HORIZON_NIO_UTIL_H

#include "jni.h"
#include "jni_util.h"
#include "jvm.h"
#include "jlong.h"
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>

/* Horizon's socket backend does not expose SO_REUSEPORT. */
#ifndef SO_REUSEPORT
#define SO_REUSEPORT 0
#endif

/* NIO utility procedures exported by IOUtil.c. */
jint fdval(JNIEnv *env, jobject fdo);
void setfdval(JNIEnv *env, jobject fdo, jint value);
jint convertReturnVal(JNIEnv *env, jint n, jboolean reading);
jlong convertLongReturnVal(JNIEnv *env, jlong n, jboolean reading);

/* Net.c uses this helper; Horizon's socket implementation owns its policy. */
jint handleSocketError(JNIEnv *env, jint errorValue);

/* Unix-domain socket methods are supplied only by a matching native backend. */
struct sockaddr_un;
jbyteArray sockaddrToUnixAddressBytes(JNIEnv *env,
                                      struct sockaddr_un *sa,
                                      socklen_t len);
jint unixSocketAddressToSockaddr(JNIEnv *env, jbyteArray uaddr,
                                 struct sockaddr_un *sa, int *len);

#endif
