/*
 * Copyright (c) 2000, 2023, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "net_util.h"
#include "java_net_Inet4AddressImpl.h"

JNIEXPORT jstring JNICALL
Java_java_net_Inet4AddressImpl_getLocalHostName(JNIEnv *env, jobject self) {
    char hostname[NI_MAXHOST + 1];
    (void)self;
    hostname[0] = '\0';
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        memcpy(hostname, "localhost", sizeof("localhost"));
    } else {
        hostname[NI_MAXHOST] = '\0';
    }
    return (*env)->NewStringUTF(env, hostname);
}

JNIEXPORT jobjectArray JNICALL
Java_java_net_Inet4AddressImpl_lookupAllHostAddr(JNIEnv *env, jobject self,
                                                  jstring host) {
    (void)self;
    jobjectArray result = NULL;
    const char *hostname = NULL;
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    int error;

    initInetAddressIDs(env);
    JNU_CHECK_EXCEPTION_RETURN(env, NULL);
    if (IS_NULL(host)) {
        JNU_ThrowNullPointerException(env, "host argument is null");
        return NULL;
    }
    hostname = JNU_GetStringPlatformCharsStrict(env, host, NULL);
    CHECK_NULL_RETURN(hostname, NULL);

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_CANONNAME;
    hints.ai_family = AF_INET;
    error = getaddrinfo(hostname, NULL, &hints, &addresses);
    if (error != 0) {
        NET_ThrowUnknownHostExceptionWithGaiError(env, hostname, error);
        goto done;
    }

    jint count = 0;
    for (struct addrinfo *it = addresses; it != NULL; it = it->ai_next) {
        if (it->ai_family != AF_INET || it->ai_addr == NULL ||
            it->ai_addrlen < sizeof(struct sockaddr_in)) continue;
        struct sockaddr_in *candidate = (struct sockaddr_in*)it->ai_addr;
        int duplicate = 0;
        for (struct addrinfo *prior = addresses; prior != it; prior = prior->ai_next) {
            if (prior->ai_family == AF_INET && prior->ai_addr != NULL &&
                prior->ai_addrlen >= sizeof(struct sockaddr_in) &&
                ((struct sockaddr_in*)prior->ai_addr)->sin_addr.s_addr ==
                    candidate->sin_addr.s_addr) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate) count++;
    }
    if (count == 0) {
        JNU_ThrowByName(env, "java/net/UnknownHostException", hostname);
        goto done;
    }

    result = (*env)->NewObjectArray(env, count, ia_class, NULL);
    if (result == NULL) goto done;
    jint index = 0;
    for (struct addrinfo *it = addresses; it != NULL; it = it->ai_next) {
        if (it->ai_family != AF_INET || it->ai_addr == NULL ||
            it->ai_addrlen < sizeof(struct sockaddr_in)) continue;
        struct sockaddr_in *candidate = (struct sockaddr_in*)it->ai_addr;
        int duplicate = 0;
        for (struct addrinfo *prior = addresses; prior != it; prior = prior->ai_next) {
            if (prior->ai_family == AF_INET && prior->ai_addr != NULL &&
                prior->ai_addrlen >= sizeof(struct sockaddr_in) &&
                ((struct sockaddr_in*)prior->ai_addr)->sin_addr.s_addr ==
                    candidate->sin_addr.s_addr) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) continue;
        jobject address = (*env)->NewObject(env, ia4_class, ia4_ctrID);
        if (address == NULL) { result = NULL; goto done; }
        setInetAddress_addr(env, address, (jint)ntohl(candidate->sin_addr.s_addr));
        if ((*env)->ExceptionCheck(env)) { result = NULL; goto done; }
        setInetAddress_hostName(env, address, host);
        if ((*env)->ExceptionCheck(env)) { result = NULL; goto done; }
        (*env)->SetObjectArrayElement(env, result, index++, address);
        (*env)->DeleteLocalRef(env, address);
        if ((*env)->ExceptionCheck(env)) { result = NULL; goto done; }
    }

done:
    if (addresses != NULL) freeaddrinfo(addresses);
    if (hostname != NULL) JNU_ReleaseStringPlatformChars(env, host, hostname);
    return result;
}

JNIEXPORT jstring JNICALL
Java_java_net_Inet4AddressImpl_getHostByAddr(JNIEnv *env, jobject self,
                                              jbyteArray address) {
    (void)self;
    if ((*env)->GetArrayLength(env, address) != 4) {
        JNU_ThrowByName(env, "java/net/UnknownHostException", NULL);
        return NULL;
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    (*env)->GetByteArrayRegion(env, address, 0, 4, (jbyte*)&sa.sin_addr.s_addr);
    if ((*env)->ExceptionCheck(env)) return NULL;
    sa.sin_family = AF_INET;
#ifdef __SWITCH__
    sa.sin_len = sizeof(sa);
#endif
    char hostname[NI_MAXHOST + 1];
    int error = getnameinfo((struct sockaddr*)&sa, sizeof(sa), hostname,
                            sizeof(hostname), NULL, 0, NI_NAMEREQD);
    if (error != 0) {
        JNU_ThrowByName(env, "java/net/UnknownHostException", NULL);
        return NULL;
    }
    return (*env)->NewStringUTF(env, hostname);
}

JNIEXPORT jboolean JNICALL
Java_java_net_Inet4AddressImpl_isReachable0(JNIEnv *env, jobject self,
                                            jbyteArray address, jint timeout,
                                            jbyteArray interface_address,
                                            jint ttl) {
    (void)self; (void)address; (void)timeout; (void)interface_address; (void)ttl;
    JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException",
                    "ICMP reachability is unsupported on Horizon");
    return JNI_FALSE;
}
