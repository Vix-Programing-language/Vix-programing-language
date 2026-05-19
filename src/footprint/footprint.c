#include "footprint.h"
#include "bytes.h"
#include "dir.h"
#include "import.h"

#define UINT_CHUNK_LIMIT (64 * 1024)

size_t PACK_CHUNK_SIZE = 64 * 1024 * 1024;  // default 64mb, overridden from config
char*  FP_LINK_OVERRIDE = NULL;
void range_to_span(SourceRange r, LineStarts* ls, uint32_t* line_start, uint16_t* col_start, uint32_t* line_end, uint16_t* col_end);
static void gw_init(GroupWriters* gw, const char* project_name, const char* source_file, uint64_t source_hash);
static void gw_flush_uint(GroupWriters* gw, PackSymGroup g);
static void gw_add(GroupWriters* gw, PackSymbol sym, PackSymGroup g);
static void gw_flush_all_uints(GroupWriters* gw);
static StrTab strtab_new(void);
static void strtab_free(StrTab* st);
static uint32_t strtab_push(StrTab* st, const char* ptr, size_t len);
static uint32_t strtab_push_cstr(StrTab* st, const char* s);
PackWriter pack_writer_new(void);
void       pack_writer_free(PackWriter* w);

PackArena pack_arena_new(void) {
    PackArena a = {0};
    PackChunk first = {0};

    ARR_ENSURE_CAP(first.data, PACK_CHUNK_SIZE); first.data.len = PACK_CHUNK_SIZE;
    ARR_PUSH(a.chunks, first);

    return a;
}

void pack_arena_free(PackArena* a) {
    for (size_t i = 0; i < a->chunks.len; i++) ARR_FREE(a->chunks.data[i].data);

    ARR_FREE(a->chunks);
    a->total_written = 0;
}

uint8_t* pack_arena_push(PackArena* a, size_t size) {
    PackChunk* cur = &a->chunks.data[a->chunks.len - 1];
    PackChunk next = {0};

    if (cur->used + size <= cur->data.cap) {
        uint8_t* ptr = cur->data.data + cur->used;
        cur->used += size;
        a->total_written += size;

        return ptr;
    }


    size_t chunk_size = size > PACK_CHUNK_SIZE ? size : PACK_CHUNK_SIZE;
    ARR_ENSURE_CAP(next.data, chunk_size);

    next.data.len = chunk_size;
    next.used = size;

    ARR_PUSH(a->chunks, next);
    a->total_written += size;

    return a->chunks.data[a->chunks.len - 1].data.data;
}

ErrWriter err_writer_new(void) {
    ErrWriter w = {0};

    w.arena = pack_arena_new();
    w.strtab = strtab_new();
    return w;
}

void err_writer_free(ErrWriter* w) {
    pack_arena_free(&w->arena);
    strtab_free(&w->strtab);

    ARR_FREE(w->entries);
}

void err_writer_add(ErrWriter* w, PackErr err) {
    if (err.kind == PE_Error)   w->header.error_count++;
    if (err.kind == PE_Warning) w->header.warning_count++;
    ARR_PUSH(w->entries, err);
}

bool err_writer_flush(ErrWriter* w, const char* path) {
    w->header.magic = ERR_MAGIC;
    w->header.version = ERR_VERSION;
    w->header.strtab_size = (uint32_t)w->strtab.len;

    ByteBuf bb = {0};
    bb_push_raw(&bb, &w->header, sizeof(ErrHeader));
    bb_push_raw(&bb, w->entries.data, w->entries.len * sizeof(PackErr));
    bb_push_raw(&bb, w->strtab.data, w->strtab.len);

    DirErr err = dir_write_file(path, bb.data, bb.len);
    bb_free(&bb);

    return err == DIR_OK;
}

StrTab strtab_new(void) {
    StrTab st = {0};
    ARR_PUSH(st, '\0');

    return st;
}

