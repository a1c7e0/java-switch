/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "jni.h"
#include "jni_util.h"
#include "jlong.h"
#include "sun_nio_fs_UnixNativeDispatcher.h"
#include "horizon_stat_snapshot.h"
#include "horizon_file_cursor.h"

static jfieldID attrs_st_mode;
static jfieldID attrs_st_ino;
static jfieldID attrs_st_dev;
static jfieldID attrs_st_rdev;
static jfieldID attrs_st_nlink;
static jfieldID attrs_st_uid;
static jfieldID attrs_st_gid;
static jfieldID attrs_st_size;
static jfieldID attrs_st_atime_sec;
static jfieldID attrs_st_atime_nsec;
static jfieldID attrs_st_mtime_sec;
static jfieldID attrs_st_mtime_nsec;
static jfieldID attrs_st_ctime_sec;
static jfieldID attrs_st_ctime_nsec;
static jfieldID attrs_birthtime_available;
static jfieldID store_frsize;
static jfieldID store_blocks;
static jfieldID store_bfree;
static jfieldID store_bavail;

static void throw_unix_exception(JNIEnv *env, int error) {
    jobject exception = JNU_NewObjectByName(env, "sun/nio/fs/UnixException",
                                             "(I)V", error);
    if (exception != NULL) {
        (*env)->Throw(env, exception);
    }
}

static jbyteArray bytes_from_string(JNIEnv *env, const char *text) {
    if (text == NULL) return NULL;
    size_t length = strlen(text);
    if (length > INT_MAX) {
        throw_unix_exception(env, EOVERFLOW);
        return NULL;
    }
    jbyteArray result = (*env)->NewByteArray(env, (jsize)length);
    if (result != NULL && length != 0) {
        (*env)->SetByteArrayRegion(env, result, 0, (jsize)length,
                                   (const jbyte *)text);
    }
    return result;
}

static void copy_stat(JNIEnv *env, const struct stat *st, jobject attrs) {
    struct horizon_stat_snapshot values = horizon_stat_snapshot_from_stat(st);
    (*env)->SetIntField(env, attrs, attrs_st_mode, (jint)values.mode);
    (*env)->SetLongField(env, attrs, attrs_st_ino, (jlong)values.inode);
    (*env)->SetLongField(env, attrs, attrs_st_dev, (jlong)values.device);
    (*env)->SetLongField(env, attrs, attrs_st_rdev, (jlong)values.special_device);
    (*env)->SetIntField(env, attrs, attrs_st_nlink, (jint)values.nlink);
    (*env)->SetIntField(env, attrs, attrs_st_uid, (jint)values.uid);
    (*env)->SetIntField(env, attrs, attrs_st_gid, (jint)values.gid);
    (*env)->SetLongField(env, attrs, attrs_st_size, (jlong)values.size);
    (*env)->SetLongField(env, attrs, attrs_st_atime_sec,
                         (jlong)values.atime_seconds);
    (*env)->SetLongField(env, attrs, attrs_st_atime_nsec,
                         (jlong)values.atime_nanoseconds);
    (*env)->SetLongField(env, attrs, attrs_st_mtime_sec,
                         (jlong)values.mtime_seconds);
    (*env)->SetLongField(env, attrs, attrs_st_mtime_nsec,
                         (jlong)values.mtime_nanoseconds);
    (*env)->SetLongField(env, attrs, attrs_st_ctime_sec,
                         (jlong)values.ctime_seconds);
    (*env)->SetLongField(env, attrs, attrs_st_ctime_nsec,
                         (jlong)values.ctime_nanoseconds);
    (*env)->SetBooleanField(env, attrs, attrs_birthtime_available, JNI_FALSE);
}

