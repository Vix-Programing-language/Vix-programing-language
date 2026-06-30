#include "import.h"
#include "register.h"
#include "ir.h"
#include "import.h"
#include "third-party/khashl.h"
#include "helper.h"
#include "register_tables.h"


bool is_generic_type(RegisterEntry* e);
Register* global_reg_ptr = NULL;
RegisterEntry** global_flat = NULL;
size_t global_flat_cap = 0;
IdTable* global_parents = NULL;
EntryArena entry_arena = {0};

khint_t sv_hash(StringView sv) {
    return kh_hash_bytes((int)sv.len, (const unsigned char*)sv.ptr);
}

int sv_eq(StringView a, StringView b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

static char*  id_buf      = NULL;
static size_t id_buf_pos  = 0;
static size_t id_buf_cap  = 0;


RegisterEntry* arena_push(RegisterEntry entry) {
    RegisterEntry* p = malloc(sizeof(RegisterEntry));
    *p = entry;
    return p;
}

static size_t arena_push_idx(RegisterEntry entry) {
    ARR_PUSH(entry_arena, entry);
    return entry_arena.len - 1;
}

StringView sv_from_range(SourceRange r) {
    return (StringView){ .ptr = r.start, .len = (size_t)(r.end - r.start) };
}

Register register_new(Register* parent, IDCounter* counter) {
    return (Register){
        .table = register_table_init(),
        .parent = parent,
        .pending = pending_table_init(),
        .counter = counter,
        .mono = NULL,
        .owner_id = 0,
    };
}


void global_registry_init(void) {
    global_parents = id_table_init();
}

static StringView id_to_sv(uint32_t id) {
    char* str = malloc(32);
    int len = snprintf(str, 32, "%u", id);
    return (StringView){ .ptr = str, .len = (size_t)len };
}

static StringView id_to_sv_alloc(uint32_t id) {
    char* str = malloc(32);
    int len = snprintf(str, 32, "%u", id);
    return (StringView){ .ptr = str, .len = (size_t)len };
}

#define SET_TEMP_ID_SV(var_name, id) \
    char var_name##_buf[32]; \
    int var_name##_len = snprintf(var_name##_buf, sizeof(var_name##_buf), "%u", id); \
    StringView var_name = (StringView){ .ptr = var_name##_buf, .len = (size_t)var_name##_len };

    EntityID register_insert(Register* reg, RegisterEntry entry) {
    entry.eid = (EntityID){ .id = reg->counter->next_id++, .kind = entry.tag };

    if (reg->owner_id != 0) {
        RegisterEntry* owner = register_from_global(reg->owner_id);
        if (owner && owner->tag == Reg_Function)
            entry.owner_fn_id = reg->owner_id;
        else
            entry.owner_fn_id = owner ? owner->owner_fn_id : 0;
    } else {
        entry.owner_fn_id = 0;
    }

    RegisterEntry* stable = malloc(sizeof(RegisterEntry));
    *stable = entry;

    global_flat_insert(entry.eid.id, stable);

    StringView name_key = sv_from_range(stable->decl_name_range);
    if (name_key.ptr && name_key.len > 0) {
        int absent;
        khint_t k = register_table_put(reg->table, name_key, &absent);
        if (absent > 0) kh_val(reg->table, k) = stable;
    }

    StringView id_key = id_to_sv_alloc(entry.eid.id);
    int absent;
    khint_t k = register_table_put(reg->table, id_key, &absent);
    if (absent > 0) {
        kh_val(reg->table, k) = stable;
    } else {
        free((void*)id_key.ptr);
    }

    return entry.eid;
}

void global_flat_insert(uint32_t id, RegisterEntry* e) {
    if (id >= global_flat_cap) {
        global_flat_cap = global_flat_cap ? global_flat_cap * 2 : 1024;
        global_flat = realloc(global_flat, global_flat_cap * sizeof(RegisterEntry*));
    }
    global_flat[id] = e;
}

RegisterEntry* register_from_global(uint32_t id) {
    if (!global_flat || id >= global_flat_cap) return NULL;
    return global_flat[id];
}

RegisterEntry* register_by_target(StringView name, int* tags, size_t tags_count) {
    RegisterEntry* e = register_get(global_reg_ptr, name);
    if (!e) {
        return NULL;
    }


    for (size_t i = 0; i < tags_count; i++) {
        if (e->tag == tags[i]) {
            return e;
        }
    }
    return NULL;
}


Register* register_get_child(Register* reg, uint32_t id) {
    RegisterEntry* e = register_from_scope(reg, id);
    if (!e) return NULL;

    switch (e->tag) {
        case Reg_Function: return e->data.function.child_reg;
        case Reg_Extern:   return e->data.extern_.child_reg;
        case Reg_If:
        case Reg_Elif:     return e->data.if_.then_child;
        case Reg_While:    return e->data.while_.body_child;
        case Reg_For:      return e->data.for_.body_child;
        case Reg_Match:    return e->data.match_.expr_child;
        default:           return NULL;
    }
}

RegisterEntry* register_by_name(StringView name) {
    if (!global_reg_ptr) return NULL;

    khint_t k = register_table_get(global_reg_ptr->table, name);
    if (k == kh_end(global_reg_ptr->table)) return NULL;

    return kh_val(global_reg_ptr->table, k);
}

RegisterEntry* register_lookup(Register* reg, StringView key) {
    khint_t k = register_table_get(reg->table, key);
    if (k == kh_end(reg->table)) return NULL;
    return kh_val(reg->table, k);
}


RegisterEntry* register_from_scope(Register* reg, uint32_t id) {
    SET_TEMP_ID_SV(key, id);
    khint_t k = register_table_get(reg->table, key);
    if (k == kh_end(reg->table)) return NULL;
    RegisterEntry* e = kh_val(reg->table, k);
    if (!e || e->tag < 0 || e->tag > 50) {
        abort();
    }
    return e;
}

EntityID register_insert_id(Register* reg, RegisterEntry entry, uint32_t id) {
    entry.eid = (EntityID){ .id = id, .kind = entry.tag };

    RegisterEntry* stable = arena_push(entry);

    global_flat_insert(id, stable);

    StringView name_key = sv_from_range(stable->decl_name_range);
    if (name_key.ptr && name_key.len > 0) {
        int absent;
        khint_t k = register_table_put(reg->table, name_key, &absent);
        kh_val(reg->table, k) = stable;
    }

    StringView id_key = id_to_sv(entry.eid.id);
    int absent;
    khint_t k = register_table_put(reg->table, id_key, &absent);
    kh_val(reg->table, k) = stable;

    return entry.eid;
}

bool register_insert_child(Register* reg, RegisterEntry entry, uint32_t parent_flat_id) {
    entry.eid = (EntityID){ .id = reg->counter->next_id++, .kind = entry.tag };
    RegisterEntry* stable = arena_push(entry);

    global_flat_insert(entry.eid.id, stable);

    StringView name_key = sv_from_range(stable->decl_name_range);
    if (name_key.ptr && name_key.len > 0) {
        int absent;
        khint_t k = register_table_put(reg->table, name_key, &absent);
        if (absent > 0) kh_val(reg->table, k) = stable;
    }

    StringView id_key = id_to_sv_alloc(entry.eid.id);
    int absent;
    khint_t k = register_table_put(reg->table, id_key, &absent);
    if (absent > 0) {
        kh_val(reg->table, k) = stable;
    } else {
        free((void*)id_key.ptr);
    }
    return true;
}

Register* make_child(Register* parent) {
    Register* child = malloc(sizeof(Register));
    *child = (Register){
        .table = register_table_init(),
        .parent = parent,
        .pending = pending_table_init(),
        .counter = parent->counter,
        .mono = NULL,
        .owner_id = 0,
    };
    return child;
}

RegisterEntry* register_get(Register* reg, StringView name) {
    Register* cur = reg;
    while (cur) {
        khint_t k = register_table_get(cur->table, name);
        if (k != kh_end(cur->table)) return kh_val(cur->table, k);
        cur = cur->parent;
    }
    return NULL;
}

uint32_t register_get_id(Register* reg, StringView name) {
    RegisterEntry* e = register_get(reg, name);
    return e ? e->eid.id : 0;
}

StringView register_get_name(Register* reg, uint32_t id) {
    RegisterEntry* e = register_from_scope(reg, id);
    if (!e) return (StringView){0};
    return sv_from_range(e->decl_name_range);
}

uint32_t register_get_function(uint32_t child_id) {
    RegisterEntry* e = register_from_global(child_id);
    return e ? e->owner_fn_id : 0;
}

FuncBodyList register_body(Stmts* body, size_t count, Register* reg, CheckerErrList* errors) {
    FuncBodyList fl = {0};
    for (size_t i = 0; i < count; i++) register_stmt(reg, &body[i], (SourceRange){0});
    return fl;
}

void register_free(Register* reg) {
    register_table_destroy(reg->table);
    pending_table_destroy(reg->pending);
}
