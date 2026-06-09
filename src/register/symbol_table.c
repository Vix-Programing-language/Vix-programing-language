#include "symbol_table.h"
#include "import.h"

SymTable *sym_table = NULL; 
void symbol_table_init(void) {
    if (sym_table) symtable_destroy(sym_table);
    sym_table = symtable_init();
}

void symbol_table_set(uint32_t id, LLVMValueRef alloca) {
    if (!sym_table) {
        return;
    }

    int absent;
    khint_t k = symtable_put(sym_table, id, &absent);


    kh_val(sym_table, k) = alloca;
}

LLVMValueRef symbol_table_get(uint32_t id) {
    khint_t k = symtable_get(sym_table, id);
    if (k == kh_end(sym_table)) return NULL;
    return kh_val(sym_table, k);
}

void symbol_table_clear(void) {
    if (sym_table) symtable_clear(sym_table);
}