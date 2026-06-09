#pragma once
#include <stddef.h>
#include <io.h>
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef long long ssize_t;
#endif

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

static inline ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        int r = _write(fd, iov[i].iov_base, (unsigned)iov[i].iov_len);
        if (r < 0) return -1;
        total += r;
    }
    return total;
}

static inline ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        int r = _read(fd, iov[i].iov_base, (unsigned)iov[i].iov_len);
        if (r < 0) return -1;
        total += r;
    }
    return total;
}