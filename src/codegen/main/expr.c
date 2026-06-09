#include "import.h"
#include "ast.h"
#include "register.h"
#include "ir.h"
#include "codegen.h"


#include <llvm-c/Core.h>


LLVMValueRef codegen_expr_call(Register *reg, IR_Expr *expr) {
    const char* fname = null_terminated(expr->data.call.name);
    LLVMValueRef fn = LLVMGetNamedFunction(llvm_mod, fname);

    if (!fn) {
        return NULL;
    }

    size_t argc = expr->data.call.args_count;
    LLVMTypeRef fn_type = LLVMGlobalGetValueType(fn);
    size_t expected_argc = LLVMCountParamTypes(fn_type);

    if (argc != expected_argc) { argc = expected_argc < argc ? expected_argc : argc; }

    LLVMTypeRef *param_types = malloc(sizeof(LLVMTypeRef) * expected_argc);
    LLVMGetParamTypes(fn_type, param_types);

    LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * argc);
    for (size_t i = 0; i < argc; i++) {
        args[i] = expr->data.call.args[i] ? codegen_expr(reg, expr->data.call.args[i]) : LLVMConstNull(LLVMPointerTypeInContext(llvm_ctx, 0));

        if (!args[i]) {
            args[i] = LLVMConstInt(LLVMInt32TypeInContext(llvm_ctx), 0, 0);
            continue;
        }

        if (i < expected_argc &&
            LLVMGetTypeKind(param_types[i]) == LLVMPointerTypeKind &&
            LLVMGetTypeKind(LLVMTypeOf(args[i])) == LLVMStructTypeKind) {
            args[i] = LLVMBuildExtractValue(llvm_builder, args[i], 0, "str_ptr");
        }
    }
    free(param_types);

    LLVMTypeRef ret_type = LLVMGetReturnType(fn_type);
    const char *call_name = LLVMGetTypeKind(ret_type) == LLVMVoidTypeKind ? "" : "call";
    LLVMValueRef result = LLVMBuildCall2(llvm_builder, fn_type, fn, args, argc, call_name);
    free(args);
    return result;
}

LLVMValueRef codegen_expr_tuple(Register *reg, IR_Expr *expr) {
    LLVMTypeRef *elem_types = malloc(sizeof(LLVMTypeRef) * expr->data.make_tuple.elems_count);

    for (size_t i = 0; i < expr->data.make_tuple.elems_count; i++)
        elem_types[i] = set_type(expr->data.make_tuple.elems[i]->ty);
        LLVMTypeRef tuple_type = LLVMStructType(elem_types, expr->data.make_tuple.elems_count, 0);
        LLVMValueRef alloca = LLVMBuildAlloca(llvm_builder, tuple_type, "tuple");
        for (size_t i = 0; i < expr->data.make_tuple.elems_count; i++) {
            LLVMValueRef val = codegen_expr(reg, expr->data.make_tuple.elems[i]);
            LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), i, 0) };
            LLVMValueRef gep = LLVMBuildGEP2(llvm_builder, tuple_type, alloca, indices, 2, "telem");
            LLVMBuildStore(llvm_builder, val, gep);
        }

    free(elem_types);
    return alloca;
}


LLVMValueRef codegen_expr_tupleindex(Register *reg, IR_Expr *expr) {
    LLVMValueRef tuple = codegen_expr(reg, expr->data.tuple_index.tuple);
    if (!tuple) return NULL;
    LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), expr->data.tuple_index.index, 0) };
    return LLVMBuildGEP2(llvm_builder, LLVMTypeOf(tuple), tuple, indices, 2, "tidx");
}

LLVMValueRef codegen_expr_index(Register *reg, IR_Expr *expr) {
    LLVMValueRef obj = codegen_expr(reg, expr->data.idx.base);
    LLVMValueRef index = codegen_expr(reg, expr->data.idx.index);

    if (!obj || !index) return NULL;

    if (LLVMGetTypeKind(LLVMTypeOf(obj)) != LLVMPointerTypeKind) {
        return NULL;
    }

    LLVMTypeRef elem_ty = set_type(expr->ty);

    if (!elem_ty || LLVMGetTypeKind(elem_ty) == LLVMVoidTypeKind) elem_ty = LLVMInt8TypeInContext(llvm_ctx);

    LLVMTypeRef index_ty = LLVMTypeOf(index);
    if (LLVMGetTypeKind(index_ty) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(index_ty) != 32) { printf("[codegen_expr_index] casting index from i%d to i32\n", LLVMGetIntTypeWidth(index_ty));
        index = LLVMBuildIntCast2(llvm_builder, index, LLVMInt32TypeInContext(llvm_ctx), 1, "idx_cast");
    }

    LLVMValueRef gep = LLVMBuildGEP2(llvm_builder, elem_ty, obj, &index, 1, "idx");
    return LLVMBuildLoad2(llvm_builder, elem_ty, gep, "idx_load");
}


LLVMValueRef codegen_expr_var(Register *reg, IR_Expr *expr) {
    StringView name = sv_from_range(expr->data.var_ref.name);
    RegisterEntry *entry = register_get(reg, name);
    LLVMValueRef alloca = symbol_table_get(entry->eid.id);
    LLVMTypeRef ty = LLVMGetAllocatedType(alloca);

    return LLVMBuildLoad2(llvm_builder, ty, alloca, "load");
}

LLVMValueRef codegen_expr_method(Register *reg, IR_Expr *expr) {
    LLVMValueRef obj = codegen_expr(reg, expr->data.method_call.object);
    LLVMValueRef fn = LLVMGetNamedFunction(llvm_mod, null_terminated(expr->data.method_call.method));
    LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * (expr->data.method_call.args_count + 1)); args[0] = obj;

    for (size_t i = 0; i < expr->data.method_call.args_count; i++) args[i + 1] = codegen_expr(reg, expr->data.method_call.args[i]);

    LLVMTypeRef fn_type = LLVMGlobalGetValueType(fn);
    LLVMValueRef result = LLVMBuildCall2(llvm_builder, fn_type, fn, args, expr->data.method_call.args_count + 1, "mcall");
    free(args);
    return result;

}