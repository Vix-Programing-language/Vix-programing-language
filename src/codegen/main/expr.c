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

LLVMValueRef codegen_expr_var(Register *reg, IR_Expr *expr) {
    if (!expr) return NULL;

    StringView name = sv_from_range(expr->data.var_ref.name);
    RegisterEntry *entry = register_get(reg, name);
    
    if (!entry) {
        const char *fname = null_terminated(expr->data.var_ref.name);
        LLVMValueRef fn = LLVMGetNamedFunction(llvm_mod, fname);
        if (!fn) fprintf(stderr, "[DEBUG VAR ERROR] Unknown symbol or function '%.*s'\n",  (int)name.len, name.ptr);
        return fn;
    }

    if (entry->tag == Reg_Function || entry->tag == Reg_ExternFunc) {
        const char *fn_name = null_terminated(entry->decl_name_range);
        LLVMValueRef fn = LLVMGetNamedFunction(llvm_mod, fn_name);
        if (!fn) fprintf(stderr, "[DEBUG VAR ERROR] Function symbol missing in LLVM module: %s\n", fn_name);

        return fn;
    }

    if (entry->tag == Reg_Const) {
        const char *cname = null_terminated(entry->decl_name_range);
        LLVMValueRef global = LLVMGetNamedGlobal(llvm_mod, cname);
        if (!global) {
            fprintf(stderr, "[DEBUG VAR ERROR] Constant global '%s' not found!\n", cname);
            return NULL;
        }

        LLVMValueRef init = LLVMGetInitializer(global);
        if (init) return init;

        LLVMTypeRef ty = LLVMGetAllocatedType(global);
        return LLVMBuildLoad2(llvm_builder, ty, global, "const_load");
    }

    LLVMValueRef alloca = symbol_table_get(entry->eid.id);
    if (!alloca) {
        fprintf(stderr, "[DEBUG VAR ERROR] Symbol ID %u ('%.*s') has no registered memory location!\n",  entry->eid.id, (int)name.len, name.ptr);
        return NULL;
    }

    LLVMTypeRef ty = LLVMGetAllocatedType(alloca);
    return LLVMBuildLoad2(llvm_builder, ty, alloca, null_terminated(expr->data.var_ref.name));
}
LLVMValueRef codegen_expr_index(Register *reg, IR_Expr *expr) {
    if (!expr) {
        fprintf(stderr, "[DEBUG INDEX ERROR] Expression is NULL!\n");
        return NULL;
    }

    LLVMValueRef obj = codegen_expr_addr(reg, expr->data.idx.object);
    bool is_addr = (obj != NULL);

    if (!obj) {
        fprintf(stderr, "[DEBUG INDEX]   L-value was NULL. Falling back to codegen_expr (R-value)...\n");
        obj = codegen_expr(reg, expr->data.idx.object);
    }

    if (!obj) {
        fprintf(stderr, "[DEBUG INDEX ERROR] Base object evaluated to NULL!\n");
        return NULL;
    }

    LLVMValueRef index = codegen_expr(reg, expr->data.idx.index);
    if (!index) {
        fprintf(stderr, "[DEBUG INDEX ERROR] Index evaluated to NULL!\n");
        return NULL;
    }

    LLVMTypeRef index_ty = LLVMTypeOf(index);

    if (LLVMGetTypeKind(index_ty) == LLVMPointerTypeKind) {
        if (expr->data.idx.index && expr->data.idx.index->ty.tag == Type_Custom) {
            LLVMTypeRef struct_ty = set_custom_type(expr->data.idx.index->ty);
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(llvm_builder, struct_ty, index, 0, "idx_struct_field");
            index = LLVMBuildLoad2(llvm_builder, LLVMInt32TypeInContext(llvm_ctx), field_ptr, "idx_char_val");
        } else {
            LLVMTypeRef int_ptr_ty = LLVMInt64TypeInContext(llvm_ctx);
            index = LLVMBuildLoad2(llvm_builder, int_ptr_ty, index, "idx_ptr_load");
        }
        index_ty = LLVMTypeOf(index);
    } 

    if (LLVMGetTypeKind(index_ty) == LLVMStructTypeKind) {
        index = LLVMBuildExtractValue(llvm_builder, index, 0, "idx_extracted_val");
        index_ty = LLVMTypeOf(index);
    }

    if (LLVMGetTypeKind(index_ty) == LLVMVoidTypeKind || !index) {
        index = LLVMConstInt(LLVMInt64TypeInContext(llvm_ctx), 0, false);
        index_ty = LLVMTypeOf(index);
    }

    if (LLVMGetTypeKind(index_ty) == LLVMIntegerTypeKind) {
        index = LLVMBuildIntCast2(llvm_builder, index, LLVMInt64TypeInContext(llvm_ctx), 1, "idx_64_cast");
    } else {
        index = LLVMConstInt(LLVMInt64TypeInContext(llvm_ctx), 0, false);
    }

    LLVMTypeRef elem_ty = NULL;
    if (expr->data.idx.object) {
        Type obj_ty = expr->data.idx.object->ty;

        if (obj_ty.tag == Type_Array && obj_ty.data.array_t.inner) {
            elem_ty = set_custom_type(*obj_ty.data.array_t.inner);
        } else if (obj_ty.tag == Type_Ptr && obj_ty.data.ptr.inner) {
            elem_ty = set_custom_type(*obj_ty.data.ptr.inner);
        } else if (obj_ty.tag == Type_RawPtr && obj_ty.data.raw_ptr.inner) {
            elem_ty = set_custom_type(*obj_ty.data.raw_ptr.inner);
        } else if (obj_ty.tag == Type_Custom) {
            elem_ty = set_custom_type(expr->ty);
        }
    }

    if (!elem_ty || LLVMGetTypeKind(elem_ty) == LLVMVoidTypeKind) {
        if (expr->ty.tag == Type_Char) {
            elem_ty = LLVMInt8TypeInContext(llvm_ctx);
        } else {
            elem_ty = set_custom_type(expr->ty);
        }
    }
    
    
    if (!elem_ty || LLVMGetTypeKind(elem_ty) == LLVMVoidTypeKind) {
        elem_ty = LLVMInt8TypeInContext(llvm_ctx);
    }


    LLVMValueRef ptr_val = obj;
    bool is_array = (expr->data.idx.object && expr->data.idx.object->ty.tag == Type_Array);

    if (expr->data.idx.object && expr->data.idx.object->ty.tag == Type_Str) {
        elem_ty = LLVMInt8TypeInContext(llvm_ctx);

        LLVMTypeRef struct_ty = set_custom_type(expr->data.idx.object->ty);
        LLVMValueRef field_ptr = LLVMBuildStructGEP2(llvm_builder, struct_ty, obj, 0, "str_ptr_field");

        ptr_val = LLVMBuildLoad2(llvm_builder, LLVMPointerTypeInContext(llvm_ctx, 0), field_ptr, "raw_char_ptr");
        is_array = false;
    }

    if (is_addr && expr->data.idx.object && expr->data.idx.object->ty.tag == Type_Ptr) {
        LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(llvm_ctx, 0);
        ptr_val = LLVMBuildLoad2(llvm_builder, ptr_ty, obj, "ptr_base");
    }

    if (LLVMGetTypeKind(LLVMTypeOf(ptr_val)) != LLVMPointerTypeKind) {
        fprintf(stderr, "[DEBUG INDEX ERROR] Base pointer is not an LLVM Pointer type!\n");
        return NULL;
    }

    LLVMValueRef gep = NULL;
    if (is_array) {
        LLVMValueRef indices[2] = {
            LLVMConstInt(LLVMInt32TypeInContext(llvm_ctx), 0, false),
            index
        };
        size_t array_len = expr->data.idx.object->ty.data.array_t.len;
        LLVMTypeRef array_ty = LLVMArrayType(elem_ty, array_len);

        gep = LLVMBuildGEP2(llvm_builder, array_ty, ptr_val, indices, 2, "arr_gep");
    } else {
        gep = LLVMBuildGEP2(llvm_builder, elem_ty, ptr_val, &index, 1, "ptr_gep");
    }

    if (!gep) {
        fprintf(stderr, "[DEBUG INDEX ERROR] GEP construction failed!\n");
        return NULL;
    }

    LLVMValueRef loaded_val = LLVMBuildLoad2(llvm_builder, elem_ty, gep, "idx_load");

    return loaded_val;

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

LLVMValueRef codegen_expr_array(Register *reg, IR_Expr *expr) {
    int explicit_count = expr->data.array.elems_count;
    int empty_count = expr->data.array.empty;
    int total_count = explicit_count + empty_count;

    LLVMTypeRef element_type = set_custom_type(*expr->data.array.ty);
    LLVMValueRef* array_elements = malloc(sizeof(LLVMValueRef) * total_count);

    for (int i = 0; i < explicit_count; i++) {
        array_elements[i] = codegen_expr(reg, expr->data.array.elems[i]);
    }

    LLVMValueRef zero_initializer = LLVMConstNull(element_type);
    for (int i = explicit_count; i < total_count; i++) {
        array_elements[i] = zero_initializer;
    }
    LLVMValueRef const_array = LLVMConstArray(element_type, array_elements, total_count);

    free(array_elements);

    return const_array;
}