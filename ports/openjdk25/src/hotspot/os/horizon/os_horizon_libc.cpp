/*
 * Copyright (c) 1999, 2025, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
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
 *
 * Modified 2026-11-02: add libc-backed Horizon file, path, and calendar APIs.
 */

#include "runtime/os.hpp"
#include "jvm_io.h"
#include "utilities/globalDefinitions.hpp"
#include "utilities/ostream.hpp"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int os::open(const char* path, int oflag, int mode) {
  return ::open(path, oflag, mode);
}

int os::stat(const char* path, struct stat* sbuf) {
  return ::stat(path, sbuf);
}

DIR* os::opendir(const char* dirname) {
  return ::opendir(dirname);
}

int os::closedir(DIR* dirp) {
  return ::closedir(dirp);
}

FILE* os::fdopen(int fd, const char* mode) {
  return ::fdopen(fd, mode);
}

int os::get_fileno(FILE* fp) {
  return ::fileno(fp);
}

int os::fseek(FILE* file, int64_t offset, int whence) {
  return ::fseeko(file, static_cast<off_t>(offset), whence);
}

int64_t os::ftell(FILE* file) {
  return static_cast<int64_t>(::ftello(file));
}

int os::ftruncate(int fd, jlong length) {
  return ::ftruncate(fd, static_cast<off_t>(length));
}

void os::flockfile(FILE* fp) {
  ::flockfile(fp);
}

void os::funlockfile(FILE* fp) {
  ::funlockfile(fp);
}

ssize_t os::pd_write(int fd, const void* buf, size_t nBytes) {
  ssize_t result;
  do {
    result = ::write(fd, buf, nBytes);
  } while (result == -1 && errno == EINTR);
  return result;
}

int os::compare_file_modified_times(const char* file1, const char* file2) {
  struct stat first;
  struct stat second;
  if (::stat(file1, &first) != 0) return -1;
  if (::stat(file2, &second) != 0) return -1;

  if (first.st_mtim.tv_sec < second.st_mtim.tv_sec) return -1;
  if (first.st_mtim.tv_sec > second.st_mtim.tv_sec) return 1;
  if (first.st_mtim.tv_nsec < second.st_mtim.tv_nsec) return -1;
  if (first.st_mtim.tv_nsec > second.st_mtim.tv_nsec) return 1;
  return 0;
}

const char* os::get_current_directory(char* buf, size_t buflen) {
  return ::getcwd(buf, buflen);
}

char* os::native_path(char* path) {
  // Newlib's devoptab paths (including sdmc:/...) are already native paths.
  return path;
}

const char* os::file_separator() { return "/"; }
const char* os::path_separator() { return ";"; }
const char* os::line_separator() { return "\n"; }

struct tm* os::gmtime_pd(const time_t* clock, struct tm* result) {
  return ::gmtime_r(clock, result);
}

struct tm* os::localtime_pd(const time_t* clock, struct tm* result) {
  return ::localtime_r(clock, result);
}

char* os::local_time_string(char* buf, size_t buflen) {
  time_t now;
  struct tm local;
  if (::time(&now) == static_cast<time_t>(-1) ||
      ::localtime_r(&now, &local) == nullptr) {
    return nullptr;
  }
  jio_snprintf(buf, buflen, "%d-%02d-%02d %02d:%02d:%02d",
               local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
               local.tm_hour, local.tm_min, local.tm_sec);
  return buf;
}

void os::print_active_locale(outputStream* st) {
  st->print_cr("Active Locale:");
  const char* locale = ::setlocale(LC_ALL, nullptr);
  st->print_cr("  LC_ALL: %s", locale == nullptr ? "(unavailable)" : locale);
}
