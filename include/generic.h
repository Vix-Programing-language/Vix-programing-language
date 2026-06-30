#ifndef GENERIC_H
#define GENERIC_H

#include "third-party/khashl.h"
#include "register.h"
#include "codegen.h"
#include "import.h"



typedef ARR(uint32_t) UInt32Arr;

typedef struct {
    size_t param_count;
    size_t generic_count;
} GenericCount;

typedef struct {
    size_t arg_count;
    size_t generic_count;
} GenericArgCount;

typedef struct {
    size_t arg_count;
    size_t generic_count;
} GenericCallCount;

typedef struct {
    size_t match_count;
    size_t generic_count;
} GenericReturnCount;

khint_t sv_hash(StringView sv);
int sv_eq(StringView a, StringView b);

Type* set_generic_type(SourceRange* raw_args, size_t count) {
    if (count == 0) return NULL;

    Type* result = malloc(sizeof(Type) * count);
    for (size_t i = 0; i < count; i++) {
        result[i] = type_from_range(raw_args[i]);
    }
    return result;
}

KHASHL_MAP_INIT(static kh_inline, GenericArgTable, generic_args_table, StringView, GenericArgCount, sv_hash, sv_eq)
KHASHL_MAP_INIT(static kh_inline, GenericCountTable, generic_count_table, StringView, GenericCount, sv_hash, sv_eq)
KHASHL_MAP_INIT(static kh_inline, GenericReturnTable, generic_return_table, StringView, GenericReturnCount, sv_hash, sv_eq)
KHASHL_MAP_INIT(static kh_inline, GenericCallTable, generic_call_table, StringView, GenericCallCount, sv_hash, sv_eq)
KHASHL_MAP_INIT(static kh_inline, GenericSeenTable, generic_seen_table, StringView, uint32_t, sv_hash, sv_eq)


typedef ARR(size_t) size_Arr;

typedef struct {
    GenericCountTable*  param_table;
    GenericReturnTable* return_table;
    GenericCallTable*   call_table;
    GenericArgTable*    args_table;
    GenericSeenTable*   global_table;
} GenericTables;


GenericTables generic_new(void);
void generic_free(GenericTables* g);

void generic_param_register(GenericCountTable* table, StringView name);
void generic_param_mark(GenericCountTable* table, StringView name);
GenericCount generic_param_get(GenericCountTable* table, StringView name);

void generic_args_register(GenericArgTable* table, StringView name);
void generic_args_mark(GenericArgTable* table, StringView name);
GenericArgCount generic_args_get(GenericArgTable* table, StringView name);

void generic_call_register(GenericCallTable* table, StringView name);
void generic_call_mark(GenericCallTable* table, StringView name);
GenericCallCount generic_call_get(GenericCallTable* table, StringView name);

void generic_return_register(GenericReturnTable* table, StringView name);
void generic_return_mark(GenericReturnTable* table, StringView name);
GenericReturnCount generic_return_get(GenericReturnTable* table, StringView name);

size_t match_generic(SourceRange name, Type* generic, size_t generic_count);

void check_generic_args(GenericArgTable* table, Type* args, size_t args_count, Type* generic, size_t generic_count);
void check_generic_args_call(GenericCallTable* table, Type* args, size_t args_count, Type* generic, size_t generic_count);


Param* generic_param(GenericSeenTable* table, Param* param, size_t count, Type* generic, size_t generic_count, Type* fn_generic);
void generic_args_contains(GenericArgTable* table, Type* args, size_t args_count, Type* generic, size_t generic_count);

void check_generic(Register* reg, GenericTables* g);
void check_function(uint32_t id, GenericTables* g);

SourceRange get_functioncall(GenericSeenTable* global_table, GenericCallTable* table, SourceRange name, Type* generic, size_t generic_count);
SourceRange set_name(GenericSeenTable* global_table, SourceRange base_name, Type* generic, size_t generic_count);
SourceRange generic_get_name(uint32_t id);

