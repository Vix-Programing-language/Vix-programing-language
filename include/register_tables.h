#ifndef REGISTER_TABLES_H
#define REGISTER_TABLES_H

#include "import.h"
#include "third-party/khashl.h"



typedef struct RegisterEntry RegisterEntry;
typedef struct { uint32_t id; uint16_t file_id; uint16_t kind; } EntityID;

typedef struct {
    uint32_t* ids;
    size_t count;
    size_t cap;
} MethodIdList;

static inline khint_t string_hash(StringView v) { return kh_hash_bytes((int)v.len, (const unsigned char*)v.ptr); }
static inline int string_eq(StringView a, StringView b) { return a.len == b.len && memcmp(a.ptr, b.ptr, a.len) == 0; }


KHASHL_MAP_INIT(KH_LOCAL, RegisterTable, register_table, StringView, RegisterEntry*, string_hash, string_eq)
KHASHL_MAP_INIT(KH_LOCAL, PendingTable,  pending_table, StringView, EntityID, string_hash, string_eq)
KHASHL_MAP_INIT(KH_LOCAL, IdTable, id_table, uint32_t, RegisterEntry*, kh_hash_uint32, kh_eq_generic)
KHASHL_MAP_INIT(KH_LOCAL, GenericOriginalsTable, generic_originals_table, StringView, RegisterEntry*, string_hash, string_eq)
KHASHL_MAP_INIT(KH_LOCAL, ClassMethodTable, class_method_table, StringView, MethodIdList*, string_hash, string_eq)

#endif