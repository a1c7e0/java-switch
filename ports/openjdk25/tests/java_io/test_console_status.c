/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>

#include "jni.h"
#include "java_io_Console.h"

static unsigned tty_descriptors;

int isatty(int fd) {
    return fd >= 0 && fd < 3 && ((tty_descriptors >> fd) & 1u) != 0;
}

int main(void) {
    tty_descriptors = 0;
    assert(Java_java_io_Console_ttyStatus(NULL, NULL) == 0);

    tty_descriptors = (1u << fileno(stdin)) | (1u << fileno(stderr));
    assert(Java_java_io_Console_ttyStatus(NULL, NULL) ==
           (java_io_Console_TTY_STDIN_MASK | java_io_Console_TTY_STDERR_MASK));
    return 0;
}
