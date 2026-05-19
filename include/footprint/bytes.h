#pragma once
#include "import.h"

typedef ARR(uint8_t) ByteBuf;

static inline void bb_push_raw(ByteBuf *b, const void *data, size_t n) {
    ARR_MAKE_ROOM(*b, n);
    memcpy(b->data + b->len, data, n);
    b->len += n;
}

static inline void bb_u8 (ByteBuf *b, uint8_t  v) { bb_push_raw(b, &v, 1); }
static inline void bb_u16(ByteBuf *b, uint16_t v) { bb_push_raw(b, &v, 2); }
static inline void bb_u32(ByteBuf *b, uint32_t v) { bb_push_raw(b, &v, 4); }
static inline void bb_u64(ByteBuf *b, uint64_t v) { bb_push_raw(b, &v, 8); }

static inline void bb_str(ByteBuf *b, const char *s) {
    uint16_t len = s ? (uint16_t)strlen(s) : 0;
    bb_u16(b, len);
    if (len) bb_push_raw(b, s, len);
}

static inline void bb_free(ByteBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

typedef struct {
    const uint8_t *buf;
    size_t         len;
    size_t         pos;
    bool           ok;
} ByteReader;

static inline ByteReader br_make(const uint8_t *buf, size_t len) {
    return (ByteReader){ .buf = buf, .len = len, .pos = 0, .ok = true };
}

static inline bool br_read(ByteReader *r, void *out, size_t n) {
    if (!r->ok || r->pos + n > r->len) { r->ok = false; return false; }
    memcpy(out, r->buf + r->pos, n);
    r->pos += n;
    return true;
}

static inline uint8_t  br_u8 (ByteReader *r) { uint8_t  v=0; br_read(r,&v,1); return v; }
static inline uint16_t br_u16(ByteReader *r) { uint16_t v=0; br_read(r,&v,2); return v; }
static inline uint32_t br_u32(ByteReader *r) { uint32_t v=0; br_read(r,&v,4); return v; }
static inline uint64_t br_u64(ByteReader *r) { uint64_t v=0; br_read(r,&v,8); return v; }

static inline char *br_str(ByteReader *r) {
    uint16_t len = br_u16(r);
    if (!r->ok || len == 0) return NULL;
    if (r->pos + len > r->len) { r->ok = false; return NULL; }
    char *s = (char *)malloc(len + 1);
    memcpy(s, r->buf + r->pos, len);
    s[len] = '\0';
    r->pos += len;
    return s;
}