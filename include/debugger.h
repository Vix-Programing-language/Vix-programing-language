#ifndef DEBUG_H
#define DEBUG_H

#include "import.h"
#include "ast.h"
#include "helper.h"

void print_type_inline(Type t) {
    switch (t.tag) {
        case Type_Ptr:    printf("*<inner>");               break;
        case Type_RawPtr: printf("**<inner>");              break;
        case Type_Bool:   printf("bool");                   break;
        case Type_Char:   printf("char");                   break;
        case Type_Str:    printf("str");                    break;
        case Type_Void:   printf("void");                   break;
        case Type_Int:
            printf("%sint%d",
                t.data.int_t.is_unsigned ? "u" : "",
                t.data.int_t.bits);
            break;
        case Type_Float:
            printf("float%d", t.data.float_t.bits);
            break;
        case Type_Array:
            printf("arr[%zu]", t.data.array_t.len);
            if (t.data.array_t.inner) {
                printf("<");
                print_type_inline(*t.data.array_t.inner);
                printf(">");
            }
            break;


case Type_Custom:
    printf("%.*s",
        (int)(t.data.custom.name.end - t.data.custom.name.start),
        t.data.custom.name.start);

    if (t.data.custom.generic_args_count > 0) {
        printf("[");
        for (size_t i = 0; i < t.data.custom.generic_args_count; i++) {

            ptrdiff_t len = t.data.custom.generic_args[i].end - t.data.custom.generic_args[i].start;
            printf("[DEBUG] arg[%zu] len=%td\n", i, len);

            if (len <= 0 || len > 256) {
                printf("<BAD_LEN>");
                continue;
            }

            printf("%.*s", (int)len, t.data.custom.generic_args[i].start);
            if (i + 1 < t.data.custom.generic_args_count) printf(", ");
        }
        printf("]");
    }
    break; default: printf("?(tag=%d)", t.tag); break;

    }
}

void print_type_inline(Type t);
void print_help(void);
void print_register_entry(Register* reg, RegisterEntry* e, int depth);
void print_expression(Exprs expr, int depth);
void print_statement(Stmts stmt, int depth);


#endif
