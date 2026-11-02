/*
 * Modified 2026-11-02 for the experimental Horizon/libnx port.
 * Copyright (c) 1997, 2024, Oracle and/or its affiliates. All rights reserved.
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

#ifndef OS_HORIZON_JVM_MD_H
#define OS_HORIZON_JVM_MD_H

// Horizon uses newlib file types. These declarations do not supply POSIX
// signal delivery, shared objects, mmap, or native method lookup.
#include <dirent.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#define JNI_LIB_PREFIX "lib"
#define JNI_LIB_SUFFIX ".a"
#define JNI_LIB_NAME(NAME) JNI_LIB_PREFIX NAME JNI_LIB_SUFFIX
#define VERSIONED_JNI_LIB_NAME(NAME, VERSION) JNI_LIB_NAME(NAME)
#define JVM_MAXPATHLEN 4096
#define JVM_R_OK R_OK
#define JVM_W_OK W_OK
#define JVM_X_OK X_OK
#define JVM_F_OK F_OK

// API constants only. No Horizon signal dispatcher is implemented.
#define JVM_SIGINT SIGINT
#define JVM_SIGTERM SIGTERM
#define BREAK_SIGNAL SIGQUIT
#define SHUTDOWN1_SIGNAL SIGHUP
#define SHUTDOWN2_SIGNAL SIGINT
#define SHUTDOWN3_SIGNAL SIGTERM
#endif
