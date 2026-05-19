#include "config.h"
#include "import.h"
#include "pack.h"
#include "toml.h"
#include "dir.h"
#include "bytes.h"

static uint64_t read_u64_or_inf(toml_table_t *tbl, const char *key) {
    toml_datum_t d = toml_int_in(tbl, key); if (d.ok) return (uint64_t)d.u.i;
    toml_datum_t s = toml_string_in(tbl, key);

    if (s.ok) {
        bool is_inf = strcasecmp(s.u.s, "inf") == 0; free(s.u.s);

        if (is_inf) return UINT64_MAX;
    }

    return 0;
}

static void free_str_arr(StrArr *a) {
    if (!a->data) return;
    for (size_t i = 0; i < a->len; i++) free(a->data[i]);
    ARR_FREE(*a);
}

static ConfigBinKind parse_bin_kind(const char *s) {
    if (!s) return BIN_EXE;
    if (strcasecmp(s, "dll") == 0) return BIN_DLL;
    if (strcasecmp(s, "static") == 0) return BIN_STATIC;

    return BIN_EXE;
}

static void parse_info(toml_table_t *root, ConfigInfo *out) {
    toml_table_t *t = toml_table_in(root, "information"); if (!t) return;
    toml_datum_t d;

    if ((d = toml_string_in(t, "name")).ok) out->name = d.u.s;
    if ((d = toml_string_in(t, "description")).ok) out->description = d.u.s;
    if ((d = toml_string_in(t, "license")).ok) out->license = d.u.s;

    toml_array_t *authors = toml_array_in(t, "authors"); if (!authors) return;
    int n = toml_array_nelem(authors);

    for (int i = 0; i < n; i++) {
        toml_datum_t a = toml_string_at(authors, i);
        if (a.ok) ARR_PUSH(out->authors, a.u.s);
    }
}

static void parse_library(toml_table_t *root, ConfigLibrary *out) {
    toml_table_t *t = toml_table_in(root, "library"); if (!t) return;

    for (int i = 0; ; i++) {
        const char *key = toml_key_in(t, i); if (!key) break;
        ConfigDep dep = {0};

        dep.key = strdup(key);

        toml_table_t *sub = toml_table_in(t, key);
        if (sub) {
            toml_datum_t v = toml_string_in(sub, "v"); if (v.ok) dep.version = v.u.s;
            toml_datum_t lnk = toml_string_in(sub, "link"); if (lnk.ok) dep.link = lnk.u.s;
            toml_array_t *feats = toml_array_in(sub, "features");

            if (feats) {
                for (int fi = 0; fi < toml_array_nelem(feats); fi++) {
                    toml_datum_t f = toml_string_at(feats, fi);
                    if (f.ok) ARR_PUSH(dep.features, f.u.s);
                }
            }
        } else {
            toml_datum_t sv = toml_string_in(t, key);
            if (sv.ok) {
                dep.version = sv.u.s;
            } else {
                toml_datum_t bv = toml_bool_in(t, key);
                if (bv.ok && bv.u.b) {
                    dep.is_builtin = true;
                    dep.version    = strdup("builtin");
                }
            }
        }
        ARR_PUSH(out->deps, dep);
    }
}

static void parse_profile(toml_table_t *root, ConfigProfile *out) {
    toml_table_t *t = toml_table_in(root, "profile");
    if (!t) return;

    toml_datum_t d;
    if ((d = toml_bool_in(t, "debug")).ok)    out->debug    = d.u.b;
    if ((d = toml_bool_in(t, "release")).ok)  out->release  = d.u.b;
    if ((d = toml_bool_in(t, "terminal")).ok) out->terminal = d.u.b;
    if ((d = toml_bool_in(t, "arc")).ok)      out->arc      = d.u.b;
}

static void parse_workspace(toml_table_t *root, ConfigWorkspace *out) {
    toml_table_t *t = toml_table_in(root, "workspace");
    if (!t) return;

    toml_array_t *members = toml_array_in(t, "members");
    if (!members) return;

    int n = toml_array_nelem(members);
    for (int i = 0; i < n; i++) {
        toml_datum_t m = toml_string_at(members, i);
        if (m.ok) ARR_PUSH(out->members, m.u.s);
    }
}

static void parse_footprint(toml_table_t *root, ConfigFootprint *out) {
    toml_table_t *t = toml_table_in(root, "footprint");
    if (!t) return;

    toml_datum_t link = toml_string_in(t, "link");
    if (link.ok) out->link = link.u.s;

    toml_table_t *bin = toml_table_in(t, "bin");
    if (!bin) return;

    toml_datum_t size = toml_string_in(bin, "size");
    toml_datum_t file = toml_string_in(bin, "file");
    if (size.ok) out->size = size.u.s;
    if (file.ok) out->file = file.u.s;

    out->low = read_u64_or_inf(bin, "low");
    out->max = read_u64_or_inf(bin, "max");
}

