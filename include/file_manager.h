#ifndef VIX_FILE_MANAGER_H
#define VIX_FILE_MANAGER_H

#include "token/ast/ast.h"
#include "third-party/khashl.h"

typedef size_t FileId;

typedef struct {
    FileId id;
    char* path;
    const char* source;
    LineStarts line_starts;
    bool is_active;
} ManagedFile;

KHASHL_MAP_INIT(KH_LOCAL, FilePathMap, file_path_map, kh_cstr_t, FileId, kh_hash_str, kh_eq_str)

typedef struct {
    FilePathMap* path_to_id;
    ARR(ManagedFile) slots;
    ARR(FileId) files;
} FileManager;

FileManager file_manager_new(void);
void file_manager_free(FileManager* self);
FileId file_manager_add(FileManager* self, const char* path, const char* source);
bool file_manager_has(const FileManager* self, const char* path);
void file_manager_set_line_starts(FileManager* self, FileId id, LineStarts line_starts);
ManagedFile* file_manager_get(FileManager* self, FileId id);
const ManagedFile* file_manager_get_const(const FileManager* self, FileId id);
bool file_manager_get_location(const FileManager* self, FileId id, const char* ptr, size_t* out_line, size_t* out_col);

#endif
