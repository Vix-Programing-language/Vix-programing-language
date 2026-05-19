#include "pack.h"
#include "config.h"
#include "import.h"
#include "bytes.h"
#include "dir.h"
#include "project_pack_builder.h"
#include "project_pack_reader.h"
#include "project_pack_verifier.h"

static uint32_t crc32_update(uint32_t crc, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t*)data;
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc;
}

char* project_pack_path(const char* source_file) {
    size_t slen = strlen(source_file) + 1;
    Dir dir = {0};
    char* result = dir_build_path(dir.data, ".pack", NULL);

    ARR_ENSURE_CAP(dir, slen);
    dir.len = slen;

    memcpy(dir.data, source_file, slen);
    dir_path_parent(dir.data);

    ARR_FREE(dir);
    return result;
}

static char* arr_strdup(const char* s) {
    if (!s) return NULL;

    size_t len = strlen(s) + 1;
    Dir buf = {0};

    ARR_ENSURE_CAP(buf, len);
    buf.len = len;
    memcpy(buf.data, s, len);
    char* result = buf.data;
    buf.data = NULL;
    return result;
}

void project_pack_free(ProjectPack* p) {
    if (!p) return;
    free(p->name);
    free(p->root);
    free(p->footprint_link);
    memset(p, 0, sizeof(*p));
}

bool project_pack_should_delete(const ProjectPack* p) {
    return p && (p->ownership_flags == PACK_OWN_ALL);
}

ProjectPack project_pack_read(const char* pack_path) {
    ProjectPack result = {0};
    FileData fd = {0};

    if (dir_read_file(pack_path, &fd) != DIR_OK) return result;
    if (fd.len < 12) { ARR_FREE(fd); return result; }

    uint32_t stored_crc; memcpy(&stored_crc, fd.data + fd.len - 4, 4);
    uint32_t computed_crc = crc32_update(0, fd.data, fd.len - 4);
    size_t fb_size = fd.len - 4;

    if (stored_crc != computed_crc) { ARR_FREE(fd); return result; }
    if (VixPack_ProjectPackMeta_verify_as_root(fd.data, fb_size) != flatcc_verify_ok) { ARR_FREE(fd); return result; }

    VixPack_ProjectPackMeta_table_t meta = VixPack_ProjectPackMeta_as_root(fd.data);
    flatbuffers_string_t name = VixPack_ProjectPackMeta_name(meta);
    result.source_hash = VixPack_ProjectPackMeta_source_hash(meta);
    result.ownership_flags = VixPack_ProjectPackMeta_ownership_flags(meta);

    if (name) result.name = arr_strdup(name);

    flatbuffers_string_t root = VixPack_ProjectPackMeta_root(meta); result.root = arr_strdup(root ? root : ".");
    flatbuffers_string_t fpl = VixPack_ProjectPackMeta_footprint_link(meta); if (fpl) result.footprint_link = arr_strdup(fpl);

    ARR_FREE(fd);
    result.valid = true;
    return result;
}

bool project_pack_write(const char* pack_path, const ProjectPack* p) {
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    VixPack_ProjectPackMeta_start_as_root(&builder);
    VixPack_ProjectPackMeta_source_hash_add(&builder, p->source_hash);
    VixPack_ProjectPackMeta_ownership_flags_add(&builder, (uint8_t)p->ownership_flags);

    if (!(p->ownership_flags & PACK_OWN_NAME) && p->name)
        VixPack_ProjectPackMeta_name_create_str(&builder, p->name);
    if (!(p->ownership_flags & PACK_OWN_PATH))
        VixPack_ProjectPackMeta_root_create_str(&builder, p->root ? p->root : ".");
    if (!(p->ownership_flags & PACK_OWN_FOOTPRINT) && p->footprint_link)
        VixPack_ProjectPackMeta_footprint_link_create_str(&builder, p->footprint_link);

    VixPack_ProjectPackMeta_end_as_root(&builder);

    size_t fb_size = 0;
    void* fb_data = flatcc_builder_finalize_buffer(&builder, &fb_size);
    flatcc_builder_clear(&builder);

    if (!fb_data) return false;

    ByteBuf b = {0};
    bb_push_raw(&b, fb_data, fb_size);
    free(fb_data);

    uint32_t crc = crc32_update(0, b.data, b.len);
    bb_u32(&b, crc);

    DirErr err = dir_write_file(pack_path, b.data, b.len);
    bb_free(&b);
    return err == DIR_OK;
}

ProjectPack project_pack_sync(const char* source_file, const char* project_name, uint64_t source_hash, uint8_t config_owns_flags, const char* footprint_link, const char* project_root) {
    char* pack_path = dir_hidden_path(project_root, ".project.pack");
    ProjectPack p = {0};
    bool dirty = false;

    if (dir_exists(pack_path)) {
        p = project_pack_read(pack_path);

        if (p.valid) {
            bool name_changed = false;
            if (project_name && p.name) name_changed = strcmp(project_name, p.name) != 0;
            else if (project_name || p.name) name_changed = true;

            if (name_changed) {
                dir_delete(pack_path);
                project_pack_free(&p);
                memset(&p, 0, sizeof(p));
            }
        }
    }

    if (!p.valid) {
        dirty = true;
    } else {
        if (p.source_hash != source_hash) dirty = true;
        if (p.ownership_flags != config_owns_flags) dirty = true;
        if (p.root && !dir_exists(p.root)) { free(p.root); p.root = arr_strdup("."); dirty = true; }
    }

    if (dirty) {
        free(p.name);
        p.name = arr_strdup(project_name);

        if (!p.root && !(config_owns_flags & PACK_OWN_PATH)) p.root = arr_strdup(".");
        if (!p.footprint_link && footprint_link && !(config_owns_flags & PACK_OWN_FOOTPRINT)) p.footprint_link = arr_strdup(footprint_link);

        p.source_hash = source_hash;
        p.ownership_flags  = config_owns_flags;
        p.valid = true;

        project_pack_write(pack_path, &p);

        if (project_pack_should_delete(&p)) {
            dir_delete(pack_path);
            project_pack_free(&p);
            free(pack_path);
            return (ProjectPack){0};
        }
    }

    free(pack_path);
    return p;
}

ProjectPack project_pack_sync_from_config(const char* source_file, uint64_t source_hash, const Config* cfg) {
    if (!cfg || !cfg->valid) return project_pack_sync(source_file, NULL, source_hash, 0, NULL, ".");

    return project_pack_sync(source_file, cfg->info.name, source_hash, (uint8_t)config_pack_own_flags(cfg), cfg->footprint.link, ".");
}