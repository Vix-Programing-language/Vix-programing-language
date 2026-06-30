#include "import.h"
#include "ast.h"
#include "register.h"
#include "ir.h"
#include "codegen.h"


#include <llvm-c/Core.h>


LLVMValueRef codegen_expr_call(Register *reg, IR_Expr *expr) {
    LLVMValueRef fn = LLVMGetNamedFunction(llvm_mod, null_terminated(expr->data.call.name));
    size_t argc = expr->data.call.args_count;
    LLVMTypeRef fn_type = LLVMGlobalGetValueType(fn);
    size_t expected_argc = LLVMCountParamTypes(fn_type);

    if (argc != expected_argc) { argc = expected_argc < argc ? expected_argc : argc; }

    ARR(LLVMTypeRef) param_types = {0};
    ARR(LLVMValueRef) args = {0};

    ARR_MAKE_ROOM(param_types, expected_argc);
    param_types.len = expected_argc;

    LLVMGetParamTypes(fn_type, param_types.data);

    for (size_t i = 0; i < argc; i++) {
        LLVMValueRef val = expr->data.call.args[i] ? codegen_expr(reg, expr->data.call.args[i]) : LLVMConstNull(LLVMPointerTypeInContext(llvm_ctx, 0));

        if (!val) {
            val = LLVMConstInt(LLVMInt32TypeInContext(llvm_ctx), 0, 0);
            ARR_PUSH(args, val);
            continue;
        }

        if (i < param_types.len &&
            LLVMGetTypeKind(ARR_AT(param_types, i)) == LLVMPointerTypeKind &&
            LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMStructTypeKind) {
            val = LLVMBuildExtractValue(llvm_builder, val, 0, "str_ptr");
        }

        ARR_PUSH(args, val);
    }

    ARR_FREE(param_types);

    LLVMTypeRef ret_type = LLVMGetReturnType(fn_type);
    const char *call_name = LLVMGetTypeKind(ret_type) == LLVMVoidTypeKind ? "" : "call";
    LLVMValueRef result = LLVMBuildCall2(llvm_builder, fn_type, fn, args.data, argc, call_name);

    ARR_FREE(args);
    return result;
}

LLVMValueRef codegen_expr_tuple(Register *reg, IR_Expr *expr) {
    ARR(LLVMTypeRef) elem_types = {0};
    for (size_t i = 0; i < expr->data.make_tuple.elems_count; i++) ARR_PUSH(elem_types, set_custom_type(expr->data.make_tuple.elems[i]->ty));

    LLVMTypeRef tuple_type = LLVMStructType(elem_types.data, (unsigned)elem_types.len, 0);
    LLVMValueRef alloca = LLVMBuildAlloca(llvm_builder, tuple_type, "tuple");

    for (size_t i = 0; i < expr->data.make_tuple.elems_count; i++) {
        LLVMValueRef val = codegen_expr(reg, expr->data.make_tuple.elems[i]);
        LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), i, 0) };
        LLVMValueRef gep = LLVMBuildGEP2(llvm_builder, tuple_type, alloca, indices, 2, "telem");
        LLVMBuildStore(llvm_builder, val, gep);
    }

    ARR_FREE(elem_types);
    return alloca;
}

LLVMValueRef codegen_expr_tupleindex(Register *reg, IR_Expr *expr) {
    LLVMValueRef tuple = codegen_expr(reg, expr->data.tuple_index.tuple);
    LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), expr->data.tuple_index.index, 0) };

    return LLVMBuildGEP2(llvm_builder, LLVMTypeOf(tuple), tuple, indices, 2, "tidx");
}

LLVMValueRef codegen_expr_index(Register *reg, IR_Expr *expr) {
    LLVMValueRef obj = codegen_expr(reg, expr->data.idx.object);
    LLVMValueRef index = codegen_expr(reg, expr->data.idx.index);
    LLVMTypeRef elem_ty = set_custom_type(expr->ty);

    if (!elem_ty || LLVMGetTypeKind(elem_ty) == LLVMVoidTypeKind) { elem_ty = LLVMInt8TypeInContext(llvm_ctx);
    } else if (LLVMGetTypeKind(elem_ty) == LLVMPointerTypeKind) { elem_ty = LLVMPointerTypeInContext(llvm_ctx, 0); }

    LLVMTypeRef index_ty = LLVMTypeOf(index);
    if (LLVMGetTypeKind(index_ty) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(index_ty) != 32) {
        index = LLVMBuildIntCast2(llvm_builder, index, LLVMInt32TypeInContext(llvm_ctx), 1, "idx_cast");
    }

    LLVMValueRef gep = LLVMBuildGEP2(llvm_builder, elem_ty, obj, &index, 1, "idx");
    return LLVMBuildLoad2(llvm_builder, elem_ty, gep, "idx_load");
}

