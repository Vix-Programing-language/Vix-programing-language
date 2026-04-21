#include "file_manager.h"

FileManager file_manager_new(void) {
    return (FileManager){
        .path_to_id = file_path_map_init(),
    };
}

bool file_manager_has(const FileManager* self, const char* path) {
    return self && self->path_to_id && file_path_map_get(self->path_to_id, path) != kh_end(self->path_to_id);
}

void file_manager_set_line_starts(FileManager* self, FileId id, LineStarts line_starts) {
    ManagedFile* file = file_manager_get(self, id);
    ASSERT(file != NULL);

    free(file->line_starts.data);
    file->line_starts = line_starts;
}

void file_manager_free(FileManager* self) {
    if (!self) {
        return;
    }

    for (size_t i = 0; i < self->slots.len; ++i) {
        ManagedFile* file = &self->slots.data[i];
        if (!file->is_active) {
            continue;
        }

        free(file->path);
        free(file->line_starts.data);
    }

    free(self->slots.data);
    file_path_map_destroy(self->path_to_id);
    *self = (FileManager){0};
}

FileId file_manager_add(FileManager* self, const char* path, const char* source) {
    ASSERT(!file_manager_has(self, path));

    FileId id = self->slots.len;
    ManagedFile file = {
        .id = id,
        .path = strdup(path),
        .source = source,
        .is_active = true,
    };
    ARR_PUSH(self->slots, file);

    int absent = 0;
    khint_t it = file_path_map_put(self->path_to_id, self->slots.data[id].path, &absent);
    ASSERT(absent == 1);
    kh_val(self->path_to_id, it) = id;
    return id;
}

ManagedFile* file_manager_get(FileManager* self, FileId id) {
    if (!self || id >= self->slots.len) {
        return NULL;
    }

    ManagedFile* file = &self->slots.data[id];
    return file->is_active ? file : NULL;
}

const ManagedFile* file_manager_get_const(const FileManager* self, FileId id) {
    if (!self || id >= self->slots.len) {
        return NULL;
    }

    const ManagedFile* file = &self->slots.data[id];
    return file->is_active ? file : NULL;
}

bool file_manager_get_location(const FileManager* self, FileId id, const char* ptr, size_t* out_line, size_t* out_col) {
    const ManagedFile* file = file_manager_get_const(self, id);
    if (!file) {
        return false;
    }

    size_t line_index = get_line_num(&file->line_starts, (uintptr_t)ptr);
    if (line_index == (size_t)-1) {
        return false;
    }

    if (out_line) {
        *out_line = line_index + 1;
    }
    if (out_col) {
        *out_col = (size_t)(ptr - file->line_starts.data[line_index]) + 1;
    }
    return true;
}
