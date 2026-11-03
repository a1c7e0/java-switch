/*
 * Copyright (c) 2015, Oracle and/or its affiliates. All rights reserved.
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
#include <fcntl.h>
#include <limits>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "jni.h"
#include "osSupport.hpp"
#include "horizon_bridge.h"
#include "horizon_memory_bridge.h"

/*
 * Horizon has no mmap. libjimage only needs a stable address for its index
 * (Horizon selects the upstream index-only path in patch 0015). This backend
 * copies those bytes into bridge-owned pages, then makes the pages read-only.
 * This is a private jimage snapshot with map/unmap ownership, not general mmap
 * semantics: later file changes are not reflected in an existing image.
 */

namespace {

const size_t horizon_page_size = 4096;

struct ImageSnapshot {
    ImageSnapshot* next;
    void* address;
    size_t reservation_size;
    size_t bytes;
};

SimpleCriticalSection file_io_lock;
SimpleCriticalSection snapshots_lock;
ImageSnapshot* snapshots;

static bool rounded_snapshot_size(size_t bytes, size_t* rounded) {
    if (bytes == 0 || bytes > SIZE_MAX - (horizon_page_size - 1)) {
        return false;
    }
    *rounded = (bytes + horizon_page_size - 1) & ~(horizon_page_size - 1);
    return true;
}

static off_t seek_retry(int fd, off_t offset, int whence) {
    off_t result;
    do {
        result = ::lseek(fd, offset, whence);
    } while (result < 0 && errno == EINTR);
    return result;
}

static void fail_closed_release(void* address, size_t size) {
    uint32_t raw_result = 0;
    js_horizon_memory_status status = js_horizon_memory_release(
        address, size, &raw_result);
    if (status != JS_HORIZON_MEMORY_OK) {
        fprintf(stderr,
                "Horizon libjimage: snapshot rollback failed (status=%u, Result=0x%08x)\n",
                static_cast<unsigned int>(status),
                static_cast<unsigned int>(raw_result));
        abort();
    }
}

}  // namespace

jint osSupport::openReadOnly(const char* path) {
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }
    SimpleCriticalSectionLock lock(&file_io_lock);
    jint fd;
    do {
        fd = ::open(path, O_RDONLY);
    } while (fd < 0 && errno == EINTR);
    return fd;
}

jint osSupport::close(jint fd) {
    SimpleCriticalSectionLock lock(&file_io_lock);
    return ::close(fd);
}

jlong osSupport::size(const char* path) {
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }
    struct stat statbuf;
    int result;
    do {
        result = ::stat(path, &statbuf);
    } while (result < 0 && errno == EINTR);
    if (result < 0 || !S_ISREG(statbuf.st_mode) ||
        statbuf.st_size < 0 ||
        static_cast<uintmax_t>(statbuf.st_size) >
            static_cast<uintmax_t>(std::numeric_limits<jlong>::max())) {
        return -1;
    }
    return static_cast<jlong>(statbuf.st_size);
}

/*
 * Newlib's Horizon descriptor API provides seek/read rather than pread.
 * Serialize the seek/read/restore sequence so libjimage reads do not alter the
 * descriptor cursor or race each other. The descriptor is private to an
 * ImageFileReader, as in the upstream implementation.
 */
jlong osSupport::read(jint fd, char* buf, jlong nBytes, jlong offset) {
    if (buf == NULL || nBytes < 0 || offset < 0 ||
        static_cast<uintmax_t>(nBytes) >
            static_cast<uintmax_t>(std::numeric_limits<size_t>::max()) ||
        static_cast<uintmax_t>(offset) >
            static_cast<uintmax_t>(std::numeric_limits<off_t>::max()) ||
        (nBytes > 0 && offset > std::numeric_limits<jlong>::max() - nBytes)) {
        errno = EINVAL;
        return -1;
    }
    if (nBytes == 0) return 0;

    SimpleCriticalSectionLock lock(&file_io_lock);
    const off_t saved_offset = seek_retry(fd, 0, SEEK_CUR);
    if (saved_offset < 0) return -1;
    if (static_cast<uintmax_t>(offset) >
        static_cast<uintmax_t>(std::numeric_limits<off_t>::max()) ||
        seek_retry(fd, static_cast<off_t>(offset), SEEK_SET) < 0) {
        const int saved_errno = errno;
        (void)seek_retry(fd, saved_offset, SEEK_SET);
        errno = saved_errno;
        return -1;
    }

    jlong total = 0;
    int read_error = 0;
    while (total < nBytes) {
        const size_t remaining = static_cast<size_t>(nBytes - total);
        const size_t chunk = remaining >
                static_cast<size_t>(std::numeric_limits<ssize_t>::max())
            ? static_cast<size_t>(std::numeric_limits<ssize_t>::max())
            : remaining;
        const ssize_t count = ::read(fd, buf + static_cast<size_t>(total), chunk);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            read_error = errno;
            break;
        }
        if (count == 0) break;
        total += static_cast<jlong>(count);
    }

    if (seek_retry(fd, saved_offset, SEEK_SET) < 0) return -1;
    if (read_error != 0) {
        errno = read_error;
        return -1;
    }
    return total;
}

