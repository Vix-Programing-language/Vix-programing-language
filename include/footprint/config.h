#pragma once
#include "import.h"
#include "dir.h"

typedef struct {
    char    *name;
    char    *description;
    char    *license;
    StrArr   authors;
} ConfigInfo;

typedef struct {
    char    *version;
    char    *link;
    StrArr   features;
    bool     is_builtin;
    char    *key;
} ConfigDep;

typedef ARR(ConfigDep) ConfigDepArr;

typedef struct {
    ConfigDepArr deps;
} ConfigLibrary;

typedef struct {
    bool debug;
    bool release;
    bool terminal;
    bool arc;
} ConfigProfile;

typedef struct {
    uint64_t low;
    uint64_t max;
} ConfigMemory;

typedef struct {
    char *dir;
    char *env;
} ConfigCompilerRef;

typedef struct {
    char    *build;
    char    *test;
} ConfigFlags;

typedef enum { BIN_EXE, BIN_DLL, BIN_STATIC } ConfigBinKind;

typedef struct {
    char    *compiler;
    uint32_t llvm;
} ConfigBackend;

typedef struct {
    char            *version;
    uint8_t          optimization;
    ConfigMemory     memory;
    char            *target;
    bool             strict;
    ConfigCompilerRef compiler_ref;
    ConfigFlags      flags;
    ConfigBinKind    bin;
    ConfigBackend    backend;
} ConfigCompiler;

typedef struct {
    StrArr members;
} ConfigWorkspace;

typedef struct {
    char    *size;       // "10mb"
    uint64_t low;
    uint64_t max;
    char    *file;       // "40kb"
    char    *link;
} ConfigFootprint;

typedef struct {
    bool             valid;
    ConfigInfo       info;
    ConfigLibrary    library;
    ConfigProfile    profile;
    ConfigCompiler   compiler;
    ConfigWorkspace  workspace;
    ConfigFootprint  footprint;
} Config;

Config config_parse(const char *path);
void config_free(Config *c);
uint8_t config_pack_own_flags(const Config *c);
bool config_find(const char *start_path, char *out, size_t cap);
Config config_parse_upwards(const char* source_file);
