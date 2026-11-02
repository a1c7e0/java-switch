#pragma once
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#define MAP_PRIVATE 1
#define MAP_ANONYMOUS 2
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_FAILED ((void *)-1)
/* Immix is explicitly disabled for this diagnostic. Fail reservation honestly. */
static inline void *mmap(void *a,size_t n,int p,int f,int d,long o){errno=ENOSYS;return MAP_FAILED;}
static inline int mprotect(void *a,size_t n,int p){errno=ENOSYS;return -1;}
static inline int munmap(void *a,size_t n){errno=ENOSYS;return -1;}

#define MADV_DONTNEED 4
static inline int madvise(void *a,size_t n,int advice){errno=ENOSYS;return -1;}