void* osSupport::map_memory(jint fd, const char* filename,
                            size_t file_offset, size_t bytes) {
    (void)filename;
    size_t reservation_size;
    if (!rounded_snapshot_size(bytes, &reservation_size) ||
        bytes > static_cast<size_t>(std::numeric_limits<jlong>::max()) ||
        file_offset > static_cast<size_t>(std::numeric_limits<jlong>::max()) ||
        static_cast<jlong>(file_offset) >
            std::numeric_limits<jlong>::max() - static_cast<jlong>(bytes)) {
        errno = EINVAL;
        return NULL;
    }

    ImageSnapshot* snapshot = static_cast<ImageSnapshot*>(malloc(sizeof(*snapshot)));
    if (snapshot == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    uint32_t raw_result = 0;
    void* address = NULL;
    js_horizon_memory_status status = js_horizon_memory_reserve(
        reservation_size, horizon_page_size, &address, &raw_result);
    if (status != JS_HORIZON_MEMORY_OK) {
        free(snapshot);
        errno = status == JS_HORIZON_MEMORY_NO_MEMORY ? ENOMEM : EIO;
        return NULL;
    }
    status = js_horizon_memory_commit(address, reservation_size, &raw_result);
    if (status != JS_HORIZON_MEMORY_OK) {
        fail_closed_release(address, reservation_size);
        free(snapshot);
        errno = status == JS_HORIZON_MEMORY_NO_MEMORY ? ENOMEM : EIO;
        return NULL;
    }

    errno = 0;
    const jlong bytes_read = osSupport::read(
        fd, static_cast<char*>(address), static_cast<jlong>(bytes),
        static_cast<jlong>(file_offset));
    if (bytes_read != static_cast<jlong>(bytes)) {
        const int read_errno = errno;
        fail_closed_release(address, reservation_size);
        free(snapshot);
        errno = bytes_read >= 0 ? EIO : read_errno;
        return NULL;
    }

    status = js_horizon_memory_protect(address, reservation_size,
                                       JS_HORIZON_MEMORY_READ, &raw_result);
    if (status != JS_HORIZON_MEMORY_OK) {
        fail_closed_release(address, reservation_size);
        free(snapshot);
        errno = status == JS_HORIZON_MEMORY_UNSUPPORTED ? ENOTSUP : EACCES;
        return NULL;
    }

    snapshot->address = address;
    snapshot->reservation_size = reservation_size;
    snapshot->bytes = bytes;
    SimpleCriticalSectionLock lock(&snapshots_lock);
    snapshot->next = snapshots;
    snapshots = snapshot;
    return address;
}

int osSupport::unmap_memory(void* address, size_t bytes) {
    if (address == NULL) {
        errno = EINVAL;
        return 0;
    }
    SimpleCriticalSectionLock lock(&snapshots_lock);
    ImageSnapshot** link = &snapshots;
    while (*link != NULL && (*link)->address != address) {
        link = &(*link)->next;
    }
    if (*link == NULL) {
        errno = EINVAL;
        return 0;
    }

    ImageSnapshot* snapshot = *link;
    if (bytes != snapshot->bytes) {
        errno = EINVAL;
        return 0;
    }
    uint32_t raw_result = 0;
    js_horizon_memory_status status = js_horizon_memory_release(
        snapshot->address, snapshot->reservation_size, &raw_result);
    if (status != JS_HORIZON_MEMORY_OK) {
        fprintf(stderr,
                "Horizon libjimage: snapshot release failed; ownership retained (status=%u, Result=0x%08x)\n",
                static_cast<unsigned int>(status),
                static_cast<unsigned int>(raw_result));
        errno = EIO;
        return 0;
    }
    *link = snapshot->next;
    free(snapshot);
    return 1;
}

void SimpleCriticalSection::enter() {
    js_horizon_monitor_lock(
        static_cast<js_horizon_monitor*>(horizon_monitor));
}

void SimpleCriticalSection::exit() {
    js_horizon_monitor_unlock(
        static_cast<js_horizon_monitor*>(horizon_monitor));
}

SimpleCriticalSection::SimpleCriticalSection() {
    horizon_monitor = js_horizon_monitor_create();
    if (horizon_monitor == NULL) {
        fprintf(stderr, "Horizon libjimage: monitor allocation failed\n");
        abort();
    }
}