void strtab_free(StrTab* st) {
    free(st->data);

    *st = (StrTab){0};
}

uint32_t strtab_push(StrTab* st, const char* ptr, size_t len) {
    if (!ptr || len == 0) return 0;
    uint32_t offset = (uint32_t)st->len;

    ARR_MAKE_ROOM(*st, len + 1);
    memcpy(st->data + st->len, ptr, len);

    st->data[st->len + len] = '\0';
    st->len += len + 1;
    return offset;
}

uint32_t strtab_push_cstr(StrTab* st, const char* s) {
    if (!s) return 0;
    return strtab_push(st, s, strlen(s));
}

uint64_t pack_hash_source(const char* src, size_t len) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)src[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

uint32_t pack_hash_decl(const char* start, const char* end) {
    uint32_t h = 0x811c9dc5u;
    for (const char* p = start; p < end; p++) {
        h ^= (uint8_t)*p;
        h *= 0x01000193u;
    }
    return h;
}

PackWriter pack_writer_new(void) {
    PackWriter w = {0};
    w.arena  = pack_arena_new();
    w.strtab = strtab_new();
    return w;
}

void pack_writer_free(PackWriter* w) {
    pack_arena_free(&w->arena);
    strtab_free(&w->strtab);
    ARR_FREE(w->symbols);
}

void pack_writer_add_symbol(PackWriter* w, PackSymbol sym) {
    ARR_PUSH(w->symbols, sym);
}

bool pack_writer_flush(PackWriter* w, const char* path, uint64_t source_hash) {
    PackHeader hdr = {0};
    ByteBuf bb = {0};

    hdr.magic = PACK_MAGIC;
    hdr.version = PACK_VERSION;
    hdr.source_hash  = source_hash;
    hdr.symbol_count = (uint32_t)w->symbols.len;
    hdr.strtab_size  = (uint32_t)w->strtab.len;

    bb_push_raw(&bb, &hdr, sizeof(hdr));
    bb_push_raw(&bb, w->symbols.data, w->symbols.len * sizeof(PackSymbol));
    bb_push_raw(&bb, w->strtab.data, w->strtab.len);

    for (size_t i = 0; i < w->arena.chunks.len; i++) {
        if (w->arena.chunks.data[i].used == 0) continue;

        bb_push_raw(&bb, w->arena.chunks.data[i].data.data, w->arena.chunks.data[i].used);
    }

    DirErr err = dir_write_file(path, bb.data, bb.len);
    bb_free(&bb);
    return err == DIR_OK;
}


static bool merge_uints_to_pack(const char* project_name, const char* source_file, PackSymGroup g, uint32_t chunk_count, uint64_t source_hash) {
    if (chunk_count == 0) return true;

    char* path = fp_pack_path(project_name, source_file, g);
    ByteBuf bb = {0};
    PackUnitEntryArr entries = {0};
    ARR_ENSURE_CAP(entries, chunk_count);
    entries.len = chunk_count;

    PackContainerHeader hdr = {0};
    hdr.magic       = PACK_MAGIC;
    hdr.version     = PACK_VERSION;
    hdr.source_hash = source_hash;
    hdr.unit_count  = chunk_count;

    bb_push_raw(&bb, &hdr, sizeof(hdr));

    size_t index_pos = bb.len;
    bb_push_raw(&bb, entries.data, sizeof(PackUnitEntry) * chunk_count);

    for (uint32_t i = 0; i < chunk_count; i++) {
        char* upath = fp_uint_path(project_name, source_file, g, i + 1);
        entries.data[i].offset = (uint64_t)bb.len;
        entries.data[i].group  = (uint8_t)g;
        pipe_file_contents(upath, &bb, &entries.data[i].size);
        free(upath);
    }

    memcpy(bb.data + index_pos, entries.data, sizeof(PackUnitEntry) * chunk_count);
    DirErr err = dir_write_file(path, bb.data, bb.len);

    bb_free(&bb);
    ARR_FREE(entries);
    free(path);
    return err == DIR_OK;
}

static PackTypeTag type_to_pack(Type t) {
    switch (t.tag) {
        case Type_Int: return t.data.int_t.bits == 64 ? PT_Int64 : PT_Int32;
        case Type_Float: return t.data.float_t.bits == 64 ? PT_Float64 : PT_Float32;
        case Type_Bool: return PT_Bool;
        case Type_Char: return PT_Char;
        case Type_Str: return PT_Str;
        case Type_Custom: return PT_Custom;
        default: return PT_Void;
    }
}

static PackSymTag reg_tag_to_pack(RegisterEntryTag t) {
    switch (t) {
        case Reg_Var: return PS_Var;
        case Reg_Let: return PS_Let;
        case Reg_Const: return PS_Const;
        case Reg_Local: return PS_Local;
        case Reg_Function: return PS_Function;
        case Reg_Class: return PS_Class;
        case Reg_Struct: return PS_Struct;
        case Reg_Enum: return PS_Enum;
        case Reg_Trait: return PS_Trait;
        case Reg_Extern: return PS_Extern;
        default: return PS_Var;
    }
}

PackSymbol pack_symbol_from_entry(RegisterEntry* e, StrTab* st, const char* file, LineStarts* ls) {
    PackSymbol s = {0};
    uint8_t flags = 0;

    s.eid = e->eid.id;
    s.name_offset = strtab_push_cstr(st, e->name);
    s.file_offset = strtab_push_cstr(st, file);
    s.sym_tag = reg_tag_to_pack(e->tag);
    s.type_tag = type_to_pack(e->type);
    s.scope_level = 0;

    if (e->type.tag == Type_Custom) {
        size_t len = e->type.data.custom.name.end - e->type.data.custom.name.start;

        s.type_name_offset = strtab_push(st, e->type.data.custom.name.start, len);
    }

    if (e->decl_range.start && e->decl_range.end > e->decl_range.start) s.decl_hash = pack_hash_decl(e->decl_range.start, e->decl_range.end);

    range_to_span(e->decl_range, ls, &s.line_start, &s.col_start, &s.line_end, &s.col_end);


    switch (e->tag) {
        case Reg_Var: if (e->data.var.is_mut) flags |= PFLAG_MUT;   break;
        case Reg_Const: flags |= PFLAG_CONST; if (e->data.const_.is_pub) flags |= PFLAG_PUB; break;
        case Reg_Let: break;
        case Reg_Local: if (e->data.local.is_pub) flags |= PFLAG_PUB;   break;
        case Reg_Function: if (e->data.function.is_pub) flags |= PFLAG_PUB;   break;
        case Reg_Class:
        case Reg_Struct: if (e->tag == Reg_Struct && e->data.strct.is_pub) flags |= PFLAG_PUB; break;
        case Reg_Enum: if (e->data.enm.is_pub) flags |= PFLAG_PUB;   break;
        case Reg_Trait: if (e->data.trait.is_pub) flags |= PFLAG_PUB;   break;
        default: break;
    }
    s.flags = flags;

    return s;
}


static void body_merge_fn(ByteBuf* bb, PackUnitEntry* entries, uint32_t count, void* user) {
    BodyMergeCtx* ctx = (BodyMergeCtx*)user;
    uint32_t ei = 0;

    for (int g = 0; g < PACK_SG_COUNT; g++) {
        if (!ctx->group_present[g]) continue;
        for (uint32_t ci = 0; ci < ctx->chunks[g] - 1; ci++) {
            char* upath = fp_uint_path(ctx->project_name, ctx->source_file, (PackSymGroup)g, ci + 1);
            entries[ei].offset = (uint64_t)bb->len;
            entries[ei].group  = (uint8_t)g;

            pipe_file_contents(upath, bb, &entries[ei].size);
            free(upath);

            ei++;
        }
    }
}

static bool merge_body_uints_to_pack(const char* project_name, const char* source_file, const char* func_name, uint32_t func_chunk, bool* group_present, uint32_t* chunks, uint64_t source_hash) {
    char* path = fp_pack_path(project_name, source_file, SG_Function);
    ByteBuf bb = {0};
    uint32_t present_count = 0;

    for (int i = 0; i < PACK_SG_COUNT; i++) if (group_present[i]) present_count++;
    if (present_count == 0) { free(path); return true; }

    PackUnitEntryArr entries = {0};
    ARR_ENSURE_CAP(entries, present_count);
    entries.len = present_count;

    PackContainerHeader hdr = { PACK_MAGIC, PACK_VERSION, source_hash, present_count };
    bb_push_raw(&bb, &hdr, sizeof(hdr));

    size_t index_pos = bb.len;
    bb_push_raw(&bb, entries.data, sizeof(PackUnitEntry) * present_count);

    uint32_t ei = 0;
    for (int g = 0; g < PACK_SG_COUNT; g++) {
        if (!group_present[g]) continue;
        for (uint32_t ci = 0; ci < chunks[g] - 1; ci++) {
            char* upath = fp_uint_path(project_name, source_file, (PackSymGroup)g, ci + 1);
            entries.data[ei].offset = (uint64_t)bb.len;
            entries.data[ei].group  = (uint8_t)g;
            pipe_file_contents(upath, &bb, &entries.data[ei].size);
            free(upath);
            ei++;
        }
    }

    memcpy(bb.data + index_pos, entries.data, sizeof(PackUnitEntry) * present_count);
    DirErr err = dir_write_file(path, bb.data, bb.len);

    bb_free(&bb);
    ARR_FREE(entries);
    free(path);
    return err == DIR_OK;
}

static bool merge_packs_to_final(const char* project_name, const char* source_file, uint64_t source_hash, bool* group_present, FuncPackList* func_packs) {
    char* path = fp_pack_path(project_name, source_file, SG_All);
    ByteBuf bb = {0};
    uint32_t present_count = 0;
    uint32_t ei = 0;

    for (int i = 0; i < PACK_SG_COUNT; i++) if (group_present[i]) present_count++;
    present_count += (uint32_t)func_packs->len;

    PackUnitEntryArr entries = {0};
    ARR_ENSURE_CAP(entries, present_count);
    entries.len = present_count;

    PackContainerHeader hdr = { PACK_MAGIC, PACK_VERSION, source_hash, present_count };
    bb_push_raw(&bb, &hdr, sizeof(hdr));

    size_t index_pos = bb.len;
    bb_push_raw(&bb, entries.data, sizeof(PackUnitEntry) * present_count);

    for (int i = 0; i < PACK_SG_COUNT; i++) {
        if (!group_present[i]) continue;
        char* ppath = fp_pack_path(project_name, source_file, (PackSymGroup)i);
        entries.data[ei].offset = (uint64_t)bb.len;
        entries.data[ei].group  = (uint8_t)i;
        pipe_file_contents(ppath, &bb, &entries.data[ei].size);
        free(ppath);
        ei++;
    }

    for (uint32_t i = 0; i < func_packs->len; i++) {
        entries.data[ei].offset = (uint64_t)bb.len;
        entries.data[ei].group  = (uint8_t)SG_Function;
        pipe_file_contents(func_packs->data[i], &bb, &entries.data[ei].size);
        ei++;
    }

    memcpy(bb.data + index_pos, entries.data, sizeof(PackUnitEntry) * present_count);
    DirErr err = dir_write_file(path, bb.data, bb.len);

    bb_free(&bb);
    ARR_FREE(entries);
    free(path);
    return err == DIR_OK;
}

static void func_pack_list_free(FuncPackList* fl) {
    for (uint32_t i = 0; i < fl->len; i++) free(fl->data[i]);

    ARR_FREE(*fl);
}
static void write_func_body(Register* child_reg, LineStarts* ls, const char* source_file, const char* project_name, const char* func_name, uint32_t func_chunk, uint64_t source_hash, FuncPackList* func_packs) {
    printf("[pack] write_func_body called 1");

    if (!child_reg->table) {
        fprintf(stderr, "[pack] child_reg->table is NULL, skipping\n");
        return;
    }

    khint_t it;
    GroupWriters body_gw = {0};
    printf("[pack] body_gw is saf");
    fprintf(stderr, "[pack] PACK_SG_COUNT = %d\n", PACK_SG_COUNT);
    bool body_present[PACK_SG_COUNT];
    memset(body_present, 0, sizeof(body_present));

    fprintf(stderr, "[pack] body_present allocated\n");
    fprintf(stderr, "[pack] write_func_body called, child symbols: %u\n", (unsigned)kh_size(child_reg->table));

    for (int i = 0; i < PACK_SG_COUNT; i++) {
        body_gw.writers[i] = pack_writer_new();
        body_gw.sizes[i]   = 0;
        body_gw.chunks[i]  = 1;
    }

    body_gw.project_name = project_name;
    body_gw.source_file  = source_file;
    body_gw.source_hash  = source_hash;

        
    kh_foreach(child_reg->table, it) {
        RegisterEntry* e = &kh_val(child_reg->table, it);
        fprintf(stderr, "[pack] child entry: name=%s tag=%d Reg_ExprFunctionCall=%d skip=%d\n",
            e->name ? e->name : "?",
            (int)e->tag,
            (int)Reg_ExprFunctionCall,
            (int)(e->tag >= Reg_ExprFunctionCall));
        if (e->tag >= Reg_ExprFunctionCall) continue;

        PackSymGroup g = reg_tag_to_group(e->tag);
        fprintf(stderr, "[pack] child symbol group=%d\n", (int)g);
        PackSymbol sym = pack_symbol_from_entry(e, &body_gw.writers[g].strtab, source_file, ls);

        fprintf(stderr, "[pack] size check: sizes[g]=%zu + sizeof=%zu > limit=%d ?\n",
            body_gw.sizes[g], sizeof(PackSymbol), UINT_CHUNK_LIMIT);

        if (body_gw.sizes[g] + sizeof(PackSymbol) > UINT_CHUNK_LIMIT) {
            fprintf(stderr, "[pack] flushing chunk\n");
            char* uint_path = func_uint_path(project_name, source_file, func_name, func_chunk, g, body_gw.chunks[g]);
            pack_writer_flush(&body_gw.writers[g], uint_path, source_hash);
            pack_writer_free(&body_gw.writers[g]);
            free(uint_path);
            body_gw.writers[g] = pack_writer_new();
            body_gw.sizes[g]   = 0;
            body_gw.chunks[g]++;
        }

        pack_writer_add_symbol(&body_gw.writers[g], sym);
        body_gw.sizes[g] += sizeof(PackSymbol);
        body_present[g]   = true;
        fprintf(stderr, "[pack] symbol added to group %d, sizes[g]=%zu\n", (int)g, body_gw.sizes[g]);
    }

    fprintf(stderr, "[pack] kh_foreach done\n");

    for (int i = 0; i < PACK_SG_COUNT; i++) {
        fprintf(stderr, "[pack] group %d: symbols=%zu\n", i, body_gw.writers[i].symbols.len);
        if (body_gw.writers[i].symbols.len == 0) { pack_writer_free(&body_gw.writers[i]); continue; }

        char* uint_path = func_uint_path(project_name, source_file, func_name, func_chunk, (PackSymGroup)i, body_gw.chunks[i]);
        fprintf(stderr, "[pack] flushing group %d to %s\n", i, uint_path);
        pack_writer_flush(&body_gw.writers[i], uint_path, source_hash);
        pack_writer_free(&body_gw.writers[i]);
        free(uint_path);
        body_gw.chunks[i]++;
    }

    fprintf(stderr, "[pack] calling merge_body_uints_to_pack\n");
    merge_body_uints_to_pack(project_name, source_file, func_name, func_chunk, body_present, body_gw.chunks, source_hash);
    fprintf(stderr, "[pack] merge done\n");

    char* path = fp_pack_path(project_name, source_file, SG_Function);
    ARR_PUSH(*func_packs, path);
}

void pack_write_register(Register* reg, CheckerErrList* errors, LineStarts* ls, const char* source, size_t source_len, const char* source_file, const char* project_name) {
    uint64_t src_hash = pack_hash_source(source, source_len);
    bool group_present[PACK_SG_COUNT] = {0};
    FuncPackList func_packs = {0};
    GroupWriters gw;
    khint_t it;
    ErrWriter ew = err_writer_new();
    char* path = fp_err_path(project_name, source_file);

    fp_ensure_tree(project_name);
    gw_init(&gw, project_name, source_file, src_hash);


    kh_foreach(reg->table, it) {
        RegisterEntry* e = &kh_val(reg->table, it);
        if (e->tag >= Reg_ExprFunctionCall) continue;

        RegisterEntryTag tag = e->tag;
        Register* child = (tag == Reg_Function) ? e->data.function.child_reg : NULL;
        PackSymGroup g = reg_tag_to_group(tag);
        PackSymbol sym = pack_symbol_from_entry(e, &gw.writers[g].strtab, source_file, ls);

        gw_add(&gw, sym, g);
        group_present[g] = true;

        if (tag == Reg_Function && child) {
                printf("[pack] Reg_function is true\n");
                Name func_name = {0};

                if (e->name) {
                    for (char* p = e->name; *p; p++) ARR_PUSH(func_name, *p);
                    ARR_PUSH(func_name, '\0');
                }
                uint32_t func_chunk = gw.chunks[SG_Function];
                printf("[pack] Reg_function is passd\n");
                write_func_body(child, ls, source_file, project_name, func_name.data, func_chunk, src_hash, &func_packs);
                ARR_FREE(func_name);
            }
        }

    gw_flush_all_uints(&gw);

    for (int i = 0; i < SG_All; i++) {
        if (!group_present[i]) continue;

        merge_uints_to_pack(project_name, source_file, (PackSymGroup)i, gw.chunks[i] - 1, src_hash);
    }

    merge_packs_to_final(project_name, source_file, src_hash, group_present, &func_packs);

    for (int i = 0; i < PACK_SG_COUNT; i++) pack_writer_free(&gw.writers[i]); func_pack_list_free(&func_packs);
    for (size_t i = 0; i < errors->count; i++) {
        CheckerErr* ce = &errors->errors[i];
        PackErr pe = {0};
        SourceRange r = {0};
        StringView vname = {0};

        pe.kind = PE_Error;
        pe.tag = (uint8_t)ce->tag;

        switch (ce->tag) {
            case Err_Tag_RDL: r = ce->data.rdl.range; vname = ce->data.rdl.var_name;  break;
            case Err_Tag_VMV: r = ce->data.vmv.range; vname = ce->data.vmv.var_name;  break;
            case Err_Tag_CVN: r = ce->data.cvn.range; vname = ce->data.cvn.var_name;  break;
            case Err_Tag_VSF: r = ce->data.vsf.range; vname = ce->data.vsf.var_name;  break;
            case Err_Tag_TNF: r = ce->data.tnf.range; vname = ce->data.tnf.type_name; break;
            default: break;
        }

        pe.file_offset = strtab_push_cstr(&ew.strtab, source_file);
        pe.var_name_offset = strtab_push(&ew.strtab, vname.ptr, vname.len);

        range_to_span(r, ls, &pe.line_start, &pe.col_start, &pe.line_end, &pe.col_end);
        err_writer_add(&ew, pe);
    }

    err_writer_flush(&ew, path);
    err_writer_free(&ew);
    free(path);
}