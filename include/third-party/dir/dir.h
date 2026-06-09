#ifndef DIR_H
#define DIR_H

#include "import.h"

typedef ARR(uint8_t) FileData;
typedef ARR(char*) StrArr;

typedef enum {
    DIR_OK = 0,
    DIR_ERR_IO,
    DIR_ERR_NOT_FOUND,
    DIR_ERR_ALREADY_EXISTS,
    DIR_ERR_PERMISSION,
    DIR_ERR_UNKNOWN
} DirErr;

typedef struct {
    char name[256];
    bool is_directory;
} DirEntry;

typedef ARR(DirEntry) DirList;

#include "pack.h"
#include "config.h"
#include "import.h"
#include "bytes.h"

static bool file_exists(const char *path) {
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

#ifdef __cplusplus
extern "C" {
#endif

DirErr dir_read_file(const char* path, FileData* out_data);
DirErr dir_write_file(const char* path, const void* data, size_t size);
DirErr dir_new_folder(const char* path);
DirErr dir_delete(const char* path);
DirErr dir_rename(const char* old_p, const char* new_p);
DirErr dir_read_folder(const char* path, DirList* out_list);
DirErr dir_ensure(const char* path);
DirErr dir_find_upwards(const char* start_path, const char* target_name, char** out);
DirErr dir_write_hidden_file(const char* path, const void* data, size_t size);
DirErr dir_ensure_hidden(const char* path);
bool dir_exists(const char* path);
bool dir_is_file(const char* path);
bool dir_is_dir(const char* path);
void dir_path_parent(char* path);
void dir_path_join(char* buf, size_t cap, const char* a, const char* b);
void dir_path_basename(const char* path, char* out, size_t cap);
void dir_path_ext(const char* path, char* out, size_t cap);
char* dir_build_path(const char* first, ...);
char* dir_get_env(const char* name);
char* dir_hidden_path(const char* dir_path, const char* filename);

#ifdef __cplusplus
}
#endif

#ifdef DIR_IMPLEMENTATION


static char dir__sep(void) {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

static bool dir__is_sep(char c) { return c == '/' || c == '\\'; }

bool dir_exists(const char* path) {
    struct stat st;

    return stat(path, &st) == 0;
}

bool dir_is_file(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;

    return (st.st_mode & S_IFMT) == S_IFREG;
}

bool dir_is_dir(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;

    return (st.st_mode & S_IFMT) == S_IFDIR;
}


void dir_path_parent(char* path) {
    char* last = NULL;
    for (char* p = path; *p; p++) if (dir__is_sep(*p)) last = p;
    if (!last) { path[0] = '.'; path[1] = '\0'; return; }
    if (last == path) { path[1] = '\0'; return; }
#ifdef _WIN32
    if (last == path + 2 && path[1] == ':') { path[3] = '\0'; return; }
#endif
    *last = '\0';
}

void dir_path_join(char* buf, size_t cap, const char* a, const char* b) {
    size_t la = strlen(a);
    bool trailing = (la > 0 && dir__is_sep(a[la - 1]));
    if (trailing) snprintf(buf, cap, "%s%s", a, b);
    else snprintf(buf, cap, "%s%c%s", a, dir__sep(), b);
}

void dir_path_basename(const char* path, char* out, size_t cap) {
    const char* last = path;
    for (const char* p = path; *p; p++) if (dir__is_sep(*p)) last = p + 1;
    strncpy(out, last, cap - 1);
    out[cap - 1] = '\0';
}

void dir_path_ext(const char* path, char* out, size_t cap) {
    size_t plen = strlen(path) + 1;
    Dir base = {0};
    ARR_ENSURE_CAP(base, plen);
    base.len = plen;
    dir_path_basename(path, base.data, plen);

    char* dot = strrchr(base.data, '.');
    if (!dot || dot == base.data) { out[0] = '\0'; ARR_FREE(base); return; }
    strncpy(out, dot + 1, cap - 1);
    out[cap - 1] = '\0';
    ARR_FREE(base);
}

DirErr dir_new_folder(const char* path) {
    if (VIX_MKDIR(path) == 0) return DIR_OK;
    if (errno == EEXIST) return DIR_ERR_ALREADY_EXISTS;
    if (errno == EACCES) return DIR_ERR_PERMISSION;
    return DIR_ERR_IO;
}

DirErr dir_ensure(const char* path) {
    if (dir_is_dir(path)) return DIR_OK;

    size_t plen = strlen(path) + 1;
    Dir tmp = {0};
    ARR_ENSURE_CAP(tmp, plen);
    tmp.len = plen;
    memcpy(tmp.data, path, plen);

    for (char* p = tmp.data + 1; *p; p++) {
        if (!dir__is_sep(*p)) continue;
        *p = '\0';
        if (!dir_is_dir(tmp.data)) {
            DirErr e = dir_new_folder(tmp.data);
            if (e != DIR_OK && e != DIR_ERR_ALREADY_EXISTS) { ARR_FREE(tmp); return e; }
        }
        *p = dir__sep();
    }

    DirErr e = dir_new_folder(tmp.data);
    ARR_FREE(tmp);
    return (e == DIR_ERR_ALREADY_EXISTS) ? DIR_OK : e;
}

DirErr dir_delete(const char* path) {
    if (remove(path) == 0) return DIR_OK;
    return DIR_ERR_IO;
}

DirErr dir_rename(const char* old_p, const char* new_p) {
    if (rename(old_p, new_p) == 0) return DIR_OK;
    return DIR_ERR_IO;
}

DirErr dir_read_folder(const char* path, DirList* out_list) {
#ifdef _WIN32
    size_t plen = strlen(path) + 3;
    Dir search = {0};
    ARR_ENSURE_CAP(search, plen);
    search.len = plen;
    snprintf(search.data, plen, "%s\\*", path);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search.data, &fd);
    ARR_FREE(search);

    if (h == INVALID_HANDLE_VALUE) return DIR_ERR_NOT_FOUND;
    do {
        if (fd.cFileName[0] == '.') continue;
        DirEntry entry = {0};
        strncpy(entry.name, fd.cFileName, sizeof(entry.name) - 1);
        entry.is_directory = !!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        ARR_PUSH(*out_list, entry);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(path);
    if (!d) return DIR_ERR_NOT_FOUND;
    struct dirent* de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        DirEntry entry = {0};
        strncpy(entry.name, de->d_name, sizeof(entry.name) - 1);
        entry.is_directory = (de->d_type == DT_DIR);
        ARR_PUSH(*out_list, entry);
    }
    closedir(d);
#endif
    return DIR_OK;
}


DirErr dir_read_file(const char* path, FileData* out_data) {
    FILE* f = fopen(path, "rb");
    if (!f) return (errno == ENOENT) ? DIR_ERR_NOT_FOUND : DIR_ERR_IO;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    if (sz < 0) { fclose(f); return DIR_ERR_IO; }

    ARR_MAKE_ROOM(*out_data, (size_t)sz);
    size_t got = fread(out_data->data + out_data->len, 1, (size_t)sz, f);
    fclose(f);

    if ((long)got != sz) return DIR_ERR_IO;
    out_data->len += got;
    return DIR_OK;
}

DirErr dir_write_file(const char* path, const void* data, size_t size) {
    size_t plen = strlen(path) + 5;
    Dir tmp = {0};
    ARR_ENSURE_CAP(tmp, plen);
    tmp.len = plen;
    snprintf(tmp.data, plen, "%s.tmp", path);

    FILE* f = fopen(tmp.data, "wb");
    if (!f) { ARR_FREE(tmp); return DIR_ERR_IO; }

    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size) { remove(tmp.data); ARR_FREE(tmp); return DIR_ERR_IO; }
    if (rename(tmp.data, path) != 0) { remove(tmp.data); ARR_FREE(tmp); return DIR_ERR_IO; }
    ARR_FREE(tmp);
    return DIR_OK;
}

