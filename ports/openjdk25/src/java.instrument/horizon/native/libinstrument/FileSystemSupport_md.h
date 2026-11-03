/*
 * Copyright (c) 2004, 2008, Oracle and/or its affiliates. All rights reserved.
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
 */

#ifndef HORIZON_INSTRUMENT_FILESYSTEM_SUPPORT_MD_H
#define HORIZON_INSTRUMENT_FILESYSTEM_SUPPORT_MD_H

#include <stdint.h>
#include <stdlib.h>

/* Keep the buffer size tied to the value exported by this VM's OS headers. */
#include "jvm_md.h"
#define MAXPATHLEN JVM_MAXPATHLEN

/* A colon belongs to Horizon device roots (for example sdmc:/), so it cannot
 * also separate a list of paths passed to native library code. */
#define pathSeparator ';'

#endif
