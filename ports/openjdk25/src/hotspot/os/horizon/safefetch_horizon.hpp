/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OS_HORIZON_SAFEFETCH_HORIZON_HPP
#define OS_HORIZON_SAFEFETCH_HORIZON_HPP
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int SafeFetch32_impl(int* address, int fallback);
intptr_t SafeFetchN_impl(intptr_t* address, intptr_t fallback);
#ifdef __cplusplus
}
#endif
#endif
