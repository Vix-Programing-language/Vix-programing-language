#pragma once
#include "third-party/khashl.h"
#include <llvm-c/Core.h>

KHASHL_MAP_INIT(KH_LOCAL, SymTable, symtable, uint32_t, LLVMValueRef, kh_hash_uint32, kh_eq_generic)

extern SymTable *sym_table;

void symbol_table_init(void);
void symbol_table_set(uint32_t id, LLVMValueRef alloca);
LLVMValueRef symbol_table_get(uint32_t id);
void symbol_table_clear(void);