JNIEXPORT jint JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_init(JNIEnv *env, jclass cls) {
    (void)cls;
    jclass attrs = (*env)->FindClass(env, "sun/nio/fs/UnixFileAttributes");
    if (attrs == NULL) return 0;
#define FIELD(dst, name, sig) do { \
    (dst) = (*env)->GetFieldID(env, attrs, (name), (sig)); \
    if ((dst) == NULL) return 0; \
} while (0)
    FIELD(attrs_st_mode, "st_mode", "I");
    FIELD(attrs_st_ino, "st_ino", "J");
    FIELD(attrs_st_dev, "st_dev", "J");
    FIELD(attrs_st_rdev, "st_rdev", "J");
    FIELD(attrs_st_nlink, "st_nlink", "I");
    FIELD(attrs_st_uid, "st_uid", "I");
    FIELD(attrs_st_gid, "st_gid", "I");
    FIELD(attrs_st_size, "st_size", "J");
    FIELD(attrs_st_atime_sec, "st_atime_sec", "J");
    FIELD(attrs_st_atime_nsec, "st_atime_nsec", "J");
    FIELD(attrs_st_mtime_sec, "st_mtime_sec", "J");
    FIELD(attrs_st_mtime_nsec, "st_mtime_nsec", "J");
    FIELD(attrs_st_ctime_sec, "st_ctime_sec", "J");
    FIELD(attrs_st_ctime_nsec, "st_ctime_nsec", "J");
    FIELD(attrs_birthtime_available, "birthtime_available", "Z");
#undef FIELD

    jclass store = (*env)->FindClass(env, "sun/nio/fs/UnixFileStoreAttributes");
    if (store == NULL) return 0;
#define STORE_FIELD(dst, name) do { \
    (dst) = (*env)->GetFieldID(env, store, (name), "J"); \
    if ((dst) == NULL) return 0; \
} while (0)
    STORE_FIELD(store_frsize, "f_frsize");
    STORE_FIELD(store_blocks, "f_blocks");
    STORE_FIELD(store_bfree, "f_bfree");
    STORE_FIELD(store_bavail, "f_bavail");
#undef STORE_FIELD

    /* The linked newlib has no openat/fstatat/unlinkat/renameat family. */
    return 0;
}

JNIEXPORT jboolean JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_fchmodatNoFollowSupported0(
        JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    return JNI_FALSE;
}

JNIEXPORT jbyteArray JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_getcwd(JNIEnv *env, jclass cls) {
    (void)cls;
    char path[PATH_MAX + 1];
    if (getcwd(path, sizeof(path)) == NULL) {
        throw_unix_exception(env, errno);
        return NULL;
    }
    return bytes_from_string(env, path);
}

JNIEXPORT jint JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_dup(JNIEnv *env, jclass cls, jint fd) {
    (void)cls;
    int result;
    do { result = dup(fd); } while (result < 0 && errno == EINTR);
    if (result < 0) throw_unix_exception(env, errno);
    return (jint)result;
}

JNIEXPORT jint JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_open0(JNIEnv *env, jclass cls,
                                           jlong path_address, jint flags,
                                           jint mode) {
    (void)cls;
    const char *path = jlong_to_ptr(path_address);
    int fd;
    do { fd = open(path, flags, (mode_t)mode); }
    while (fd < 0 && errno == EINTR);
    if (fd < 0) throw_unix_exception(env, errno);
    return fd;
}

