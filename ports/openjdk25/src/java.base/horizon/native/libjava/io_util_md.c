/*
 * Copyright (c) 2001, 2024, Oracle and/or its affiliates. All rights reserved.
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
#include "jni.h"
#include "jni_util.h"
#include "jvm.h"
#include "io_util.h"
#include "io_util_md.h"
#include <string.h>
#include <unistd.h>

#if defined(__linux__) || defined(_ALLBSD_SOURCE) || defined(_AIX)
#include <sys/ioctl.h>
#endif

#if defined(__linux__)
#include <linux/fs.h>
#include <sys/stat.h>
#endif

static void
file_cursor_unlock_preserving_errno(void)
{
    int saved_errno = errno;
    js_horizon_file_cursor_unlock();
    errno = saved_errno;
}

off_t
horizon_file_lseek(FD fd, off_t offset, int whence)
{
    if (js_horizon_file_cursor_lock() != 0) return (off_t)-1;
    off_t result = lseek(fd, offset, whence);
    file_cursor_unlock_preserving_errno();
    return result;
}

int
horizon_file_fsync(FD fd)
{
    if (js_horizon_file_cursor_lock() != 0) return -1;
    int result;
    RESTARTABLE(fsync(fd), result);
    file_cursor_unlock_preserving_errno();
    return result;
}

#ifdef MACOSX

#include <CoreFoundation/CoreFoundation.h>

__private_extern__
jstring newStringPlatform(JNIEnv *env, const char* str)
{
    jstring rv = NULL;
    CFMutableStringRef csref = CFStringCreateMutable(NULL, 0);
    if (csref == NULL) {
        JNU_ThrowOutOfMemoryError(env, "native heap");
    } else {
        CFStringAppendCString(csref, str, kCFStringEncodingUTF8);
        CFStringNormalize(csref, kCFStringNormalizationFormC);
        int clen = CFStringGetLength(csref);
        int ulen = (clen + 1) * 2;        // utf16 + zero padding
        char* chars = malloc(ulen);
        if (chars == NULL) {
            CFRelease(csref);
            JNU_ThrowOutOfMemoryError(env, "native heap");
        } else {
            if (CFStringGetCString(csref, chars, ulen, kCFStringEncodingUTF16)) {
                rv = (*env)->NewString(env, (jchar*)chars, clen);
            }
            free(chars);
            CFRelease(csref);
        }
    }
    return rv;
}
#endif

FD
handleOpen(const char *path, int oflag, int mode) {
    FD fd;
    RESTARTABLE(open(path, oflag, mode), fd);
    if (fd != -1) {
        struct stat buf;
        int result;
        RESTARTABLE(fstat(fd, &buf), result);
        if (result != -1) {
            if (S_ISDIR(buf.st_mode)) {
                close(fd);
                errno = EISDIR;
                fd = -1;
            }
        } else {
            int saved_errno = errno;
            close(fd);
            errno = saved_errno;
            fd = -1;
        }
    }
    return fd;
}

FD getFD(JNIEnv *env, jobject obj, jfieldID fid) {
  jobject fdo = (*env)->GetObjectField(env, obj, fid);
  if (fdo == NULL) {
    return -1;
  }
  return (*env)->GetIntField(env, fdo, IO_fd_fdID);
}

void
fileOpen(JNIEnv *env, jobject this, jstring path, jfieldID fid, int flags)
{
    WITH_PLATFORM_STRING(env, path, ps) {
        FD fd;

        /* Match Unix stream-open behavior for trailing separators while
         * retaining the slash that belongs to a Horizon device root.
         */
        char *native_path = (char *)ps;
        size_t root_length = native_path[0] == '/' ? 1 : 0;
        const char *colon = strchr(native_path, ':');
        if (colon != NULL && colon[1] == '/')
            root_length = (size_t)(colon - native_path) + 2;
        size_t path_length = strlen(native_path);
        while (path_length > root_length && native_path[path_length - 1] == '/')
            native_path[--path_length] = '\0';

        fd = handleOpen(native_path, flags, 0666);
        if (fd != -1) {
            jobject fdobj;
            jboolean append;
            fdobj = (*env)->GetObjectField(env, this, fid);
            if (fdobj != NULL) {
                // Set FD
                (*env)->SetIntField(env, fdobj, IO_fd_fdID, fd);
                append = (flags & O_APPEND) == 0 ? JNI_FALSE : JNI_TRUE;
                (*env)->SetBooleanField(env, fdobj, IO_append_fdID, append);
            }
        } else {
            throwFileNotFoundException(env, path);
        }
    } END_PLATFORM_STRING(env, ps);
}

