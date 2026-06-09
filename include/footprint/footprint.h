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
    uint32_t* chunks;
    bool* group_present;
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

extern const char* const SG_EXT[PACK_SG_COUNT];

#ifdef __cplusplus
extern "C" {
#endif

PackWriter pack_writer_new(void);
bool pack_writer_flush(PackWriter* w, const char* path, uint64_t source_hash);
void pack_writer_free(PackWriter* w);
void pack_writer_add_symbol(PackWriter* w, PackSymbol sym);

PackSymGroup reg_tag_to_group(RegisterEntryTag t);
char* arr_strdup(const char* s);
PackSymGroup sym_group_from_tag(PackSymTag t);
char* fp_build_root(void);
DirErr fp_ensure_tree(const char* project);
char* fp_pack_path(const char* project, const char* source, PackSymGroup g);
char* fp_uint_path(const char* project, const char* source, PackSymGroup g, uint32_t chunk);
char* fp_data_path(const char* project, const char* source);
char* fp_err_path(const char* project, const char* source);

void gw_init(GroupWriters* gw, const char* project_name, const char* source_file, uint64_t source_hash);
void gw_flush_uint(GroupWriters* gw, PackSymGroup g);
void gw_add(GroupWriters* gw, PackSymbol sym, PackSymGroup g);
void gw_flush_all_uints(GroupWriters* gw);
bool pipe_file_contents(const char* in_path, ByteBuf* out_bb, uint64_t* size_out);
bool pipe_fd(int in_fd, int out_fd, uint64_t* total_out);
const char* fp_base_name(const char* source);

char *fp_obj_path(const char *project, const char *source_file);
char *fp_exe_path(const char *project, const char *source_file);
char* func_uint_path(const char* project, const char* source, const char* func_name, uint32_t func_chunk, PackSymGroup g, uint32_t chunk);
char* fp_snapshot_path(const char* project);
void fp_snapshot_write(const char* project, const char* source_file, const char* source, size_t source_len);
char* fp_snapshot_read(const char* project, const char* source_file);

#ifdef __cplusplus
}
#endif

#endif // FOOTPRINT_H