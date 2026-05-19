#ifndef FOOTPRINT_H
#define FOOTPRINT_H

#include "import.h"
#include "register.h"
#include "error.h"
#include "bytes.h"
#include "dir.h"
#include "third-party/khashl.h"


#define UINT_CHUNK_LIMIT (64 * 1024)


extern size_t PACK_CHUNK_SIZE;
extern char* FP_LINK_OVERRIDE;

typedef enum {
    SG_Var      = 0,
    SG_Function = 1,
    SG_Class    = 2,
    SG_Struct   = 3,
    SG_Enum     = 4,
    SG_Trait    = 5,
    SG_Extern   = 6,
    SG_All      = 7,
    SG_Error    = 8,
} PackSymGroup;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint64_t source_hash;
    uint32_t unit_count;
    uint32_t _pad;
} PackContainerHeader;

typedef enum __attribute__((packed)) {
    PT_Void    = 0,
    PT_Int32   = 1,
    PT_Int64   = 2,
    PT_Float32 = 3,
    PT_Float64 = 4,
    PT_Bool    = 5,
    PT_Char    = 6,
    PT_Str     = 7,
    PT_Custom  = 8,
} PackTypeTag;

typedef enum __attribute__((packed)) {
    PS_Var      = 0,
    PS_Let      = 1,
    PS_Const    = 2,
    PS_Local    = 3,
    PS_Function = 4,
    PS_Class    = 5,
    PS_Struct   = 6,
    PS_Enum     = 7,
    PS_Trait    = 8,
    PS_Extern   = 9,
} PackSymTag;

typedef struct {
    uint32_t eid;
    uint32_t name_offset;
    uint32_t file_offset;
    uint32_t decl_hash; 
    uint32_t type_name_offset;
    uint8_t flags;
    uint32_t line_start;
    uint32_t line_end;
    uint16_t col_start;
    uint16_t col_end;
    uint16_t read_count;
    uint16_t write_count;
    uint8_t scope_level;
    uint8_t _pad[2];

    PackTypeTag type_tag;
    PackSymTag sym_tag;
} PackSymbol;

typedef enum __attribute__((packed)) {
    PE_Error   = 0,
    PE_Warning = 1,
} PackErrKind;

typedef struct __attribute__((packed)) {
    uint64_t offset;
    uint64_t size;
    uint8_t  group;
    uint8_t  _pad[7];
} PackUnitEntry;

typedef struct {
    PackErrKind kind;

    uint8_t tag;
    uint8_t _pad[2];
    uint32_t file_offset;
    uint32_t var_name_offset;
    uint32_t msg_offset;
    uint32_t line_start;
    uint32_t line_end;
    uint16_t col_start;
    uint16_t col_end;
} PackErr; 

typedef ARR(uint8_t) ByteChunk;

typedef struct {
    ByteChunk data;
    size_t used;
} PackChunk;

typedef ARR(char*) FuncPackList;
typedef ARR(char) Filename;
typedef ARR(char) Name;
typedef ARR(PackChunk) PackChunkArr;
typedef ARR(PackErr) PackErrArr;
typedef ARR(PackSymbol) PackSymbolArr;
typedef ARR(PackUnitEntry) PackUnitEntryArr;
typedef ARR(uint8_t) StrTab;



typedef struct PackArena {
    PackChunkArr chunks;
    size_t total_written;
} PackArena;


typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t _pad[3];
    uint64_t source_hash;
    uint32_t symbol_count;
    uint32_t strtab_size;
} PackHeader;

typedef struct {
    PackArena arena;
    StrTab strtab;
    PackHeader header;
    PackSymbolArr symbols;
} PackWriter;


typedef struct {
    PackWriter writers[PACK_SG_COUNT];
    size_t     sizes[PACK_SG_COUNT];
    uint32_t   chunks[PACK_SG_COUNT];
    const char* project_name;
    const char* source_file;
    uint64_t    source_hash;
} GroupWriters;

typedef struct {
    const char* project_name;
    const char* source_file;
    const char* func_name;
    uint32_t    func_chunk;
    uint32_t*   chunks;
    bool*       group_present;
} BodyMergeCtx;

