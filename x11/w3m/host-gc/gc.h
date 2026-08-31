#ifndef EXWORD_W3M_HOST_GC_SHIM_H
#define EXWORD_W3M_HOST_GC_SHIM_H

#include <stdlib.h>

/* mktable is a short-lived host generator; ordinary allocation is enough. */
#define GC_INIT() ((void)0)
#define GC_MALLOC(size) malloc(size)
#define GC_MALLOC_ATOMIC(size) malloc(size)
#define GC_REALLOC(ptr, size) realloc((ptr), (size))
#define GC_malloc(size) malloc(size)
#define GC_malloc_atomic(size) malloc(size)
#define GC_free(ptr) free(ptr)

#endif
