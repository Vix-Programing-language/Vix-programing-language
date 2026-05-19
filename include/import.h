#ifndef VIX_IMPORT_H
#define VIX_IMPORT_H

#ifndef _WIN32
    #define _GNU_SOURCE
    #define _POSIX_C_SOURCE 200809L
#endif

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
#include <direct.h>
#include "sys/uio.h"

#define PATH_SEP "\\"
#define VIX_MKDIR(path) _mkdir(path)

#ifndef ssize_t
    typedef long long ssize_t;
#endif

#define open(path, flags, ...)  _open((path), (flags)|0x8000, ##__VA_ARGS__)
#define read(fd, buf, n)        _read((fd), (buf), (unsigned int)(n))
#define write(fd, buf, n)       _write((fd), (buf), (unsigned int)(n))
#define close(fd)               _close(fd)
#define lseek(fd, off, whence)  _lseeki64((fd), (off), (whence))

#else
#   include <unistd.h>
#   include <sys/uio.h>
#   include <sys/types.h>
#   include <dirent.h>
    
#   define PATH_SEP "/"
#   define VIX_MKDIR(path) mkdir((path), 0755)
#endif

typedef struct {
    const char* ptr;
    size_t      len;
} StringView;

#define SV(s) (StringView){(s), strlen(s)}

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

#ifndef HAVE_STRNDUP
static inline char* strndup(const char* s, size_t n) {
    if (!s) return NULL;
    size_t len = 0;
    while (len < n && s[len]) len++;
    char *new_str = (char *)malloc(len + 1);
    if (!new_str) return NULL;
    memcpy(new_str, s, len);
    new_str[len] = '\0';
    return new_str;
}
#endif


#define ARR(T) struct { T* data; size_t len; size_t cap; }

static inline void arr__ensure_cap_impl(void **data, size_t *cap, size_t need, size_t elem_size) {
    if (*cap >= need) return;
    size_t new_cap = *cap ? *cap : 8;
    while (new_cap < need) new_cap *= 2;
    *data = checked_realloc(*data, new_cap * elem_size);
    *cap = new_cap;
}

#define ARR_ENSURE_CAP(arr, need) \
    arr__ensure_cap_impl((void**)&((arr).data), &((arr).cap), (size_t)(need), sizeof(*(arr).data))

#define ARR_PUSH(arr, x) \
    (ARR_ENSURE_CAP(arr, (arr).len + 1), (arr).data[(arr).len++] = (x))

#define ARR_MAKE_ROOM(arr, extra) \
    ARR_ENSURE_CAP(arr, (arr).len + (size_t)(extra))

#define ARR_FREE(arr) \
    do { free((arr).data); (arr).data = NULL; (arr).len = (arr).cap = 0; } while(0)

#define ARR_CLEAR(arr) \
    ((arr).len = 0)

#define ARR_AT(arr, i) \
    ((arr).data[(ASSERT((size_t)(i) < (size_t)((arr).len)), (size_t)(i))])

#define ARR_POP(arr) \
    ((arr).data[(ASSERT((arr).len > 0), --(arr).len)])

#define ARR_PEEK(arr) \
    ((arr).data[(ASSERT((arr).len > 0), (arr).len - 1)])

#define TODO do { fprintf(stderr, "TODO: %s:%d\n", __FILE__, __LINE__); abort(); } while(0)

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
    #define ASSERT(x) do { if (!(x)) COMPILER_UNREACHABLE(); } while (0)
    #define UNREACHABLE() COMPILER_UNREACHABLE()
#endif

#endif // VIX_IMPORT_H