static void parse_compiler(toml_table_t *root, ConfigCompiler *out) {
    toml_table_t *t = toml_table_in(root, "compiler"); if (!t) return;
    toml_datum_t d;

    if ((d = toml_string_in(t, "version")).ok) out->version = d.u.s;
    if ((d = toml_int_in(t,    "optimization")).ok) out->optimization = (uint8_t)d.u.i;
    if ((d = toml_string_in(t, "target")).ok) out->target = d.u.s;

    toml_table_t *mem = toml_table_in(t, "memory"); if (!mem) return;

    out->memory.low = read_u64_or_inf(mem, "low");
    out->memory.max = read_u64_or_inf(mem, "max");
}


void config_serialize(ByteBuf *bb, const Config *c) {
    bb_u8(bb, c->valid ? 1 : 0);
    if (!c->valid) return;

    bb_str(bb, c->info.name);
    bb_str(bb, c->info.license);
    bb_u16(bb, (uint16_t)c->info.authors.len);

    for (size_t i = 0; i < c->info.authors.len; i++) bb_str(bb, c->info.authors.data[i]); bb_u16(bb, (uint16_t)c->library.deps.len);
    for (size_t i = 0; i < c->library.deps.len; i++) {
        ConfigDep *d = &c->library.deps.data[i];
        bb_str(bb, d->key);
        bb_str(bb, d->version);
        bb_u8(bb, d->is_builtin ? 1 : 0);
    }
}

Config config_deserialize(const uint8_t *data, size_t len) {
    Config c = {0};
    ByteReader br = br_make(data, len);

    if (br_u8(&br) == 0) return c;

    c.info.name    = br_str(&br);
    c.info.license = br_str(&br);

    uint16_t n_auth = br_u16(&br); for (uint16_t i = 0; i < n_auth; i++) ARR_PUSH(c.info.authors, br_str(&br));
    uint16_t n_deps = br_u16(&br);

    for (uint16_t i = 0; i < n_deps; i++) {
        ConfigDep d  = {0};
        d.key        = br_str(&br);
        d.version    = br_str(&br);
        d.is_builtin = (br_u8(&br) == 1);
        ARR_PUSH(c.library.deps, d);
    }

    c.valid = br.ok;
    return c;
}

uint8_t config_pack_own_flags(const Config* c) {
    uint8_t flags = 0;
    if (c->info.name) flags |= PACK_OWN_NAME;
    if (c->footprint.link) flags |= PACK_OWN_FOOTPRINT;
    return flags;
}

Config config_parse(const char *path) {
    Config c = {0};
    FileData raw = {0};
    char errbuf[200];

    if (dir_read_file(path, &raw) != DIR_OK) return c; ARR_PUSH(raw, '\0');
    toml_table_t *root = toml_parse((char *)raw.data, errbuf, sizeof(errbuf));
    ARR_FREE(raw);

    if (!root) { fprintf(stderr, "[config] error: %s\n", errbuf); return c; }

    parse_info(root, &c.info);
    parse_library(root, &c.library);
    parse_compiler(root, &c.compiler);
    parse_profile(root, &c.profile);
    parse_workspace(root, &c.workspace);
    parse_footprint(root, &c.footprint);


    toml_free(root);
    c.valid = true;
    return c;
}

void config_free(Config *c) {
    if (!c) return;

    free(c->info.name);
    free(c->info.description);
    free(c->info.license);
    free_str_arr(&c->info.authors);

    for (size_t i = 0; i < c->library.deps.len; i++) {
        ConfigDep *d = &c->library.deps.data[i];
        free(d->key);
        free(d->version);
        free(d->link);
        free_str_arr(&d->features);
    }

    ARR_FREE(c->library.deps);

    free(c->compiler.version);
    free(c->compiler.target);
    free_str_arr(&c->workspace.members);

    // footprint
    free(c->footprint.size);
    free(c->footprint.file);
    free(c->footprint.link);

    // compiler new fields
    free(c->compiler.compiler_ref.dir);
    free(c->compiler.compiler_ref.env);
    free(c->compiler.backend.compiler);
    free(c->compiler.flags.build);
    free(c->compiler.flags.test);
    memset(c, 0, sizeof(*c));
}

Config config_parse_upwards(const char* source_file) {
    char* path = NULL;

    if (dir_find_upwards(source_file, "config.toml", &path) != DIR_OK) return (Config){0};
    Config c = config_parse(path);

    free(path);
    return c;
}