bool generic_contains(GenericSeenTable* global_table, StringView name);
void generic_index(GenericSeenTable* global_table, StringView name, RegisterEntry* entry);
RegisterEntry* generic_get_parent(SourceRange call_name, int* tags, size_t tags_count);

EntityID generic_insert(GenericSeenTable* global_table, RegisterEntry entry);
EnumVariant* generic_variant(GenericSeenTable* table, EnumVariant* variant, size_t count, Type* generic, size_t generic_count, Type* fn_generic);

void check_enumcall(RegisterEntry* entry, GenericTables* g);
void check_body(Register* child_reg, uint32_t id, GenericTables* g); 
void check_enum(uint32_t id, GenericTables* g);
void check_var(RegisterEntry* entry, GenericTables* g, bool is_mut, bool is_const);

void check_structcall(RegisterEntry* entry, GenericTables* g);
UInt32Arr collect_generic_template_ids(Register* reg);
void check_struct(uint32_t id, GenericTables* g);
StructParam* generic_field(GenericSeenTable* table, StructParam* field, size_t count, Type* generic, size_t generic_count, Type* fn_generic);

void generic_args_register(GenericArgTable* table, StringView name) {
    int absent;
    khint_t k = generic_args_table_put(table, name, &absent);
    if (absent) kh_val(table, k) = (GenericArgCount){0};
    kh_val(table, k).generic_count++;
}

void generic_args_mark(GenericArgTable* table, StringView name) {
    khint_t k = generic_args_table_get(table, name);
    if (k != kh_end(table)) {
        kh_val(table, k).arg_count++;
    }
}

size_t match_generic(SourceRange name, Type* generic, size_t generic_count) {
    for (size_t i = 0; i < generic_count; i++) {
        if (range_eq(generic[i].data.custom.name, null_terminated(name))) return i;
    }
    return generic_count;
}

GenericArgCount generic_args_get(GenericArgTable* table, StringView name) {
    khint_t k = generic_args_table_get(table, name);
    if (k == kh_end(table)) return (GenericArgCount){0};
    return kh_val(table, k);
}

bool generic_args_contains_key(GenericArgTable* table, StringView name) {
    khint_t k = generic_args_table_get(table, name);
    return k != kh_end(table) && kh_val(table, k).arg_count > 0;
}

void generic_args_table_free(GenericArgTable* table) {
    generic_args_table_destroy(table);
}

void check_generic_args(GenericArgTable* table, Type* args, size_t args_count, Type* generic, size_t generic_count) {
    for (size_t i = 0; i < args_count; i++) {
        if (args[i].tag != Type_Custom) continue;

        SourceRange ty_name = args[i].data.custom.name;
        size_t idx = match_generic(ty_name, generic, generic_count);

        if (idx == generic_count) {
            size_t inner_count = args[i].data.custom.generic_args_count;
            if (args[i].tag == Type_Custom && args[i].data.custom.generic_args_count > 0) {
                Type* inner_args = set_generic_type(args[i].data.custom.generic_args, args[i].data.custom.generic_args_count);
                
                check_generic_args(table, inner_args, inner_count, generic, generic_count);
            }

            continue;
        }

        generic_args_mark(table, sv_from_range(ty_name));
    }
}

void generic_args_contains(GenericArgTable* table, Type* args, size_t args_count, Type* generic, size_t generic_count) {
    for (size_t g = 0; g < generic_count; g++) {
        generic_args_register(table, sv_from_range(generic[g].data.custom.name));
    }

    for (size_t i = 0; i < args_count; i++) {
        if (args[i].tag != Type_Custom) continue;

        SourceRange ty_name = args[i].data.custom.name;
        size_t idx = match_generic(ty_name, generic, generic_count);

        if (idx == generic_count) {
            size_t inner_count = args[i].data.custom.generic_args_count;
            Type* inner_args = set_generic_type(args[i].data.custom.generic_args, args[i].data.custom.generic_args_count);
            if (inner_count > 0) check_generic_args(table, inner_args, inner_count, generic, generic_count);
            continue;
        }

        generic_args_mark(table, sv_from_range(ty_name));
    }
}


