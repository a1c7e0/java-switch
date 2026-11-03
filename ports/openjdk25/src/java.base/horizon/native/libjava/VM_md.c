/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
#include "jni.h"
#include "jdk_internal_misc_VM.h"
#include <errno.h>

/* jdk.internal.misc.VM explicitly specifies -1 when a Unix user/group identity
 * is unavailable. Horizon has no corresponding Unix identity domain. */
#define UNAVAILABLE_ID(name) \
JNIEXPORT jlong JNICALL Java_jdk_internal_misc_VM_##name(JNIEnv *env, jclass cls) { \
    (void)env; (void)cls; errno = ENOTSUP; return -1; \
}
UNAVAILABLE_ID(getuid)
UNAVAILABLE_ID(geteuid)
UNAVAILABLE_ID(getgid)
UNAVAILABLE_ID(getegid)
