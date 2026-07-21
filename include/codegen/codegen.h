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
    LLVMTypeRef *param_type;
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



RegisterEntry* register_by_target(StringView name, int* tags, size_t tags_count);

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
typedef struct { char* name; size_t variants_count; LLVMTypeRef* payload_types; size_t payload_count; } Codegen_Enum;

static LLVMModuleRef module;
static LLVMBuilderRef builder;

LLVMContextRef llvm_ctx;
LLVMModuleRef llvm_mod;
LLVMBuilderRef llvm_builder;
const char *codegen_source = NULL;

LLVMTypeRef str_type;

void codegen_new(const char *filename, const char *source) {
    llvm_ctx = LLVMContextCreate();
    llvm_mod = LLVMModuleCreateWithNameInContext(filename, llvm_ctx);
    llvm_builder = LLVMCreateBuilderInContext(llvm_ctx);
    codegen_source = source;

    symbol_table_init();
}

LLVMTypeRef set_custom_type(Type t);
LLVMTypeRef get_or_create_enum_type(RegisterEntry* enum_entry, const char* name);


LLVMTypeRef get_or_create_enum_type(RegisterEntry* enum_entry, const char* name) {
    LLVMTypeRef existing = LLVMGetTypeByName2(llvm_ctx, name);
    if (existing) return existing;

    size_t max_fields = 0;
    for (size_t i = 0; i < enum_entry->data.enm.variants_count; i++) if (enum_entry->data.enm.variants[i].fields_count > max_fields) max_fields = enum_entry->data.enm.variants[i].fields_count;

    size_t total = 1 + max_fields;
    LLVMTypeRef* body = malloc(sizeof(LLVMTypeRef) * total);
    body[0] = LLVMInt32TypeInContext(llvm_ctx);

    for (size_t i = 0; i < enum_entry->data.enm.variants_count; i++) {
        if (enum_entry->data.enm.variants[i].fields_count == max_fields) {
            for (size_t j = 0; j < max_fields; j++) body[j + 1] = set_custom_type(enum_entry->data.enm.variants[i].fields[j].type);
            break;
        }
    }

    LLVMTypeRef enum_ty = LLVMStructCreateNamed(llvm_ctx, name);
    LLVMStructSetBody(enum_ty, body, (unsigned)total, 0);
    free(body);
    return enum_ty;
}

LLVMTypeRef set_type(Type t) {
    switch (t.tag) {
        case Type_Int:   return LLVMIntTypeInContext(llvm_ctx, t.data.int_t.bits);
        case Type_Float: return t.data.float_t.bits == 64 ? LLVMDoubleTypeInContext(llvm_ctx) : LLVMFloatTypeInContext(llvm_ctx);
        case Type_Bool:  return LLVMInt1TypeInContext(llvm_ctx);
        case Type_Char:  return LLVMInt8TypeInContext(llvm_ctx);
        case Type_Void:  return LLVMVoidTypeInContext(llvm_ctx);
        case Type_Ptr:
        case Type_RawPtr: return LLVMPointerType(LLVMInt8TypeInContext(llvm_ctx), 0);
        case Type_Tuple: {
            LLVMTypeRef *elems = malloc(sizeof(LLVMTypeRef) * t.data.tuple.elems_count);
            for (size_t i = 0; i < t.data.tuple.elems_count; i++) elems[i] = set_custom_type(t.data.tuple.elems[i]);
            LLVMTypeRef result = LLVMStructTypeInContext(llvm_ctx, elems, t.data.tuple.elems_count, 0);
            free(elems);
            return result;
        }

        case Type_Str: {
            LLVMTypeRef field_types[] = { LLVMPointerType(LLVMInt8TypeInContext(llvm_ctx), 0), LLVMInt64TypeInContext(llvm_ctx), };
            return LLVMStructTypeInContext(llvm_ctx, field_types, 2, 0);
        }

        case Type_FnPtr: {
            LLVMTypeRef *param_tys = malloc(sizeof(LLVMTypeRef) * t.data.fn_ptr.params_count);
            for (size_t i = 0; i < t.data.fn_ptr.params_count; i++) param_tys[i] = set_custom_type(t.data.fn_ptr.params[i]);
            
            LLVMTypeRef ret_ty = t.data.fn_ptr.ret ? set_custom_type(*t.data.fn_ptr.ret) : LLVMVoidTypeInContext(llvm_ctx);
            LLVMTypeRef fn_ty = LLVMFunctionType(ret_ty, param_tys, t.data.fn_ptr.params_count, 0);
            free(param_tys);
            return LLVMPointerType(fn_ty, 0);
        }

        case Type_Array: {
            if (!t.data.array_t.inner) {
                return LLVMInt32TypeInContext(llvm_ctx);
            }
            LLVMTypeRef elem_ty = set_custom_type(*t.data.array_t.inner);
            return LLVMArrayType(elem_ty, (unsigned int)t.data.array_t.len);
        }
        
        case Type_Custom: { 
            const char* type_name = null_terminated(t.data.custom.name);
            LLVMTypeRef struct_ty = LLVMGetTypeByName2(llvm_ctx, type_name);
            if (struct_ty) return struct_ty;

            StringView sv = { .ptr = type_name, .len = strlen(type_name) };
            int tags[] = { Reg_Enum };
            RegisterEntry* enum_entry = register_by_target(sv, tags, 1);
            if (enum_entry) return get_or_create_enum_type(enum_entry, type_name);

            return LLVMPointerType(LLVMInt8TypeInContext(llvm_ctx), 0);
        }
        default: return LLVMInt32TypeInContext(llvm_ctx);
    }
}

