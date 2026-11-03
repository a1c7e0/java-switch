/*
 * Copyright (c) 2002, 2020, Oracle and/or its affiliates. All rights reserved.
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
#include <string.h>
#include <unistd.h>

#include "jni.h"
#include "sun_net_dns_ResolverConfigurationImpl.h"

/* libnx resolves through the initialized BSD service; it exposes no
 * supported nameserver/search-domain configuration API. Java's existing
 * /etc/resolv.conf parser may therefore return an empty configuration on
 * Horizon. Only provide a search suffix when the real system hostname has one. */
JNIEXPORT jstring JNICALL
Java_sun_net_dns_ResolverConfigurationImpl_fallbackDomain0(JNIEnv* env,
                                                           jclass clazz) {
    (void)clazz;
    char hostname[1025];
    if (gethostname(hostname, sizeof(hostname)) != 0) return NULL;
    hostname[sizeof(hostname) - 1] = '\0';
    char* dot = strchr(hostname, '.');
    if (dot == NULL || dot[1] == '\0') return NULL;
    return (*env)->NewStringUTF(env, dot + 1);
}
