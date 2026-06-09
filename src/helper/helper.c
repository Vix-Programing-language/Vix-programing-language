#include "import.h"
#include "register.h"


void range_to_span(SourceRange r, LineStarts* ls, uint32_t* line_start, uint16_t* col_start, uint32_t* line_end, uint16_t* col_end) {
    size_t ls_idx = get_line_num(ls, (uintptr_t)r.start);
    size_t le_idx = get_line_num(ls, (uintptr_t)r.end);

    *line_start = (ls_idx != (size_t)-1) ? (uint32_t)ls_idx : 0;
    *line_end = (le_idx != (size_t)-1) ? (uint32_t)le_idx : 0;

    *col_start = (ls_idx != (size_t)-1) ? (uint16_t)(r.start - ls->data[ls_idx]) : 0;
    *col_end = (le_idx != (size_t)-1) ? (uint16_t)(r.end - ls->data[le_idx])   : 0;
}

Type* type_heap(Type t) {
    switch (t.tag) {
        case Type_Void:
        case Type_Custom:
            return NULL;
        default: {
            Type* p = malloc(sizeof(Type));
            *p = t;
            return p;
        }
    }
}