void check_generic_args_call(GenericCallTable* table, Type* args, size_t args_count, Type* generic, size_t generic_count) {
    for (size_t i = 0; i < args_count; i++) {
        if (args[i].tag != Type_Custom) continue;

        SourceRange ty_name = args[i].data.custom.name;
        size_t idx = match_generic(ty_name, generic, generic_count);

        if (idx == generic_count) {
            size_t inner_count = args[i].data.custom.generic_args_count;
            Type* inner_args = set_generic_type(args[i].data.custom.generic_args, args[i].data.custom.generic_args_count);
            if (inner_count > 0) check_generic_args_call(table, inner_args, inner_count, generic, generic_count);
            continue;
        }

        generic_call_mark(table, sv_from_range(ty_name));
    }
}

void generic_param_register(GenericCountTable* table, StringView name) {
    int absent;
    khint_t k = generic_count_table_put(table, name, &absent);
    if (absent) kh_val(table, k) = (GenericCount){0};
    kh_val(table, k).generic_count++;
}

void generic_param_mark(GenericCountTable* table, StringView name) {
    khint_t k = generic_count_table_get(table, name);
    if (k != kh_end(table)) {
        kh_val(table, k).param_count++;
    }
}

EntityID generic_insert(GenericSeenTable* global_table, RegisterEntry entry) {
    EntityID new_id = register_insert(global_reg_ptr, entry);
    RegisterEntry* stable = register_from_global(new_id.id);

    int absent;
    khint_t k = generic_seen_table_put(global_table, sv_from_range(stable->decl_name_range), &absent);
    kh_val(global_table, k) = new_id.id;

    return new_id;
}

GenericCount generic_param_get(GenericCountTable* table, StringView name) {
    khint_t k = generic_count_table_get(table, name);
    if (k == kh_end(table)) return (GenericCount){0};
    return kh_val(table, k);
}

bool generic_param_contains(GenericCountTable* table, StringView name) {
    khint_t k = generic_count_table_get(table, name);
    return k != kh_end(table) && kh_val(table, k).param_count > 0;
}

void generic_param_table_free(GenericCountTable* table) {
    generic_count_table_destroy(table);
}

void generic_call_register(GenericCallTable* t, StringView name) {
    int absent;
    khint_t k = generic_call_table_put(t, name, &absent);
    if (absent) kh_val(t, k) = (GenericCallCount){0};
    kh_val(t, k).generic_count++;
}

void generic_call_mark(GenericCallTable* t, StringView name) {
    khint_t k = generic_call_table_get(t, name);
    if (k != kh_end(t)) kh_val(t, k).arg_count++;
}

GenericCallCount generic_call_get(GenericCallTable* t, StringView name) {
    khint_t k = generic_call_table_get(t, name);
    return k == kh_end(t) ? (GenericCallCount){0} : kh_val(t, k);
}

void generic_return_register(GenericReturnTable* t, StringView name) {
    int absent;
    khint_t k = generic_return_table_put(t, name, &absent);
    if (absent) kh_val(t, k) = (GenericReturnCount){0};
    kh_val(t, k).generic_count++;
}

void generic_return_mark(GenericReturnTable* t, StringView name) {
    khint_t k = generic_return_table_get(t, name);
    if (k != kh_end(t)) kh_val(t, k).match_count++;
}

RegisterEntry* generic_get(uint32_t id, int expected_tag) {
    RegisterEntry* e = register_from_global(id);
    if (!e || e->tag != expected_tag) return NULL;
    return e;
}

RegisterEntry* generic_get_parent(SourceRange call_name, int* tags, size_t tags_count) {
    return register_by_name(sv_from_range(call_name));
}

RegisterEntry* generic_get_by_name(GenericSeenTable* global_table, StringView name) {
    khint_t k = generic_seen_table_get(global_table, name);
    if (k == kh_end(global_table)) return NULL;
    uint32_t id = kh_val(global_table, k);
    return register_from_global(id);
}

bool generic_contains(GenericSeenTable* global_table, StringView name) {
    khint_t k = generic_seen_table_get(global_table, name);
    return k != kh_end(global_table);
}