LLVMValueRef codegen_expr_var(Register *reg, IR_Expr *expr) {
    StringView name = sv_from_range(expr->data.var_ref.name);
    RegisterEntry *entry = register_get(reg, name);
    if (!entry) {
        const char* fname = null_terminated(expr->data.var_ref.name);
        LLVMValueRef fn = LLVMGetNamedFunction(llvm_mod, fname);
        if (fn) return fn;
        return NULL;
    }

    if (entry->tag == Reg_Function || entry->tag == Reg_ExternFunc) {
        LLVMValueRef fn = LLVMGetNamedFunction(llvm_mod, null_terminated(entry->decl_name_range));
        if (fn) return fn;
        return NULL;
    }

    if (entry->tag == Reg_Const) {
        const char *cname = null_terminated(entry->decl_name_range);
        LLVMValueRef global = LLVMGetNamedGlobal(llvm_mod, cname);
        LLVMValueRef init = LLVMGetInitializer(global); if (init) return init;
        LLVMTypeRef ty = LLVMGetAllocatedType(global);

        return LLVMBuildLoad2(llvm_builder, ty, global, "load");
    }

    LLVMValueRef alloca = symbol_table_get(entry->eid.id);
    LLVMTypeRef ty = LLVMGetAllocatedType(alloca);

    return LLVMBuildLoad2(llvm_builder, ty, alloca, "load");
}

LLVMValueRef codegen_expr_method(Register *reg, IR_Expr *expr) {
    LLVMValueRef obj = codegen_expr(reg, expr->data.method_call.object);
    LLVMValueRef fn = LLVMGetNamedFunction(llvm_mod, null_terminated(expr->data.method_call.method));
    ARR(LLVMValueRef) args = {0};
    ARR_PUSH(args, obj);

    for (size_t i = 0; i < expr->data.method_call.args_count; i++) ARR_PUSH(args, codegen_expr(reg, expr->data.method_call.args[i]));

    LLVMTypeRef fn_type = LLVMGlobalGetValueType(fn);
    LLVMValueRef result = LLVMBuildCall2(llvm_builder, fn_type, fn, args.data, (unsigned)args.len, "mcall");

    ARR_FREE(args);
    return result;

}

LLVMValueRef codegen_expr_make_struct(Register *reg, IR_Expr *expr) {
    LLVMTypeRef struct_ty = LLVMGetTypeByName2(llvm_ctx, null_terminated(expr->data.make_struct.name));
    LLVMValueRef result = LLVMGetUndef(struct_ty);

    for (size_t i = 0; i < expr->data.make_struct.fields_count; i++) {
        LLVMValueRef val = codegen_expr(reg, expr->data.make_struct.fields[i].val);
        if (!val) { return LLVMGetUndef(struct_ty); }

        LLVMTypeRef expected = LLVMStructGetTypeAtIndex(struct_ty, (unsigned)i);
        LLVMTypeRef actual = LLVMTypeOf(val);

        if (LLVMGetTypeKind(expected) == LLVMPointerTypeKind && LLVMGetTypeKind(actual) == LLVMStructTypeKind) {
            val = LLVMBuildExtractValue(llvm_builder, val, 0, "str_ptr");
        }

        result = LLVMBuildInsertValue(llvm_builder, result, val, (unsigned)i, "field");
    }

    return result;
}

LLVMValueRef codegen_expr_make_enum(Register *reg, IR_Expr *expr) {
    char* name_str = null_terminated(expr->data.make_enum.type_name);

    LLVMTypeRef enum_ty = LLVMGetTypeByName(llvm_mod, name_str);
    LLVMValueRef result = LLVMGetUndef(enum_ty);
    LLVMValueRef tag_val = LLVMConstInt(LLVMInt32TypeInContext(llvm_ctx), expr->data.make_enum.active_idx, 0);
    result = LLVMBuildInsertValue(llvm_builder, result, tag_val, 0, "enum_tag");

    for (size_t i = 0; i < expr->data.make_enum.args_count; i++) {
        if (!expr->data.make_enum.args[i]) continue;
        
        LLVMValueRef val = codegen_expr(reg, expr->data.make_enum.args[i]);
        if (!val) continue;

        LLVMTypeRef expected = LLVMStructGetTypeAtIndex(enum_ty, (unsigned)(i + 1));
        LLVMTypeRef actual = LLVMTypeOf(val);

        if (LLVMGetTypeKind(expected) == LLVMPointerTypeKind && LLVMGetTypeKind(actual) == LLVMStructTypeKind) {
            val = LLVMBuildExtractValue(llvm_builder, val, 0, "str_ptr");
        }

        result = LLVMBuildInsertValue(llvm_builder, result, val, (unsigned)(i + 1), "enum_field");
    }

    return result;
}