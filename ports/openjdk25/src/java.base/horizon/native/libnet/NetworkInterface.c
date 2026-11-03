/*
 * Copyright (c) 2000, 2026, Oracle and/or its affiliates. All rights reserved.
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
#include "net_util.h"
#include "java_net_NetworkInterface.h"

jclass ni_class;
jfieldID ni_nameID;
jfieldID ni_indexID;
jfieldID ni_addrsID;
jfieldID ni_descID;
jfieldID ni_bindsID;
jfieldID ni_virutalID;
jfieldID ni_childsID;
jfieldID ni_parentID;
jmethodID ni_ctrID;

static jclass ni_interface_address_class;
static jmethodID ni_interface_address_constructor;
static jfieldID ni_interface_address_address;
static jfieldID ni_interface_address_broadcast;
static jfieldID ni_interface_address_mask;

JNIEXPORT void JNICALL
Java_java_net_NetworkInterface_init(JNIEnv* env, jclass clazz) {
    (void)clazz;
    ni_class = (*env)->FindClass(env, "java/net/NetworkInterface");
    CHECK_NULL(ni_class);
    ni_class = (*env)->NewGlobalRef(env, ni_class);
    CHECK_NULL(ni_class);
    ni_nameID = (*env)->GetFieldID(env, ni_class, "name", "Ljava/lang/String;");
    CHECK_NULL(ni_nameID);
    ni_indexID = (*env)->GetFieldID(env, ni_class, "index", "I");
    CHECK_NULL(ni_indexID);
    ni_addrsID = (*env)->GetFieldID(env, ni_class, "addrs", "[Ljava/net/InetAddress;");
    CHECK_NULL(ni_addrsID);
    ni_bindsID = (*env)->GetFieldID(env, ni_class, "bindings", "[Ljava/net/InterfaceAddress;");
    CHECK_NULL(ni_bindsID);
    ni_descID = (*env)->GetFieldID(env, ni_class, "displayName", "Ljava/lang/String;");
    CHECK_NULL(ni_descID);
    ni_virutalID = (*env)->GetFieldID(env, ni_class, "virtual", "Z");
    CHECK_NULL(ni_virutalID);
    ni_childsID = (*env)->GetFieldID(env, ni_class, "childs", "[Ljava/net/NetworkInterface;");
    CHECK_NULL(ni_childsID);
    ni_parentID = (*env)->GetFieldID(env, ni_class, "parent", "Ljava/net/NetworkInterface;");
    CHECK_NULL(ni_parentID);
    ni_ctrID = (*env)->GetMethodID(env, ni_class, "<init>", "()V");
    CHECK_NULL(ni_ctrID);

    ni_interface_address_class = (*env)->FindClass(env, "java/net/InterfaceAddress");
    CHECK_NULL(ni_interface_address_class);
    ni_interface_address_class = (*env)->NewGlobalRef(env, ni_interface_address_class);
    CHECK_NULL(ni_interface_address_class);
    ni_interface_address_constructor = (*env)->GetMethodID(
        env, ni_interface_address_class, "<init>", "()V");
    CHECK_NULL(ni_interface_address_constructor);
    ni_interface_address_address = (*env)->GetFieldID(
        env, ni_interface_address_class, "address", "Ljava/net/InetAddress;");
    CHECK_NULL(ni_interface_address_address);
    ni_interface_address_broadcast = (*env)->GetFieldID(
        env, ni_interface_address_class, "broadcast", "Ljava/net/Inet4Address;");
    CHECK_NULL(ni_interface_address_broadcast);
    ni_interface_address_mask = (*env)->GetFieldID(
        env, ni_interface_address_class, "maskLength", "S");
    CHECK_NULL(ni_interface_address_mask);

    initInetAddressIDs(env);
}

static void throw_interface_query_unavailable(JNIEnv* env) {
    JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException",
                    "Network interface enumeration is unavailable on Horizon");
}

JNIEXPORT jobject JNICALL
Java_java_net_NetworkInterface_getByName0(JNIEnv* env, jclass clazz,
                                          jstring name) {
    (void)clazz; (void)name;
    throw_interface_query_unavailable(env);
    return NULL;
}

JNIEXPORT jobject JNICALL
Java_java_net_NetworkInterface_getByIndex0(JNIEnv* env, jclass clazz,
                                           jint index) {
    (void)clazz; (void)index;
    throw_interface_query_unavailable(env);
    return NULL;
}

JNIEXPORT jboolean JNICALL
Java_java_net_NetworkInterface_boundInetAddress0(JNIEnv* env, jclass clazz,
                                                 jobject address) {
    (void)clazz; (void)address;
    throw_interface_query_unavailable(env);
    return JNI_FALSE;
}

JNIEXPORT jobject JNICALL
Java_java_net_NetworkInterface_getByInetAddress0(JNIEnv* env, jclass clazz,
                                                  jobject address) {
    (void)clazz; (void)address;
    throw_interface_query_unavailable(env);
    return NULL;
}

JNIEXPORT jobjectArray JNICALL
Java_java_net_NetworkInterface_getAll(JNIEnv* env, jclass clazz) {
    (void)clazz;
    throw_interface_query_unavailable(env);
    return NULL;
}

JNIEXPORT jboolean JNICALL
Java_java_net_NetworkInterface_isUp0(JNIEnv* env, jclass clazz,
                                     jstring name, jint index) {
    (void)clazz; (void)name; (void)index;
    throw_interface_query_unavailable(env);
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_java_net_NetworkInterface_isP2P0(JNIEnv* env, jclass clazz,
                                      jstring name, jint index) {
    (void)clazz; (void)name; (void)index;
    throw_interface_query_unavailable(env);
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_java_net_NetworkInterface_isLoopback0(JNIEnv* env, jclass clazz,
                                           jstring name, jint index) {
    (void)clazz; (void)name; (void)index;
    throw_interface_query_unavailable(env);
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_java_net_NetworkInterface_supportsMulticast0(JNIEnv* env, jclass clazz,
                                                  jstring name, jint index) {
    (void)clazz; (void)name; (void)index;
    throw_interface_query_unavailable(env);
    return JNI_FALSE;
}

JNIEXPORT jbyteArray JNICALL
Java_java_net_NetworkInterface_getMacAddr0(JNIEnv* env, jclass clazz,
                                           jbyteArray address, jstring name,
                                           jint index) {
    (void)clazz; (void)address; (void)name; (void)index;
    throw_interface_query_unavailable(env);
    return NULL;
}

JNIEXPORT jint JNICALL
Java_java_net_NetworkInterface_getMTU0(JNIEnv* env, jclass clazz,
                                       jstring name, jint index) {
    (void)clazz; (void)name; (void)index;
    throw_interface_query_unavailable(env);
    return -1;
}