void generic_index(GenericSeenTable* global_table, StringView name, RegisterEntry* entry) {
    int absent;
    khint_t k = generic_seen_table_put(global_table, name, &absent);
    kh_val(global_table, k) = entry->eid.id;
}

static SourceRange type_to_name(Type t) {
    switch (t.tag) {
        case Type_Custom: return t.data.custom.name;

        case Type_Int: {
            const char* base = t.data.int_t.is_unsigned ? "uint" : "int";
            char buf[16];
            int len;
            if (t.data.int_t.bits == 32 && !t.data.int_t.is_unsigned) {
                len = snprintf(buf, sizeof(buf), "int");
            } else {
                len = snprintf(buf, sizeof(buf), "%s%d", base, t.data.int_t.bits);
            }
            char* out = malloc(len + 1);
            memcpy(out, buf, len + 1);
            return (SourceRange){ .start = out, .end = out + len };
        }

        case Type_Float: {
            char buf[16];
            int len = (t.data.float_t.bits == 32) ? snprintf(buf, sizeof(buf), "float") : snprintf(buf, sizeof(buf), "float%d", t.data.float_t.bits);
            char* out = malloc(len + 1);
            memcpy(out, buf, len + 1);
            return (SourceRange){ .start = out, .end = out + len };
        }

        case Type_Char: {
            static char lit[] = "char";
            return (SourceRange){ .start = lit, .end = lit + 4 };
        }
        case Type_Str: {
            static char lit[] = "str";
            return (SourceRange){ .start = lit, .end = lit + 3 };
        }
        case Type_Bool: {
            static char lit[] = "bool";
            return (SourceRange){ .start = lit, .end = lit + 4 };
        }
        case Type_Void: {
            static char lit[] = "void";
            return (SourceRange){ .start = lit, .end = lit + 4 };
        }

        case Type_Ptr: {
            SourceRange inner = t.data.ptr.inner ? type_to_name(*t.data.ptr.inner) : (SourceRange){0};
            size_t ilen = inner.end - inner.start;
            size_t total = ilen + 3;
            char* out = malloc(total + 1);
            memcpy(out, "ptr", 3);
            memcpy(out + 3, inner.start, ilen);
            out[total] = '\0';
            return (SourceRange){ .start = out, .end = out + total };
        }

        case Type_RawPtr: {
            SourceRange inner = t.data.raw_ptr.inner ? type_to_name(*t.data.raw_ptr.inner) : (SourceRange){0};
            size_t ilen = inner.end - inner.start;
            size_t total = ilen + 6;
            char* out = malloc(total + 1);
            memcpy(out, "rawptr", 6);
            memcpy(out + 6, inner.start, ilen);
            out[total] = '\0';
            return (SourceRange){ .start = out, .end = out + total };
        }

        case Type_Array: {
            SourceRange inner = t.data.array_t.inner ? type_to_name(*t.data.array_t.inner) : (SourceRange){0};
            size_t ilen = inner.end - inner.start;
            char numbuf[24];
            int nlen = snprintf(numbuf, sizeof(numbuf), "arr%zu", t.data.array_t.len);
            size_t total = (size_t)nlen + ilen;
            char* out = malloc(total + 1);
            memcpy(out, numbuf, nlen);
            memcpy(out + nlen, inner.start, ilen);
            out[total] = '\0';
            return (SourceRange){ .start = out, .end = out + total };
        }

        case Type_Atomic: {
            SourceRange inner = t.data.atomic.inner ? type_to_name(*t.data.atomic.inner) : (SourceRange){0};
            size_t ilen = inner.end - inner.start;
            size_t total = ilen + 6;
            char* out = malloc(total + 1);
            memcpy(out, "atomic", 6);
            memcpy(out + 6, inner.start, ilen);
            out[total] = '\0';
            return (SourceRange){ .start = out, .end = out + total };
        }

        case Type_Tuple: {
            size_t total = 3;
            for (size_t i = 0; i < t.data.tuple.elems_count; i++) {
                SourceRange e = type_to_name(t.data.tuple.elems[i]);
                total += 1 + (e.end - e.start);
            }
            char* out = malloc(total + 1);
            size_t pos = 0;
            memcpy(out, "tup", 3);
            pos += 3;
            for (size_t i = 0; i < t.data.tuple.elems_count; i++) {
                SourceRange e = type_to_name(t.data.tuple.elems[i]);
                size_t elen = e.end - e.start;
                out[pos++] = '_';
                memcpy(out + pos, e.start, elen);
                pos += elen;
            }
            out[total] = '\0';
            return (SourceRange){ .start = out, .end = out + total };
        }

        case Type_FnPtr: {
            static char lit[] = "fnptr";
            return (SourceRange){ .start = lit, .end = lit + 5 };
        }

        default: {
            static char lit[] = "unknown";
            return (SourceRange){ .start = lit, .end = lit + 7 };
        }
    }
}
SourceRange set_name(GenericSeenTable* global_table, SourceRange base_name, Type* generic, size_t generic_count) {
    size_t base_len = base_name.end - base_name.start;

    ARR(SourceRange) names = {0};    
    ARR(char) buf = {0};

    ARR_MAKE_ROOM(names, generic_count);
    for (size_t i = 0; i < generic_count; i++) {
        SourceRange r = type_to_name(generic[i]);
        ARR_PUSH(names, r);
    }

    ARR_PUSH_N(buf, base_name.start, base_len);
    for (size_t i = 0; i < names.len; i++) {
        size_t g_len = names.data[i].end - names.data[i].start;
        ARR_PUSH(buf, '_');
        if (g_len > 0) ARR_PUSH_N(buf, names.data[i].start, g_len);
    }
    ARR_PUSH(buf, '\0');
    ARR_FREE(names);

    size_t total = buf.len - 1;

    SourceRange candidate = { .start = buf.data, .end = buf.data + total, .file_id = base_name.file_id };
    StringView sv = sv_from_range(candidate);

    if (generic_contains(global_table, sv)) {
        khint_t k = generic_seen_table_get(global_table, sv);
        uint32_t id = kh_val(global_table, k);
        RegisterEntry* cached = register_from_global(id);
        ARR_FREE(buf);
        return cached->decl_name_range;
    }
    RegisterEntry* existing = register_by_name(sv);
    if (!existing) {
        return candidate;
    }

    for (size_t suffix = 1;; suffix++) {
        char num[32];
        int nlen = snprintf(num, sizeof(num), "_%zu", suffix);

        ARR(char) buf2 = {0};
        ARR_PUSH_N(buf2, buf.data, total);
        ARR_PUSH_N(buf2, num, nlen);
        ARR_PUSH(buf2, '\0');
    
        size_t total2 = buf2.len - 1;
        StringView sv2 = { .ptr = buf2.data, .len = total2 };
        RegisterEntry* clash = register_by_name(sv2);
        if (!clash) {
            ARR_FREE(buf);
            printf("part 1");
            return (SourceRange){ .start = buf2.data, .end = buf2.data + total2, .file_id = base_name.file_id };
        }
        printf("part 1");
        ARR_FREE(buf2);
    }
}
SourceRange generic_get_name(uint32_t id) {
    RegisterEntry* fn = register_from_global(id);
    if (!fn) return (SourceRange){0};
    return fn->decl_name_range;
}


SourceRange get_functioncall(GenericSeenTable* global_table, GenericCallTable* table, SourceRange name, Type* generic, size_t generic_count) {
    ARR(Type) used = {0};

    for (size_t i = 0; i < generic_count; i++) {
        StringView key = sv_from_range(generic[i].data.custom.name);
        GenericCallCount c = generic_call_get(table, key);
        if (c.arg_count > 0) {
            ARR_PUSH(used, generic[i]);
        }
    }

    SourceRange result = set_name(global_table, name, used.data, used.len);
    ARR_FREE(used);
    return result;
}

GenericReturnCount generic_return_get(GenericReturnTable* t, StringView name) {
    khint_t k = generic_return_table_get(t, name);
    return k == kh_end(t) ? (GenericReturnCount){0} : kh_val(t, k);
}



#endif