/*
 * Copyright (c) 2000, 2025, Oracle and/or its affiliates. All rights reserved.
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
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "net_util.h"
#include "java_net_Inet4AddressImpl.h"
#include "java_net_Inet6AddressImpl.h"
#include "java_net_spi_InetAddressResolver_LookupPolicy.h"

static int supported_address(const struct addrinfo* address, int ipv6_enabled) {
    if (address->ai_addr == NULL) return 0;
    if (address->ai_family == AF_INET)
        return address->ai_addrlen >= sizeof(struct sockaddr_in);
    if (address->ai_family == AF_INET6)
        return ipv6_enabled && address->ai_addrlen >= sizeof(struct sockaddr_in6);
    return 0;
}

static int duplicate_before(const struct addrinfo* head,
                            const struct addrinfo* current,
                            int ipv6_enabled) {
    for (const struct addrinfo* previous = head; previous != current;
         previous = previous->ai_next) {
        if (!supported_address(previous, ipv6_enabled) ||
            previous->ai_family != current->ai_family) continue;
        if (current->ai_family == AF_INET) {
            if (((const struct sockaddr_in*)previous->ai_addr)->sin_addr.s_addr ==
                ((const struct sockaddr_in*)current->ai_addr)->sin_addr.s_addr)
                return 1;
        } else if (memcmp(&((const struct sockaddr_in6*)previous->ai_addr)->sin6_addr,
                          &((const struct sockaddr_in6*)current->ai_addr)->sin6_addr,
                          sizeof(struct in6_addr)) == 0 &&
                   ((const struct sockaddr_in6*)previous->ai_addr)->sin6_scope_id ==
                   ((const struct sockaddr_in6*)current->ai_addr)->sin6_scope_id) {
            return 1;
        }
    }
    return 0;
}

JNIEXPORT jstring JNICALL
Java_java_net_Inet6AddressImpl_getLocalHostName(JNIEnv *env, jobject self) {
    (void)self;
    char hostname[NI_MAXHOST + 1];
    hostname[0] = '\0';
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        memcpy(hostname, "localhost", sizeof("localhost"));
    } else {
        hostname[NI_MAXHOST] = '\0';
    }
    return (*env)->NewStringUTF(env, hostname);
}

JNIEXPORT jobjectArray JNICALL
Java_java_net_Inet6AddressImpl_lookupAllHostAddr(JNIEnv *env, jobject self,
                                                  jstring host,
                                                  jint characteristics) {
    (void)self;
    jobjectArray result = NULL;
    const char* hostname = NULL;
    struct addrinfo hints;
    struct addrinfo* addresses = NULL;
    const int ipv6_enabled = ipv6_available() == JNI_TRUE;
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
    hints.ai_family = lookupCharacteristicsToAddressFamily(characteristics);
    if (!ipv6_enabled) {
        if (hints.ai_family == AF_INET6) {
            NET_ThrowUnknownHostExceptionWithGaiError(env, hostname, EAI_FAMILY);
            goto done;
        }
        hints.ai_family = AF_INET;
    }
    error = getaddrinfo(hostname, NULL, &hints, &addresses);
    if (error != 0) {
        NET_ThrowUnknownHostExceptionWithGaiError(env, hostname, error);
        goto done;
    }

    jint count = 0;
    for (struct addrinfo* it = addresses; it != NULL; it = it->ai_next) {
        if (!supported_address(it, ipv6_enabled) ||
            duplicate_before(addresses, it, ipv6_enabled)) continue;
        ++count;
    }
    if (count == 0) {
        JNU_ThrowByName(env, "java/net/UnknownHostException", hostname);
        goto done;
    }
    result = (*env)->NewObjectArray(env, count, ia_class, NULL);
    if (result == NULL) goto done;

    const int system_order = addressesInSystemOrder(characteristics);
    const int ipv6_first = !system_order &&
        (characteristics & java_net_spi_InetAddressResolver_LookupPolicy_IPV6_FIRST) != 0;
    jint index = 0;
    for (int pass = 0; pass < (system_order ? 1 : 2); ++pass) {
        int family = AF_UNSPEC;
        if (!system_order) {
            const int first_family = ipv6_first ? AF_INET6 : AF_INET;
            family = pass == 0 ? first_family :
                (first_family == AF_INET ? AF_INET6 : AF_INET);
        }
        for (struct addrinfo* it = addresses; it != NULL; it = it->ai_next) {
            if (!supported_address(it, ipv6_enabled) ||
                duplicate_before(addresses, it, ipv6_enabled) ||
                (!system_order && it->ai_family != family)) continue;
            int port = 0;
            jobject object = NET_SockaddrToInetAddress(
                env, (SOCKETADDRESS*)it->ai_addr, &port);
            if (object == NULL) { result = NULL; goto done; }
            setInetAddress_hostName(env, object, host);
            if ((*env)->ExceptionCheck(env)) {
                (*env)->DeleteLocalRef(env, object);
                result = NULL;
                goto done;
            }
            (*env)->SetObjectArrayElement(env, result, index++, object);
            (*env)->DeleteLocalRef(env, object);
            if ((*env)->ExceptionCheck(env)) { result = NULL; goto done; }
        }
    }
done:
    if (addresses != NULL) freeaddrinfo(addresses);
    if (hostname != NULL) JNU_ReleaseStringPlatformChars(env, host, hostname);
    return result;
}

JNIEXPORT jstring JNICALL
Java_java_net_Inet6AddressImpl_getHostByAddr(JNIEnv *env, jobject self,
                                              jbyteArray address) {
    (void)self;
    jsize length = (*env)->GetArrayLength(env, address);
    SOCKETADDRESS sa;
    socklen_t sa_length;
    memset(&sa, 0, sizeof(sa));
    if (length == 4) {
        (*env)->GetByteArrayRegion(env, address, 0, 4,
                                   (jbyte*)&sa.sa4.sin_addr.s_addr);
        sa.sa4.sin_family = AF_INET;
#ifdef __SWITCH__
        sa.sa4.sin_len = sizeof(sa.sa4);
#endif
        sa_length = sizeof(sa.sa4);
    } else if (length == 16) {
        (*env)->GetByteArrayRegion(env, address, 0, 16,
                                   (jbyte*)&sa.sa6.sin6_addr);
        sa.sa6.sin6_family = AF_INET6;
        sa_length = sizeof(sa.sa6);
    } else {
        JNU_ThrowByName(env, "java/net/UnknownHostException", NULL);
        return NULL;
    }
    if ((*env)->ExceptionCheck(env)) return NULL;
    char hostname[NI_MAXHOST + 1];
    int error = getnameinfo(&sa.sa, sa_length, hostname, sizeof(hostname),
                            NULL, 0, NI_NAMEREQD);
    if (error != 0) {
        JNU_ThrowByName(env, "java/net/UnknownHostException", NULL);
        return NULL;
    }
    return (*env)->NewStringUTF(env, hostname);
}

JNIEXPORT jboolean JNICALL
Java_java_net_Inet6AddressImpl_isReachable0(JNIEnv *env, jobject self,
                                            jbyteArray address, jint scope,
                                            jint timeout, jbyteArray if_address,
                                            jint ttl, jint if_scope) {
    (void)scope; (void)timeout; (void)if_address; (void)ttl; (void)if_scope;
    if ((*env)->GetArrayLength(env, address) == 4) {
        return Java_java_net_Inet4AddressImpl_isReachable0(
            env, self, address, timeout, if_address, ttl);
    }
    JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException",
                    "ICMP reachability is unsupported on Horizon");
    return JNI_FALSE;
}
