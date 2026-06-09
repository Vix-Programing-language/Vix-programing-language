#include "import.h"
#include "ast.h"
#include "register.h"
#include "ir.h"
#include "codegen.h"


#include <llvm-c/Core.h>



LLVMValueRef codegen_expr_field(Register *reg, IR_Expr *expr) {
    LLVMValueRef obj = codegen_expr(reg, expr->data.field.object);
    RegisterEntry* type_entry = register_get_by_id(reg, expr->data.field.type_eid.id);
    StructParam* fields = NULL;
    size_t fields_count = 0;

    switch (type_entry->tag) {
        case Reg_Struct: fields = type_entry->data.strct.fields; fields_count = type_entry->data.strct.fields_count; break;
        case Reg_Class: fields = type_entry->data._class.fields; fields_count = type_entry->data._class.fields_count; break;
        default: return NULL;
    }

    StringView field_sv = sv_from_range(expr->data.field.field);
    int field_index = -1;

    for (size_t i = 0; i < fields_count; i++) {
        StringView fname = sv_from_range(fields[i].name);
        if (fname.len == field_sv.len && memcmp(fname.ptr, field_sv.ptr, fname.len) == 0) {
            field_index = (int)i;
            break;
        }
    }

    LLVMTypeRef container_type = LLVMGetTypeByName2(llvm_ctx, null_terminated(type_entry->decl_name_range));
    LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), field_index, 0), };
    LLVMValueRef gep = LLVMBuildGEP2(llvm_builder, container_type, obj, indices, 2, "field");
    LLVMTypeRef field_ty = LLVMStructGetTypeAtIndex(container_type, field_index);
    return LLVMBuildLoad2(llvm_builder, field_ty, gep, "field_load");
}

LLVMValueRef codegen_expr_addr(Register *reg, IR_Expr *expr) {
    if (expr->tag == IR_Expr_VarRef) {
        StringView name = sv_from_range(expr->data.var_ref.name);
        RegisterEntry *entry = register_get(reg, name);
        return symbol_table_get(entry->eid.id);
    }
    if (expr->tag == IR_Expr_Idx) {
        LLVMValueRef base  = codegen_expr(reg, expr->data.idx.base);
        LLVMValueRef index = codegen_expr(reg, expr->data.idx.index);
        LLVMTypeRef  elem_ty = set_type(expr->ty);
        return LLVMBuildGEP2(llvm_builder, elem_ty, base, &index, 1, "idx_addr");
    }
    return NULL;
}


LLVMValueRef codegen_expr_binop(Register *reg, IR_Expr *expr) {
    LLVMValueRef lhs = codegen_expr(reg, expr->data.bin.lhs);
    LLVMValueRef rhs = codegen_expr(reg, expr->data.bin.rhs);

    if (!lhs || !rhs) return NULL;

    LLVMTypeRef lhs_ty = LLVMTypeOf(lhs);
    LLVMTypeRef rhs_ty = LLVMTypeOf(rhs);
    if (lhs_ty != rhs_ty &&
        LLVMGetTypeKind(lhs_ty) == LLVMIntegerTypeKind &&
        LLVMGetTypeKind(rhs_ty) == LLVMIntegerTypeKind) {
        rhs = LLVMBuildIntCast2(llvm_builder, rhs, lhs_ty, 1, "rhs_cast");
    }

    switch (expr->data.bin.op) {
        case Plus:      return LLVMBuildAdd(llvm_builder, lhs, rhs, "add");
        case Minuss:    return LLVMBuildSub(llvm_builder, lhs, rhs, "sub");
        case Stars:     return LLVMBuildMul(llvm_builder, lhs, rhs, "mul");
        case Slashs:    return LLVMBuildSDiv(llvm_builder, lhs, rhs, "div");
        case Percents:  return LLVMBuildSRem(llvm_builder, lhs, rhs, "rem");
        case Lesses:    return LLVMBuildICmp(llvm_builder, LLVMIntSLT, lhs, rhs, "lt");
        case Greaters:  return LLVMBuildICmp(llvm_builder, LLVMIntSGT, lhs, rhs, "gt");
        case DoubleEqualss: return LLVMBuildICmp(llvm_builder, LLVMIntEQ, lhs, rhs, "eq");
        case NotEqualss:    return LLVMBuildICmp(llvm_builder, LLVMIntNE, lhs, rhs, "ne");
        case LessEqualss:   return LLVMBuildICmp(llvm_builder, LLVMIntSLE, lhs, rhs, "le");
        case GreaterEqualss: return LLVMBuildICmp(llvm_builder, LLVMIntSGE, lhs, rhs, "ge");
        case Ampersands: return LLVMBuildAnd(llvm_builder, lhs, rhs, "and");
        case Pipes:      return LLVMBuildOr(llvm_builder, lhs, rhs, "or");
        case Carets:     return LLVMBuildXor(llvm_builder, lhs, rhs, "xor");
        case LeftShifts: return LLVMBuildShl(llvm_builder, lhs, rhs, "shl");
        case RightShifts: return LLVMBuildLShr(llvm_builder, lhs, rhs, "shr");
        default: return NULL;
    }
}

LLVMValueRef codegen_expr_unop(Register *reg, IR_Expr *expr) {
    LLVMValueRef operand = codegen_expr(reg, expr->data.unary.operand);
    if (!operand) return NULL;
    switch (expr->data.unary.op) {
        case Minuss: return LLVMBuildNeg(llvm_builder, operand, "neg");
        case Nots:   return LLVMBuildNot(llvm_builder, operand, "not");
        case Tildes: return LLVMBuildNot(llvm_builder, operand, "bitnot");
        default:     return operand;
    }
}