#define PFLAG_MUT          (1 << 0)
#define PFLAG_PUB          (1 << 1)
#define PFLAG_CONST        (1 << 2)
#define PFLAG_IN_SCOPE     (1 << 3)
#define PFLAG_BLOCK_USAGE  (1 << 4)
#define PFLAG_SHADOW       (1 << 5)
#define PFLAG_INITIALIZED  (1 << 6)
#define PFLAG_USED         (1 << 7)


#pragma pack(push, 1)


typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t _pad[3];
    uint32_t error_count;
    uint32_t warning_count;
    uint32_t strtab_size;
} ErrHeader;

#pragma pack(pop)

typedef struct {
    PackArena arena;
    StrTab strtab;
    ErrHeader header;
    PackErrArr entries;
} ErrWriter;


static inline char* arr_strdup(const char* s);
PackWriter pack_writer_new(void);
bool pack_writer_flush(PackWriter* w, const char* path, uint64_t source_hash);
void pack_writer_free(PackWriter* w);
void pack_writer_add_symbol(PackWriter* w, PackSymbol sym);


static PackSymGroup reg_tag_to_group(RegisterEntryTag t) {
    switch (t) {
        case Reg_Function: return SG_Function;
        case Reg_Class:    return SG_Class;
        case Reg_Struct:   return SG_Struct;
        case Reg_Enum:     return SG_Enum;
        case Reg_Trait:    return SG_Trait;
        case Reg_Extern:   return SG_Extern;
        default:           return SG_Var;
    }
}

static inline char* arr_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    Filename buf = {0};
    ARR_ENSURE_CAP(buf, len);
    buf.len = len;
    memcpy(buf.data, s, len);
    char* result = buf.data;
    buf.data = NULL;
    return result;
}


static inline PackSymGroup sym_group_from_tag(PackSymTag t) {
    switch (t) {
        case PS_Function: return SG_Function;
        case PS_Class:    return SG_Class;
        case PS_Struct:   return SG_Struct;
        case PS_Enum:     return SG_Enum;
        case PS_Trait:    return SG_Trait;
        case PS_Extern:   return SG_Extern;
        default:          return SG_Var;
    }
}

static const char* const SG_EXT[PACK_SG_COUNT] = {
    "var", "func", "class", "struct", "enum", "trait", "extern", "all", "err"
};


static char* fp_build_root(void) {
    if (FP_LINK_OVERRIDE) return arr_strdup(FP_LINK_OVERRIDE);  // config wins

    char* local = dir_get_env("LOCALAPPDATA");
    if (!local) return NULL;
    char* result = dir_build_path(local, "vix", "build", NULL);
    free(local);
    return result;
}

static DirErr fp_ensure_tree(const char* project) {
    char* root = fp_build_root();
    if (!root) return DIR_ERR_IO; // if an rror dir should giv th rrror and print and call diacngos.c to print th rror

    char* proj  = dir_build_path(root, project, NULL);
    char* pack  = dir_build_path(proj, "pack",  NULL);
    char* uint_ = dir_build_path(proj, "uint",  NULL);
    char* data  = dir_build_path(proj, "data",  NULL);

    DirErr e = DIR_OK;

    if ((e = dir_ensure(root))  != DIR_OK) free(root);
    if ((e = dir_ensure(proj))  != DIR_OK) free(proj);
    if ((e = dir_ensure(pack))  != DIR_OK) free(pack);
    if ((e = dir_ensure(uint_)) != DIR_OK) free(uint_);
    if ((e = dir_ensure(data))  != DIR_OK) free(data);

    return e;
}

static char* fp_pack_path(const char* project, const char* source, PackSymGroup g) {
    char* root = fp_build_root();
    size_t cap = strlen(source) + strlen(SG_EXT[g]) + 16;
    Filename filename = {0};

    ARR_ENSURE_CAP(filename, cap);
    snprintf(filename.data, cap, "%s.%s.pack", source, SG_EXT[g]);
    char* path = dir_build_path(root, project, "pack", filename.data, NULL);
    ARR_FREE(filename);
    free(root);
    return path;
}