LLVMTypeRef set_custom_type(Type t) {
    if (t.tag == Type_Custom) {
        const char* sname = null_terminated(t.data.custom.name);
        LLVMTypeRef struct_ty = LLVMGetTypeByName2(llvm_ctx, sname);
        if (struct_ty) {
            return struct_ty;
        }

        StringView sv = { .ptr = sname, .len = strlen(sname) };
        int tags[] = { Reg_Enum };
        RegisterEntry* enum_entry = register_by_target(sv, tags, 1);
        if (enum_entry) {
            return get_or_create_enum_type(enum_entry, sname);
        }
    }
    return set_type(t);
}

LLVMTypeRef *set_fields(IR_FieldDef *fields, size_t count) {
    ARR(LLVMTypeRef) field_types = {0};

    for (size_t i = 0; i < count; i++) {
        ARR_PUSH(field_types, set_custom_type(fields[i].ty));
    }

    return field_types.data;
}

LLVMTypeRef *set_param(IR_Param *param, uint32_t count) {
    ARR(LLVMTypeRef) llvm_param_types = {0};

    for (size_t i = 0; i < count; i++) {
        ARR_PUSH(llvm_param_types, set_custom_type(param[i].ty));
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


LLVMValueRef codegen_expr_array(Register *reg, IR_Expr *expr);
LLVMValueRef codegen_expr_call(Register *reg, IR_Expr *expr);
LLVMValueRef codegen_expr_tuple(Register *reg, IR_Expr *expr);
LLVMValueRef codegen_expr_tupleindex(Register *reg, IR_Expr *expr);
LLVMValueRef codegen_expr_index(Register *reg, IR_Expr *expr);
LLVMValueRef codegen_expr_make_struct(Register *reg, IR_Expr *expr);
LLVMValueRef codegen_expr_make_enum(Register *reg, IR_Expr *expr);

LLVMValueRef codegen_expr_var(Register *reg, IR_Expr *expr);
LLVMValueRef codegen_expr_method(Register *reg, IR_Expr *expr);


void codegen_generate_function(Register *reg, uint32_t *param_ids, Codegen_FuncDef fn);
LLVMValueRef codegen_expr(Register *reg, IR_Expr *expr);
void codegen_stmts(Register *reg, IR_Stmt *stmts, size_t count);

void codegen_generate_var(uint32_t id, Codegen_Var v);
void codegen_generate_let(uint32_t id, Codegen_Let l);
void codegen_generate_const(uint32_t id, Codegen_Const c, bool globle);

IR_Module codegen_def(Register *reg, IR_Module mod);


void codegen_generate_struct(Codegen_Struct s);
void codegen_generate_extern(Codegen_Extern e);
void codegen_generate_enum(Codegen_Enum e);
void codegen_generate_if(Register *reg, LLVMValueRef cond, struct IR_Stmt *body, size_t body_count,struct IR_Stmt *else_body, size_t else_count);
void codegen_generate_while(Register *reg, struct IR_Expr *cond_expr, struct IR_Stmt *body, size_t body_count);
void codegen_generate_let(uint32_t id, Codegen_Let l);
void codegen_generate_for(Register *reg, struct IR_Expr *iter_expr, struct IR_Stmt *body, size_t body_count);
void codegen_generate_match(Register *reg, LLVMValueRef match_val, IR_MatchArm *arms, size_t arms_count, struct IR_Stmt *default_body, size_t default_body_count);

#endif
