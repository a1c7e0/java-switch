/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef JS_TEST_SWITCH_RESULT_H
#define JS_TEST_SWITCH_RESULT_H
#include <stdint.h>
typedef uint32_t Result;
enum { Module_Libnx = 345, LibnxError_OutOfMemory = 9 };
#define MAKERESULT(module, description) (((module) & 0x1ff) | (((description) & 0x1fff) << 9))
#define R_SUCCEEDED(result) ((result) == 0)
#define R_FAILED(result) ((result) != 0)
#endif