static char* fp_uint_path(const char* project, const char* source, PackSymGroup g, uint32_t chunk) {
    char* root = fp_build_root();
    Filename filename = {0};
    size_t cap = strlen(source) + strlen(SG_EXT[g]) + 16;

    ARR_ENSURE_CAP(filename, cap);
    snprintf(filename.data, cap, "%s.%s.p%u", source, SG_EXT[g], chunk);
    char* path = dir_build_path(root, project, "uint", filename.data, NULL);
    ARR_FREE(filename);
    free(root);
    return path;
}

static char* fp_data_path(const char* project, const char* source) {
    char* root = fp_build_root();
    Filename filename = {0};
    size_t cap = strlen(source) + 16;

    ARR_ENSURE_CAP(filename, cap);
    snprintf(filename.data, cap, "%s.vix.tmp", source);
    char* path = dir_build_path(root, project, "data", filename.data, NULL);
    ARR_FREE(filename);
    free(root);
    return path;
}

static char* fp_err_path(const char* project, const char* source) {
    char* root = fp_build_root();
    Filename filename = {0};
    size_t cap = strlen(source) + 16;

    ARR_ENSURE_CAP(filename, cap);
    snprintf(filename.data, cap, "%s.err.pack", source);
    char* path = dir_build_path(root, project, "data", filename.data, NULL);
    ARR_FREE(filename);
    free(root);
    return path;
}


static void gw_init(GroupWriters* gw, const char* project_name, const char* source_file, uint64_t source_hash) {
    for (int i = 0; i < PACK_SG_COUNT; i++) {
        gw->writers[i] = pack_writer_new();
        gw->sizes[i] = 0;
        gw->chunks[i]  = 1;
    }

    gw->project_name = project_name;
    gw->source_file  = source_file;
    gw->source_hash  = source_hash;
}

static void gw_flush_uint(GroupWriters* gw, PackSymGroup g) {
    char* path = fp_uint_path(gw->project_name, gw->source_file, g, gw->chunks[g]);

    pack_writer_flush(&gw->writers[g], path, gw->source_hash);
    pack_writer_free(&gw->writers[g]);
    gw->writers[g] = pack_writer_new();
    gw->sizes[g]   = 0;
    gw->chunks[g]++;

    free(path);
}

static void gw_add(GroupWriters* gw, PackSymbol sym, PackSymGroup g) {
    if (gw->sizes[g] + sizeof(PackSymbol) > UINT_CHUNK_LIMIT)
        gw_flush_uint(gw, g);

    pack_writer_add_symbol(&gw->writers[g], sym);
    gw->sizes[g] += sizeof(PackSymbol);
}

static void gw_flush_all_uints(GroupWriters* gw) {
    for (int i = 0; i < SG_All; i++) {
        if (gw->writers[i].symbols.len == 0) continue;
        gw_flush_uint(gw, (PackSymGroup)i);
    }
}

static bool pipe_file_contents(const char* in_path, ByteBuf* out_bb, uint64_t* size_out) {
    FileData fd = {0};
    if (dir_read_file(in_path, &fd) != DIR_OK) return false;

    bb_push_raw(out_bb, fd.data, fd.len);
    if (size_out) *size_out = (uint64_t)fd.len;

    ARR_FREE(fd);
    return true;
}

static bool pipe_fd(int in_fd, int out_fd, uint64_t* total_out) {
    StrTab buf = {0};
    ARR_ENSURE_CAP(buf, PACK_CHUNK_SIZE);
    ssize_t n;
    uint64_t total = 0;

    while ((n = read(in_fd, buf.data, buf.cap)) > 0) {
        write(out_fd, buf.data, n);
        total += (uint64_t)n;
    }

    ARR_FREE(buf);
    if (total_out) *total_out = total;
    return true;
}


static char* func_uint_path(const char* project, const char* source, const char* func_name, uint32_t func_chunk, PackSymGroup g, uint32_t chunk) {
    char* root = fp_build_root();
    size_t cap = strlen(source) + strlen(func_name) + strlen(SG_EXT[g]) + 64;
    Filename filename = {0};

    ARR_ENSURE_CAP(filename, cap);
    snprintf(filename.data, cap, "%s.%s.f%u.%s.p%u", source, func_name, func_chunk, SG_EXT[g], chunk);
    
    char* path = dir_build_path(root, project, "uint", filename.data, NULL);
    ARR_FREE(filename);
    free(root);
    return path;
}

#endif