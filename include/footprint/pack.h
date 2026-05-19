#pragma once
#include "import.h"

#define PACK_META_MAGIC 0x4B434150u
#define PACK_META_VERSION 1u  

#define PACK_OWN_NAME (1u << 0)
#define PACK_OWN_PATH (1u << 1)
#define PACK_OWN_FOOTPRINT (1u << 2)
#define PACK_OWN_ALL (PACK_OWN_NAME | PACK_OWN_PATH | PACK_OWN_FOOTPRINT)


#define PACK_SG_COUNT 9
#define PACK_MAX_UNITS 256
#define PACK_MAX_CHUNKS 256


#define PACK_MAGIC 0x4B434150u
#define PACK_VERSION 1u
#define ERR_MAGIC 0x52524550u
#define ERR_VERSION 1u

typedef ARR(char) Dir;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t ownership_flags;
    uint16_t _pad;
} ProjectPackHeader;

typedef struct {
    char* name;
    char* root;
    char* footprint_link;
    uint64_t source_hash;
    uint8_t ownership_flags;
    bool valid;
} ProjectPack;

char* project_pack_path(const char* source_file);
bool project_pack_write(const char *pack_path, const ProjectPack *p);
ProjectPack project_pack_read(const char *pack_path);
void project_pack_free(ProjectPack *p);
bool project_pack_should_delete(const ProjectPack *p);