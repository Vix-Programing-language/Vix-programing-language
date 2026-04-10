#include "file_manager.h"

static void file_record_rebuild_line_starts(ManagedFile* file) {
    file->line_starts.len = 0;
    ARR_PUSH(file->line_starts, file->source);

    for (const char* cur = file->source; *cur != '\0'; ++cur) {
        if (*cur == '\n') {
            ARR_PUSH(file->line_starts, cur + 1);
        }
    }

    ARR_PUSH(file->line_starts, file->source + strlen(file->source) + 1);
}

FileManager file_manager_new(void) {
    return (FileManager){
        .path_to_id = file_path_map_init(),
    };
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
    free(self->files.data);
    file_path_map_destroy(self->path_to_id);
    *self = (FileManager){0};
}

FileId file_manager_add(FileManager* self, const char* path, const char* source) {
    khint_t it = file_path_map_get(self->path_to_id, path);
    if (it != kh_end(self->path_to_id)) {
        FileId id = kh_val(self->path_to_id, it);
        ManagedFile* file = &self->slots.data[id];
        file->source = source;
        file_record_rebuild_line_starts(file);
        return id;
    }

    FileId id = self->slots.len;
    ManagedFile file = {
        .id = id,
        .path = strdup(path),
        .source = source,
        .is_active = true,
    };
    file_record_rebuild_line_starts(&file);
    ARR_PUSH(self->slots, file);
    ARR_PUSH(self->files, id);

    int absent = 0;
    it = file_path_map_put(self->path_to_id, self->slots.data[id].path, &absent);
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
