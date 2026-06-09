#include "import.h"
#include "ast.h"
#include "register.h"
#include "ir.h"
#include "codegen.h"


#include <llvm-c/Core.h>


LLVMValueRef codegen_generate_str(Register *reg, IR_Expr *expr) {
    SourceRange r = expr->data.literal.data.str_range;
    size_t len = r.end - r.start;
    const char *str_data = r.start;

    LLVMTypeRef byte_arr_ty = LLVMArrayType(LLVMInt8TypeInContext(llvm_ctx), (unsigned)len);
    LLVMValueRef const_data = LLVMConstStringInContext(llvm_ctx, str_data, (unsigned)len, 1);

    static int str_lit_counter = 0;
    char global_name[64];
    snprintf(global_name, sizeof(global_name), ".str.%d", str_lit_counter++);

    LLVMValueRef global = LLVMAddGlobal(llvm_mod, byte_arr_ty, global_name);
    LLVMSetInitializer(global, const_data);
    LLVMSetGlobalConstant(global, 1);
    LLVMSetLinkage(global, LLVMPrivateLinkage);
    

    LLVMValueRef indices[] = {
        LLVMConstInt(LLVMInt32TypeInContext(llvm_ctx), 0, 0),
        LLVMConstInt(LLVMInt32TypeInContext(llvm_ctx), 0, 0),
    };
    LLVMValueRef data_ptr = LLVMConstGEP2(byte_arr_ty, global, indices, 2);
    LLVMValueRef len_val  = LLVMConstInt(LLVMInt64TypeInContext(llvm_ctx), len, 0);

    LLVMValueRef struct_fields[] = { data_ptr, len_val };
    return LLVMConstNamedStruct(str_type, struct_fields, 2);
}

LLVMValueRef codegen_expr_literal(Register *reg, IR_Expr *expr) {
    switch (expr->ty.tag) {
        case Type_Int:   return LLVMConstInt(LLVMInt32TypeInContext(llvm_ctx), expr->data.literal.data.int_val, 1);
        case Type_Float: return LLVMConstReal(LLVMDoubleTypeInContext(llvm_ctx), expr->data.literal.data.float_val);
        case Type_Bool:  return LLVMConstInt(LLVMInt1TypeInContext(llvm_ctx), expr->data.literal.data.bool_val, 0);
        case Type_Char:  return LLVMConstInt(LLVMInt8TypeInContext(llvm_ctx), expr->data.literal.data.char_val, 0);
        case Type_Ptr:   return LLVMConstNull(LLVMPointerTypeInContext(llvm_ctx, 0));
        case Type_Str:   return codegen_generate_str(reg, expr);
        default:         return NULL;
    }
}

LLVMValueRef codegen_expr_cast(Register *reg, IR_Expr *expr) {
    LLVMValueRef inner = codegen_expr(reg, expr->data.cast.expr);
    LLVMTypeRef target_ty = set_type(expr->ty);
    LLVMTypeRef inner_type = LLVMTypeOf(inner);
    if (LLVMGetTypeKind(inner_type) == LLVMStructTypeKind && LLVMGetTypeKind(target_ty) == LLVMPointerTypeKind) {
        return LLVMBuildExtractValue(llvm_builder, inner, 0, "str_ptr");
    }

    if (LLVMGetTypeKind(inner_type) == LLVMPointerTypeKind && LLVMGetTypeKind(target_ty) == LLVMPointerTypeKind) {
        return LLVMBuildPointerCast(llvm_builder, inner, target_ty, "ptrcast");
    }

    if (LLVMGetTypeKind(inner_type) == LLVMIntegerTypeKind && LLVMGetTypeKind(target_ty) == LLVMIntegerTypeKind) {
        return LLVMBuildIntCast2(llvm_builder, inner, target_ty, 1 /* signed */, "intcast");
    }

    return LLVMBuildBitCast(llvm_builder, inner, target_ty, "cast");
}
