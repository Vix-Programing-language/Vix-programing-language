#ifndef VIX_CODEGEN_H
#define VIX_CODEGEN_H

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>

#include "ast.h"
#include "ir.h"
#include "import.h"
#include "symbol_table.h"

extern LLVMContextRef     llvm_ctx;
extern LLVMModuleRef      llvm_mod;
extern LLVMBuilderRef     llvm_builder;
extern const char *codegen_source;
extern LLVMTypeRef str_type;

typedef struct {
    char* name;
    EntityID eid;
    LLVMTypeRef return_type;
    LLVMTypeRef *params;
    size_t params_count;
    IR_Stmt *body;
    size_t body_count;
    bool is_pub;
    bool is_unsafe;
    LexerTokenTag operation_op;
    CallingConv   cc;
    const char   **param_names;
} Codegen_FuncDef;

typedef struct {
    const char  *name;
    LLVMTypeRef *fields;
    size_t       fields_count;
} Codegen_Struct;



char *null_terminated(SourceRange s) {
    size_t len = s.end - s.start;
    char *buf = malloc(len + 1);
    memcpy(buf, s.start, len);
    buf[len] = '\0';
    return buf;
}

typedef struct { const char *name; LLVMTypeRef type; LLVMValueRef init; } Codegen_Var;
typedef struct { const char *name; LLVMTypeRef type; LLVMValueRef init; } Codegen_Let;
typedef struct { const char *name; LLVMTypeRef type; LLVMValueRef init; } Codegen_Const;
typedef struct { const char *name; LLVMTypeRef *params; const char **param_names; uint32_t params_count; LLVMTypeRef return_type; } Codegen_Extern;
typedef struct { const char *name; size_t variants_count; } Codegen_Enum;

static LLVMModuleRef module;
static LLVMBuilderRef builder;

LLVMContextRef     llvm_ctx;
LLVMModuleRef      llvm_mod;
LLVMBuilderRef     llvm_builder;
const char *codegen_source = NULL;

LLVMTypeRef str_type;

void codegen_new(const char *filename, const char *source) {
    llvm_ctx = LLVMContextCreate();
    llvm_mod = LLVMModuleCreateWithNameInContext(filename, llvm_ctx);
    llvm_builder = LLVMCreateBuilderInContext(llvm_ctx);
    codegen_source = source;

    LLVMTypeRef str_field_types[] = { LLVMPointerType(LLVMInt8TypeInContext(llvm_ctx), 0), LLVMInt64TypeInContext(llvm_ctx) };
    str_type = LLVMStructCreateNamed(llvm_ctx, "str");
    LLVMStructSetBody(str_type, str_field_types, 2, 0);

    symbol_table_init();
}

LLVMTypeRef set_type(Type t) {
    switch (t.tag) {
        case Type_Int:   return LLVMIntTypeInContext(llvm_ctx, t.data.int_t.bits);
        case Type_Float: return t.data.float_t.bits == 64 ? LLVMDoubleTypeInContext(llvm_ctx) : LLVMFloatTypeInContext(llvm_ctx);
        case Type_Bool:  return LLVMInt1TypeInContext(llvm_ctx);
        case Type_Char:  return LLVMInt8TypeInContext(llvm_ctx);
        case Type_Str: {
            LLVMTypeRef field_types[] = { LLVMPointerType(LLVMInt8TypeInContext(llvm_ctx), 0), LLVMInt64TypeInContext(llvm_ctx), };
            return LLVMStructTypeInContext(llvm_ctx, field_types, 2, 0);
        }
        case Type_Void:  return LLVMVoidTypeInContext(llvm_ctx);
        case Type_Ptr:
        case Type_RawPtr: return LLVMPointerType(LLVMInt8TypeInContext(llvm_ctx), 0);
        case Type_Tuple: {
            LLVMTypeRef *elems = malloc(sizeof(LLVMTypeRef) * t.data.tuple.elems_count);
            for (size_t i = 0; i < t.data.tuple.elems_count; i++) elems[i] = set_type(t.data.tuple.elems[i]);
            LLVMTypeRef result = LLVMStructTypeInContext(llvm_ctx, elems, t.data.tuple.elems_count, 0);
            free(elems);
            return result;
        }

        case Type_Custom:  return LLVMGetTypeByName2(llvm_ctx, null_terminated(t.data.custom.name));
        default: return LLVMInt32TypeInContext(llvm_ctx);
    }
}


LLVMTypeRef *set_fields(IR_FieldDef *fields, size_t count) {
    ARR(LLVMTypeRef) field_types = {0};

    for (size_t i = 0; i < count; i++) {
        ARR_PUSH(field_types, set_type(fields[i].ty));
    }

    return field_types.data;
}

LLVMTypeRef *set_param(IR_Param *param, uint32_t count) {
    ARR(LLVMTypeRef) llvm_param_types = {0};

    for (size_t i = 0; i < count; i++) {
        ARR_PUSH(llvm_param_types, set_type(param[i].ty));
    }

    return llvm_param_types.data;
}

const char **set_param_names(IR_Param *param, uint32_t count) {
    ARR(const char*) names = {0};

    for (size_t i = 0; i < count; i++) {
        ARR_PUSH(names, null_terminated(param[i].name));
    }

    return names.data;
}

LLVMValueRef codegen_expr_literal(Register *reg, IR_Expr *expr);
LLVMValueRef codegen_expr_cast(Register *reg, IR_Expr *expr);
LLVMValueRef codegen_generate_str(Register *reg, IR_Expr *expr);

LLVMValueRef codegen_expr_field(Register *reg, IR_Expr *expr);
LLVMValueRef codegen_expr_addr(Register *reg, IR_Expr *expr);

LLVMValueRef codegen_expr_binop(Register *reg, IR_Expr *expr);
LLVMValueRef codegen_expr_unop(Register *reg, IR_Expr *expr);


LLVMValueRef codegen_expr_call(Register *reg, IR_Expr *expr);
LLVMValueRef codegen_expr_tuple(Register *reg, IR_Expr *expr);
LLVMValueRef codegen_expr_tupleindex(Register *reg, IR_Expr *expr);
LLVMValueRef codegen_expr_index(Register *reg, IR_Expr *expr);

LLVMValueRef codegen_expr_var(Register *reg, IR_Expr *expr);
LLVMValueRef codegen_expr_method(Register *reg, IR_Expr *expr);


void codegen_generate_function(Register *reg, uint32_t *param_ids, Codegen_FuncDef fn);
LLVMValueRef codegen_expr(Register *reg, IR_Expr *expr);
void codegen_stmts(Register *reg, IR_Stmt *stmts, size_t count);

void codegen_generate_var(uint32_t id, Codegen_Var v);
void codegen_generate_let(uint32_t id, Codegen_Let l);
void codegen_generate_const(uint32_t id, Codegen_Const c);


#endif