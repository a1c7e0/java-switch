/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OS_HORIZON_GLOBALS_HORIZON_HPP
#define OS_HORIZON_GLOBALS_HORIZON_HPP
// No Linux container/THP/perf flags and no Horizon-specific runtime flags yet.
#define RUNTIME_OS_FLAGS(develop, develop_pd, product, product_pd, range, constraint)
define_pd_global(size_t, PreTouchParallelChunkSize, 4 * M);
define_pd_global(bool, UseLargePages, false);
define_pd_global(bool, UseLargePagesIndividualAllocation, false);
define_pd_global(bool, UseThreadPriorities, false);
#endif