// Function to close the fd held by this FileDescriptor and set fd to -1.
void
fileDescriptorClose(JNIEnv *env, jobject this)
{
    if (js_horizon_file_cursor_lock() != 0) {
        JNU_ThrowIOExceptionWithLastError(env, "File descriptor lock failed");
        return;
    }
    FD fd = (*env)->GetIntField(env, this, IO_fd_fdID);
    if ((*env)->ExceptionCheck(env)) {
        file_cursor_unlock_preserving_errno();
        return;
    }

    if (fd == -1) {
        file_cursor_unlock_preserving_errno();
        return;     // already closed and set to -1
    }

    /* Set the fd to -1 before closing it so that the timing window
     * of other threads using the wrong fd (closed but recycled fd,
     * that gets re-opened with some other filename) is reduced.
     * Practically the chance of its occurrence is low, however, we are
     * taking extra precaution over here.
     */
    (*env)->SetIntField(env, this, IO_fd_fdID, -1);
    if ((*env)->ExceptionCheck(env)) {
        file_cursor_unlock_preserving_errno();
        return;
    }
    /* Horizon has no /dev/null device. Close the actual descriptor, including
     * standard streams, instead of redirecting it to a nonexistent path.
     */
    int result = close(fd);
    int saved_errno = errno;
    js_horizon_file_cursor_unlock();
    errno = saved_errno;
    if (result == -1 && errno != EINTR) {
        JNU_ThrowIOExceptionWithLastError(env, "close failed");
    }
}

ssize_t
handleRead(FD fd, void *buf, jint len)
{
    if (js_horizon_file_cursor_lock() != 0) return -1;
    ssize_t result;
    RESTARTABLE(read(fd, buf, len), result);
    file_cursor_unlock_preserving_errno();
    return result;
}

ssize_t
handleWrite(FD fd, const void *buf, jint len)
{
    if (js_horizon_file_cursor_lock() != 0) return -1;
    ssize_t result;
    RESTARTABLE(write(fd, buf, len), result);
    file_cursor_unlock_preserving_errno();
    return result;
}

jint
handleAvailable(FD fd, jlong *pbytes)
{
    if (js_horizon_file_cursor_lock() != 0) return 0;
    struct stat buf;
    jlong current;

    int result;
    RESTARTABLE(fstat(fd, &buf), result);
    if (result == -1) {
        file_cursor_unlock_preserving_errno();
        return 0;
    }
    /* Horizon has no FIONREAD ioctl. Report remaining bytes only when the
     * descriptor is a seekable regular file; other stream types cannot be
     * queried accurately through newlib and therefore report an error.
     */
    if (!S_ISREG(buf.st_mode)) {
        errno = ESPIPE;
        file_cursor_unlock_preserving_errno();
        return 0;
    }

    current = lseek(fd, 0, SEEK_CUR);
    if (current == -1) {
        file_cursor_unlock_preserving_errno();
        return 0;
    }

    *pbytes = buf.st_size > current ? buf.st_size - current : 0;
    file_cursor_unlock_preserving_errno();
    return 1;
}

jint
handleSetLength(FD fd, jlong length)
{
    if (js_horizon_file_cursor_lock() != 0) return -1;
    int result;
    RESTARTABLE(ftruncate(fd, length), result);
    file_cursor_unlock_preserving_errno();
    return result;
}

jlong
handleGetLength(FD fd)
{
    if (js_horizon_file_cursor_lock() != 0) return -1;
    struct stat sb;
    int result;
    RESTARTABLE(fstat(fd, &sb), result);
    if (result < 0) {
        file_cursor_unlock_preserving_errno();
        return -1;
    }
#if defined(__linux__) && defined(BLKGETSIZE64)
    if (S_ISBLK(sb.st_mode)) {
        uint64_t size;
        if(ioctl(fd, BLKGETSIZE64, &size) < 0) {
            file_cursor_unlock_preserving_errno();
            return -1;
        }
        file_cursor_unlock_preserving_errno();
        return (jlong)size;
    }
#endif
    jlong length = sb.st_size;
    file_cursor_unlock_preserving_errno();
    return length;
}

jboolean
handleIsRegularFile(JNIEnv* env, FD fd)
{
    if (js_horizon_file_cursor_lock() != 0) {
        JNU_ThrowIOExceptionWithLastError(env, "File descriptor lock failed");
        return JNI_FALSE;
    }
    struct stat fbuf;
    int result = fstat(fd, &fbuf);
    int saved_errno = errno;
    js_horizon_file_cursor_unlock();
    errno = saved_errno;
    if (result == -1) {
        JNU_ThrowIOExceptionWithLastError(env, "fstat failed");
        return JNI_FALSE;
    }

    return S_ISREG(fbuf.st_mode) ? JNI_TRUE : JNI_FALSE;
}
