/*
 * Copyright (c) 2022, Oracle and/or its affiliates. All rights reserved.
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
#include <stdio.h>
#include <unistd.h>

#include "jni.h"
#include "jni_util.h"
#include "jdk_internal_io_JdkConsoleImpl.h"

JNIEXPORT jboolean JNICALL
Java_jdk_internal_io_JdkConsoleImpl_echo(JNIEnv *env, jclass cls, jboolean on)
{
    (void)cls;
    (void)on;
    int is_tty = isatty(fileno(stdin));

    /* TTY detection is provided separately by the Unix Console native code
     * using newlib isatty(). Horizon has no termios echo control, so never
     * claim that password input has had echo disabled or restored.
     */
    errno = is_tty ? ENOTSUP : ENOTTY;
    JNU_ThrowIOExceptionWithLastError(env,
        is_tty
            ? "Horizon terminal echo control is unsupported"
            : "Standard input is not a terminal");
    return JNI_FALSE;
}