JNIEXPORT jint JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_openat0(JNIEnv *env, jclass cls,
                                             jint dirfd, jlong path,
                                             jint flags, jint mode) {
    (void)cls; (void)dirfd; (void)path; (void)flags; (void)mode;
    throw_unix_exception(env, ENOTSUP);
    return -1;
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_close0(JNIEnv *env, jclass cls, jint fd) {
    (void)cls;
    if (close(fd) < 0 && errno != EINTR) throw_unix_exception(env, errno);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_rewind(JNIEnv *env, jclass cls,
                                            jlong stream) {
    (void)cls;
    FILE *file = jlong_to_ptr(stream);
    clearerr(file);
    if (fseek(file, 0, SEEK_SET) != 0) throw_unix_exception(env, errno);
}

JNIEXPORT jint JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_getlinelen(JNIEnv *env, jclass cls,
                                                jlong stream) {
    (void)cls;
    FILE *file = jlong_to_ptr(stream);
    size_t length = 0;
    int c;
    errno = 0;
    while ((c = fgetc(file)) != EOF) {
        if (length == INT_MAX) {
            throw_unix_exception(env, EOVERFLOW);
            return -1;
        }
        length++;
        if (c == '\n') break;
    }
    if (c == EOF) {
        if (ferror(file)) throw_unix_exception(env, errno != 0 ? errno : EIO);
        return length == 0 ? -1 : (jint)length;
    }
    return (jint)length;
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_link0(JNIEnv *env, jclass cls,
                                           jlong old_path, jlong new_path) {
    (void)cls;
    if (link(jlong_to_ptr(old_path), jlong_to_ptr(new_path)) < 0)
        throw_unix_exception(env, errno);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_unlink0(JNIEnv *env, jclass cls,
                                             jlong path) {
    (void)cls;
    if (unlink(jlong_to_ptr(path)) < 0) throw_unix_exception(env, errno);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_unlinkat0(JNIEnv *env, jclass cls,
                                               jint dirfd, jlong path,
                                               jint flags) {
    (void)cls; (void)dirfd; (void)path; (void)flags;
    throw_unix_exception(env, ENOTSUP);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_mknod0(JNIEnv *env, jclass cls,
                                            jlong path, jint mode,
                                            jlong device) {
    (void)cls; (void)path; (void)mode; (void)device;
    throw_unix_exception(env, ENOTSUP);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_rename0(JNIEnv *env, jclass cls,
                                             jlong from, jlong to) {
    (void)cls;
    if (rename(jlong_to_ptr(from), jlong_to_ptr(to)) < 0)
        throw_unix_exception(env, errno);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_renameat0(JNIEnv *env, jclass cls,
                                               jint from_fd, jlong from,
                                               jint to_fd, jlong to) {
    (void)cls; (void)from_fd; (void)from; (void)to_fd; (void)to;
    throw_unix_exception(env, ENOTSUP);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_mkdir0(JNIEnv *env, jclass cls,
                                            jlong path, jint mode) {
    (void)cls;
    if (mkdir(jlong_to_ptr(path), (mode_t)mode) < 0)
        throw_unix_exception(env, errno);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_rmdir0(JNIEnv *env, jclass cls,
                                            jlong path) {
    (void)cls;
    if (rmdir(jlong_to_ptr(path)) < 0) throw_unix_exception(env, errno);
}

JNIEXPORT jbyteArray JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_readlink0(JNIEnv *env, jclass cls,
                                               jlong path) {
    (void)cls; (void)path;
    throw_unix_exception(env, ENOTSUP);
    return NULL;
}

JNIEXPORT jbyteArray JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_realpath0(JNIEnv *env, jclass cls,
                                               jlong path_address) {
    (void)cls;
    char resolved[PATH_MAX + 1];
    if (realpath(jlong_to_ptr(path_address), resolved) == NULL) {
        throw_unix_exception(env, errno);
        return NULL;
    }
    return bytes_from_string(env, resolved);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_symlink0(JNIEnv *env, jclass cls,
                                              jlong target, jlong link_path) {
    (void)cls; (void)target; (void)link_path;
    throw_unix_exception(env, ENOTSUP);
}

JNIEXPORT jint JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_stat0(JNIEnv *env, jclass cls,
                                           jlong path, jobject attrs) {
    (void)cls;
    struct stat st;
    if (stat(jlong_to_ptr(path), &st) < 0) return errno;
    copy_stat(env, &st, attrs);
    return 0;
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_lstat0(JNIEnv *env, jclass cls,
                                            jlong path, jobject attrs) {
    (void)cls;
    struct stat st;
    if (lstat(jlong_to_ptr(path), &st) < 0) {
        throw_unix_exception(env, errno);
        return;
    }
    copy_stat(env, &st, attrs);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_fstat0(JNIEnv *env, jclass cls, jint fd,
                                            jobject attrs) {
    (void)cls;
    struct stat st;
    if (fstat(fd, &st) < 0) {
        throw_unix_exception(env, errno);
        return;
    }
    copy_stat(env, &st, attrs);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_fstatat0(JNIEnv *env, jclass cls,
                                              jint dirfd, jlong path,
                                              jint flags, jobject attrs) {
    (void)cls; (void)dirfd; (void)path; (void)flags; (void)attrs;
    throw_unix_exception(env, ENOTSUP);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_chown0(JNIEnv *env, jclass cls,
                                            jlong path, jint uid, jint gid) {
    (void)cls; (void)path; (void)uid; (void)gid;
    throw_unix_exception(env, ENOTSUP);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_lchown0(JNIEnv *env, jclass cls,
                                             jlong path, jint uid, jint gid) {
    (void)cls; (void)path; (void)uid; (void)gid;
    throw_unix_exception(env, ENOTSUP);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_fchown0(JNIEnv *env, jclass cls,
                                             jint fd, jint uid, jint gid) {
    (void)cls; (void)fd; (void)uid; (void)gid;
    throw_unix_exception(env, ENOTSUP);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_chmod0(JNIEnv *env, jclass cls,
                                            jlong path, jint mode) {
    (void)cls;
    if (chmod(jlong_to_ptr(path), (mode_t)mode) < 0)
        throw_unix_exception(env, errno);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_fchmod0(JNIEnv *env, jclass cls,
                                             jint fd, jint mode) {
    (void)cls;
    if (fchmod(fd, (mode_t)mode) < 0) throw_unix_exception(env, errno);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_fchmodat0(JNIEnv *env, jclass cls,
                                               jint fd, jlong path,
                                               jint mode, jint flags) {
    (void)cls; (void)fd; (void)path; (void)mode; (void)flags;
    throw_unix_exception(env, ENOTSUP);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_futimens0(JNIEnv *env, jclass cls,
                                               jint fd, jlong atime,
                                               jlong mtime) {
    (void)cls; (void)fd; (void)atime; (void)mtime;
    throw_unix_exception(env, ENOTSUP);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_utimensat0(JNIEnv *env, jclass cls,
                                                jint fd, jlong path,
                                                jlong atime, jlong mtime,
                                                jint flags) {
    (void)cls; (void)fd; (void)path; (void)atime; (void)mtime; (void)flags;
    throw_unix_exception(env, ENOTSUP);
}

JNIEXPORT jlong JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_opendir0(JNIEnv *env, jclass cls,
                                              jlong path) {
    (void)cls;
    DIR *directory = opendir(jlong_to_ptr(path));
    if (directory == NULL) {
        throw_unix_exception(env, errno);
        return (jlong)-1;
    }
    return ptr_to_jlong(directory);
}

JNIEXPORT jlong JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_fdopendir(JNIEnv *env, jclass cls,
                                               jint fd) {
    (void)cls; (void)fd;
    throw_unix_exception(env, ENOTSUP);
    return (jlong)-1;
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_closedir(JNIEnv *env, jclass cls,
                                              jlong dir) {
    (void)cls;
    if (closedir(jlong_to_ptr(dir)) < 0 && errno != EINTR)
        throw_unix_exception(env, errno);
}

JNIEXPORT jbyteArray JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_readdir0(JNIEnv *env, jclass cls,
                                              jlong dir) {
    (void)cls;
    errno = 0;
    struct dirent *entry = readdir(jlong_to_ptr(dir));
    if (entry == NULL) {
        if (errno != 0) throw_unix_exception(env, errno);
        return NULL;
    }
    return bytes_from_string(env, entry->d_name);
}

JNIEXPORT jint JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_read0(JNIEnv *env, jclass cls, jint fd,
                                           jlong buffer, jint length) {
    (void)cls;
    if (length < 0) {
        throw_unix_exception(env, EINVAL);
        return -1;
    }
    if (js_horizon_file_cursor_lock() < 0) {
        throw_unix_exception(env, errno);
        return -1;
    }
    ssize_t result;
    do { result = read(fd, jlong_to_ptr(buffer), (size_t)length); }
    while (result < 0 && errno == EINTR);
    int error = errno;
    js_horizon_file_cursor_unlock();
    if (result < 0) throw_unix_exception(env, error);
    return (jint)result;
}

JNIEXPORT jint JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_write0(JNIEnv *env, jclass cls, jint fd,
                                            jlong buffer, jint length) {
    (void)cls;
    if (length < 0) {
        throw_unix_exception(env, EINVAL);
        return -1;
    }
    if (js_horizon_file_cursor_lock() < 0) {
        throw_unix_exception(env, errno);
        return -1;
    }
    ssize_t result;
    do { result = write(fd, jlong_to_ptr(buffer), (size_t)length); }
    while (result < 0 && errno == EINTR);
    int error = errno;
    js_horizon_file_cursor_unlock();
    if (result < 0) throw_unix_exception(env, error);
    return (jint)result;
}

JNIEXPORT jint JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_access0(JNIEnv *env, jclass cls,
                                             jlong path, jint mode) {
    (void)env; (void)cls;
    return access(jlong_to_ptr(path), mode) == 0 ? 0 : errno;
}

JNIEXPORT jbyteArray JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_getpwuid(JNIEnv *env, jclass cls,
                                              jint uid) {
    (void)cls; (void)uid;
    throw_unix_exception(env, ENOTSUP);
    return NULL;
}

JNIEXPORT jbyteArray JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_getgrgid(JNIEnv *env, jclass cls,
                                              jint gid) {
    (void)cls; (void)gid;
    throw_unix_exception(env, ENOTSUP);
    return NULL;
}

JNIEXPORT jint JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_getpwnam0(JNIEnv *env, jclass cls,
                                               jlong name) {
    (void)cls; (void)name;
    throw_unix_exception(env, ENOTSUP);
    return -1;
}

JNIEXPORT jint JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_getgrnam0(JNIEnv *env, jclass cls,
                                               jlong name) {
    (void)cls; (void)name;
    throw_unix_exception(env, ENOTSUP);
    return -1;
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_statvfs0(JNIEnv *env, jclass cls,
                                              jlong path, jobject attrs) {
    (void)cls;
    struct statvfs st;
    if (statvfs(jlong_to_ptr(path), &st) < 0) {
        throw_unix_exception(env, errno);
        return;
    }
    (*env)->SetLongField(env, attrs, store_frsize, (jlong)st.f_frsize);
    (*env)->SetLongField(env, attrs, store_blocks, (jlong)st.f_blocks);
    (*env)->SetLongField(env, attrs, store_bfree, (jlong)st.f_bfree);
    (*env)->SetLongField(env, attrs, store_bavail, (jlong)st.f_bavail);
}

JNIEXPORT jbyteArray JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_strerror(JNIEnv *env, jclass cls,
                                              jint error) {
    (void)cls;
    return bytes_from_string(env, strerror(error));
}

JNIEXPORT jint JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_fgetxattr0(JNIEnv *env, jclass cls,
                                                jint fd, jlong name,
                                                jlong value, jint size) {
    (void)cls; (void)fd; (void)name; (void)value; (void)size;
    throw_unix_exception(env, ENOTSUP);
    return -1;
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_fsetxattr0(JNIEnv *env, jclass cls,
                                                jint fd, jlong name,
                                                jlong value, jint size) {
    (void)cls; (void)fd; (void)name; (void)value; (void)size;
    throw_unix_exception(env, ENOTSUP);
}

JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_fremovexattr0(JNIEnv *env, jclass cls,
                                                   jint fd, jlong name) {
    (void)cls; (void)fd; (void)name;
    throw_unix_exception(env, ENOTSUP);
}

JNIEXPORT jint JNICALL
Java_sun_nio_fs_UnixNativeDispatcher_flistxattr(JNIEnv *env, jclass cls,
                                                jint fd, jlong list,
                                                jint size) {
    (void)cls; (void)fd; (void)list; (void)size;
    throw_unix_exception(env, ENOTSUP);
    return -1;
}
