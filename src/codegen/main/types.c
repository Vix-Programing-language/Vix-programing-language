#include "import.h"
#include "ast.h"
#include "register.h"
#include "ir.h"
#include "codegen.h"


#include <llvm-c/Core.h>

LLVMValueRef codegen_generate_str(Register *reg, IR_Expr *expr) {
    if (!expr) return NULL;

    SourceRange range = expr->data.literal.data.str_range;
    size_t raw_len = range.end - range.start;
    const char *str_data = range.start;

    if (raw_len >= 2 && str_data[0] == '"' && str_data[raw_len - 1] == '"') {
        str_data++;
        raw_len -= 2;
    }

    char *buf = (char *)malloc(raw_len + 1);
    memcpy(buf, str_data, raw_len);
    buf[raw_len] = '\0';

    LLVMValueRef global_bytes_ptr = LLVMBuildGlobalStringPtr(llvm_builder, buf, "str_bytes");
    free(buf);

    LLVMTypeRef str_struct_type = LLVMGetTypeByName(llvm_mod, "str");

    if (!str_struct_type) {
        if (str_type) {
            str_struct_type = str_type;
        } else {
            str_struct_type = LLVMStructCreateNamed(llvm_ctx, "str");
            LLVMTypeRef field_types[2] = {
                LLVMPointerTypeInContext(llvm_ctx, 0),
                LLVMInt32TypeInContext(llvm_ctx)
            };
            LLVMStructSetBody(str_struct_type, field_types, 2, 0);
        }
    }

    LLVMValueRef str_struct = LLVMGetUndef(str_struct_type);

    str_struct = LLVMBuildInsertValue(llvm_builder, str_struct, global_bytes_ptr, 0, "str_ptr");

    LLVMValueRef len_val = LLVMConstInt(LLVMInt32TypeInContext(llvm_ctx), (unsigned long long)raw_len, 0);
    str_struct = LLVMBuildInsertValue(llvm_builder, str_struct, len_val, 1, "str_len");

    return str_struct;
}

LLVMValueRef codegen_expr_literal(Register *reg, IR_Expr *expr) {
    if (!expr) {
        fprintf(stderr, "[DEBUG LITERAL ERROR] Passed NULL expr!\n");
        return NULL;
    }

    switch (expr->ty.tag) {
        case Type_Int:   return LLVMConstInt(LLVMInt32TypeInContext(llvm_ctx), expr->data.literal.data.int_val, 1);
        case Type_Float: return LLVMConstReal(LLVMDoubleTypeInContext(llvm_ctx), expr->data.literal.data.float_val);
        case Type_Bool:  return LLVMConstInt(LLVMInt1TypeInContext(llvm_ctx), expr->data.literal.data.bool_val, 0);
        case Type_Char:  return LLVMConstInt(LLVMInt8TypeInContext(llvm_ctx), expr->data.literal.data.char_val, 0);
        case Type_Ptr:   return LLVMConstNull(LLVMPointerTypeInContext(llvm_ctx, 0));
        case Type_Str:   return codegen_generate_str(reg, expr);
        
        case Type_Array: {
            LLVMTypeRef arr_type = set_custom_type(expr->ty);

            if (expr->data.array.elems_count == 0) {
                return LLVMConstNull(arr_type);
            }

            LLVMTypeRef elem_type = set_custom_type(*expr->ty.data.array_t.inner);
            LLVMValueRef *elem_vals = malloc(sizeof(LLVMValueRef) * expr->data.array.elems_count);

            for (size_t i = 0; i < expr->data.array.elems_count; i++) {
                elem_vals[i] = codegen_expr(reg, &expr->data.array.elems[i]);
            }

            LLVMValueRef const_arr = LLVMConstArray(elem_type, elem_vals, (unsigned int)expr->data.array.elems_count);
            free(elem_vals);
            return const_arr;
        }
        default: {
            fprintf(stderr, "[DEBUG LITERAL ERROR] Unhandled literal type tag: %d\n", expr->ty.tag);
            return NULL;
        }
    }
}

LLVMValueRef codegen_expr_cast(Register *reg, IR_Expr *expr) {
    LLVMValueRef inner = codegen_expr(reg, expr->data.cast.expr);
    
    if (!inner) {
        if (expr->data.cast.expr && expr->data.cast.expr->tag == IR_Expr_VarRef) {
            const char* fname = null_terminated(expr->data.cast.expr->data.var_ref.name);
            inner = LLVMGetNamedFunction(llvm_mod, fname);
        }
    }

    LLVMTypeRef target_ty = set_custom_type(expr->ty);
    LLVMTypeRef inner_type = LLVMTypeOf(inner);

    if (LLVMGetTypeKind(inner_type) == LLVMStructTypeKind && LLVMGetTypeKind(target_ty) == LLVMPointerTypeKind) {
        return LLVMBuildExtractValue(llvm_builder, inner, 0, "str_ptr");
    }

    if (LLVMGetTypeKind(inner_type) == LLVMPointerTypeKind && LLVMGetTypeKind(target_ty) == LLVMPointerTypeKind) {
        return LLVMBuildPointerCast(llvm_builder, inner, target_ty, "ptrcast");
    }

    if (LLVMGetTypeKind(inner_type) == LLVMIntegerTypeKind && LLVMGetTypeKind(target_ty) == LLVMIntegerTypeKind) {
        return LLVMBuildIntCast2(llvm_builder, inner, target_ty, 1, "intcast");
    }

    if (LLVMGetTypeKind(inner_type) == LLVMIntegerTypeKind && LLVMGetTypeKind(target_ty) == LLVMPointerTypeKind) {
        if (LLVMIsConstant(inner)) {
            return LLVMConstIntToPtr(inner, target_ty);
        } else {
            return LLVMBuildIntToPtr(llvm_builder, inner, target_ty, "inttoptr");
        }
    }

    if (LLVMGetTypeKind(inner_type) == LLVMPointerTypeKind && LLVMGetTypeKind(target_ty) == LLVMIntegerTypeKind) {
        if (LLVMIsConstant(inner)) {
            return LLVMConstPtrToInt(inner, target_ty);
        } else {
            return LLVMBuildPtrToInt(llvm_builder, inner, target_ty, "ptrtoint");
        }
    }

    if (LLVMGetTypeKind(inner_type) == LLVMFunctionTypeKind || (LLVMGetTypeKind(inner_type) == LLVMPointerTypeKind && LLVMGetTypeKind(LLVMGetElementType(inner_type)) == LLVMFunctionTypeKind)) {
        return LLVMBuildPointerCast(llvm_builder, inner, target_ty, "fnptrcast");
    }

    return LLVMBuildBitCast(llvm_builder, inner, target_ty, "cast");
}