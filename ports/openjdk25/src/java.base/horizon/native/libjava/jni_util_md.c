/*
 * Copyright (c) 2008, 2024, Oracle and/or its affiliates. All rights reserved.
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
#include <string.h>

#include "jni.h"
#include "jni_util.h"

/* The VM exports the actual process-scope token used by its static native
 * resolver. It is passed back to JVM_FindLibraryEntry; it is not a libdl
 * handle, and Horizon does not claim dlopen/dlsym support.
 */
extern void *js_horizon_process_handle(void);

void *getProcessHandle(void) {
    return js_horizon_process_handle();
}

jstring
getLastErrorString(JNIEnv *env)
{
    char buffer[256] = {0};
    if (errno == 0) return NULL;
    getErrorString(errno, buffer, sizeof(buffer));
    return buffer[0] != '\0' ? JNU_NewStringPlatform(env, buffer) : NULL;
}

JNIEXPORT int JNICALL
getErrorString(int error, char *buffer, size_t length)
{
    if (error == 0 || length == 0) return 0;
    const char *description = strerror(error);
    if (description == NULL) return EINVAL;
    size_t description_length = strlen(description);
    if (description_length >= length) {
        memcpy(buffer, description, length - 1);
        buffer[length - 1] = '\0';
        return ERANGE;
    }
    memcpy(buffer, description, description_length + 1);
    return 0;
}
