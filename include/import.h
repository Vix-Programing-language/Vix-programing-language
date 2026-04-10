#define _GNU_SOURCE
#ifndef VIX_IMPORT_H
#define VIX_IMPORT_H

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void* checked_malloc(size_t size) {
    void* ptr = malloc(size);
    assert(ptr != NULL);
    return ptr;
}

static inline void* checked_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    assert(new_ptr != NULL);
    return new_ptr;
}

#define TODO \
do { \
    fprintf(stderr, "TODO hit at %s:%d\n", __FILE__, __LINE__); \
    abort(); \
} while (0);

#if defined(__GNUC__) || defined(__clang__)
    #define COMPILER_UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
    #define COMPILER_UNREACHABLE() __assume(0)
#else
    #define COMPILER_UNREACHABLE() ((void)0)
#endif


#ifndef NDEBUG
    #define ASSERT(x) assert(x)
    #define UNREACHABLE() assert(!"UNREACHABLE")
#else
    #define ASSERT(x) \
        do { if (!(x)) COMPILER_UNREACHABLE(); } while (0)

    #define UNREACHABLE() COMPILER_UNREACHABLE()
#endif


static inline void arr__ensure_cap_impl(
    void **data,
    size_t *cap,
    size_t need,
    size_t elem_size
) {
    if (*cap >= need) return;

    size_t new_cap = *cap ? *cap : 8;
    while (new_cap < need) {
        new_cap *= 2;
    }

    *data = checked_realloc(*data, new_cap * elem_size);
    *cap = new_cap;
}

#define ARR(T) struct { T* data; size_t len; size_t cap; }

#define ARR_POP(arr) \
    ((arr).data[(ASSERT((arr).len), --(arr).len)])

#define ARR_PEEK(arr) \
    ((arr).data[(ASSERT((arr).len), (arr).len - 1)])

#define ARR_AT(arr, i) \
    ((arr).data[(ASSERT((size_t)(i) < (size_t)((arr).len)), (size_t)(i))])

#define ARR_REMOVE_UNORDERED(arr, i) \
    (ARR_AT((arr), (i)) = ARR_PEEK(arr), (arr).data[--(arr).len])

#define ARR_ENSURE_CAP(arr, need) \
    ((void)arr__ensure_cap_impl( \
        (void**)&((arr).data), \
        &((arr).cap), \
        (size_t)(need), \
        sizeof(*(arr).data) \
    ))

#define ARR_MAKE_ROOM(arr, extra) \
    ARR_ENSURE_CAP((arr), (arr).len + (size_t)(extra))

#define ARR_EXTEND(arr, src, n) \
    ( \
        ARR_MAKE_ROOM((arr), (n)), \
        memcpy( \
            (arr).data + (arr).len, \
            (src), \
            (size_t)(n) * sizeof(*(arr).data) \
        ), \
        (arr).len += (size_t)(n) \
    )

#define ARR_PUSH(arr, x) \
    ( \
        ARR_MAKE_ROOM((arr), 1), \
        (arr).data[(arr).len] = (x), \
        (arr).len++ \
    )
#endif