DirErr dir_find_upwards(const char* start_path, const char* target_name, char** out) {
    size_t slen = strlen(start_path) + 1;
    Dir current = {0};
    ARR_ENSURE_CAP(current, slen);
    current.len = slen;
    memcpy(current.data, start_path, slen);

    if (dir_is_file(current.data)) dir_path_parent(current.data);

    while (true) {
        char* candidate = dir_build_path(current.data, target_name, NULL);
        if (dir_exists(candidate)) { *out = candidate; ARR_FREE(current); return DIR_OK; }
        free(candidate);

        size_t clen = strlen(current.data) + 1;
        Dir parent = {0};
        ARR_ENSURE_CAP(parent, clen);
        parent.len = clen;
        memcpy(parent.data, current.data, clen);
        dir_path_parent(parent.data);

        if (strcmp(parent.data, current.data) == 0) { ARR_FREE(parent); break; }

        ARR_FREE(current);
        current = parent;
    }

    ARR_FREE(current);
    *out = NULL;
    return DIR_ERR_NOT_FOUND;
}


char* dir_build_path(const char* first, ...) {
    va_list args;
    va_start(args, first);

    Dir buf = {0};
    ARR_PUSH(buf, '\0');

    const char* part = first;
    while (part != NULL) {
        if (buf.len > 1) {
            buf.data[buf.len - 1] = dir__sep();
            ARR_PUSH(buf, '\0');
        }
        size_t plen = strlen(part);
        ARR_MAKE_ROOM(buf, plen + 1);
        memcpy(buf.data + buf.len - 1, part, plen);
        buf.len += plen;
        buf.data[buf.len - 1] = '\0';
        part = va_arg(args, const char*);
    }

    va_end(args);
    char* result = buf.data;
    buf.data = NULL;
    return result; 
}

char* dir_get_env(const char* name) {
#ifdef _WIN32
    DWORD size = GetEnvironmentVariableA(name, NULL, 0);
    if (size == 0) return NULL;

    Dir buf = {0};
    ARR_ENSURE_CAP(buf, size);
    buf.len = size;
    GetEnvironmentVariableA(name, buf.data, size);

    char* result = buf.data;
    buf.data = NULL;
    return result;
#else
    const char* val = getenv(name);
    if (!val) return NULL;

    size_t len = strlen(val) + 1;
    Dir buf = {0};
    ARR_ENSURE_CAP(buf, len);
    buf.len = len;
    memcpy(buf.data, val, len);

    char* result = buf.data;
    buf.data = NULL;
    return result;
#endif
}

char* dir_hidden_path(const char* dir_path, const char* filename) {
    Dir name = {0};
    size_t flen = strlen(filename);

    if (filename[0] != '.') {
        ARR_ENSURE_CAP(name, flen + 2);
        name.data[0] = '.';
        memcpy(name.data + 1, filename, flen + 1);
        name.len = flen + 2;
    } else {
        ARR_ENSURE_CAP(name, flen + 1);
        memcpy(name.data, filename, flen + 1);
        name.len = flen + 1;
    }

    char* result = dir_build_path(dir_path, name.data, NULL);
    ARR_FREE(name);
    return result;
}

DirErr dir_write_hidden_file(const char* path, const void* data, size_t size) {
    DirErr err = dir_write_file(path, data, size);

#ifdef _WIN32
    if (err == DIR_OK)
        SetFileAttributesA(path, FILE_ATTRIBUTE_HIDDEN);
#endif

    return err;
}


#endif

#endif