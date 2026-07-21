#include "import.h"
#include "ast.h"
#include "register.h"
#include "ir.h"
#include "codegen.h"


#include <llvm-c/Core.h>

LLVMValueRef codegen_expr_field(Register *reg, IR_Expr *expr) {
    LLVMValueRef gep = codegen_expr_addr(reg, expr);
    if (!gep) {
        return NULL;
    }

    RegisterEntry* type_entry = register_from_global(expr->data.field.type_eid.id);
    const char* type_name = null_terminated(type_entry->decl_name_range);
    LLVMTypeRef container_type = LLVMGetTypeByName2(llvm_ctx, type_name);

    StringView field_sv = sv_from_range(expr->data.field.field);
    StructParam* fields = (expr->data.field.kind == FieldOwner_Struct) ?  type_entry->data.strct.fields : type_entry->data._class.fields;
    size_t fields_count = (expr->data.field.kind == FieldOwner_Struct) ?  type_entry->data.strct.fields_count : type_entry->data._class.fields_count;

    int field_index = -1;

    for (size_t i = 0; i < fields_count; i++) {
        StringView fname = sv_from_range(fields[i].name);
        if (fname.len == field_sv.len && memcmp(fname.ptr, field_sv.ptr, fname.len) == 0) {
            field_index = (int)i;
            break;
        }
    }

    LLVMTypeRef field_ty = LLVMStructGetTypeAtIndex(container_type, field_index);
    return LLVMBuildLoad2(llvm_builder, field_ty, gep, "field_load");
}

LLVMValueRef codegen_expr_addr(Register *reg, IR_Expr *expr) {
    if (!expr) return NULL;

    if (expr->tag == IR_Expr_VarRef) {
        StringView name = sv_from_range(expr->data.var_ref.name);
        LLVMValueRef val = symbol_table_get(expr->data.var_ref.eid.id);

        if (val) return val;

        RegisterEntry *entry = register_get(reg, name);

        if (entry) {
            val = symbol_table_get(entry->eid.id);
            if (val) return val;
        }

        return NULL;
    }

    if (expr->tag == IR_Expr_Idx) {
        LLVMValueRef base = codegen_expr_addr(reg, expr->data.idx.object);
        if (!base) base = codegen_expr(reg, expr->data.idx.object);
        if (!base) return NULL;

        bool is_array = (expr->data.idx.object && expr->data.idx.object->ty.tag == Type_Array);

        if (!is_array && expr->data.idx.object && expr->data.idx.object->ty.tag == Type_Ptr) {
            LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(llvm_ctx, 0);
            base = LLVMBuildLoad2(llvm_builder, ptr_ty, base, "ptr_base");
        }

        LLVMValueRef index = codegen_expr(reg, expr->data.idx.index);
        if (!index) return NULL;

        LLVMTypeRef idx_ty = LLVMTypeOf(index);

        if (LLVMGetTypeKind(idx_ty) == LLVMPointerTypeKind) {
            if (expr->data.idx.index && expr->data.idx.index->ty.tag == Type_Custom) {
                LLVMTypeRef struct_ty = set_custom_type(expr->data.idx.index->ty);
                LLVMValueRef field_ptr = LLVMBuildStructGEP2(llvm_builder, struct_ty, index, 0, "idx_struct_field");
                index = LLVMBuildLoad2(llvm_builder, LLVMInt32TypeInContext(llvm_ctx), field_ptr, "idx_char_val");
            } else {
                LLVMTypeRef int_ptr_ty = LLVMInt64TypeInContext(llvm_ctx);
                index = LLVMBuildLoad2(llvm_builder, int_ptr_ty, index, "idx_ptr_load");
            }
            idx_ty = LLVMTypeOf(index);
        }  else if (LLVMGetTypeKind(idx_ty) == LLVMStructTypeKind) {
            index = LLVMBuildExtractValue(llvm_builder, index, 0, "idx_extracted_val");
            idx_ty = LLVMTypeOf(index);
        }

        if (LLVMGetTypeKind(idx_ty) == LLVMVoidTypeKind || !index) {
            index = LLVMConstInt(LLVMInt64TypeInContext(llvm_ctx), 0, false);
            idx_ty = LLVMTypeOf(index);
        }

        if (LLVMGetTypeKind(idx_ty) == LLVMIntegerTypeKind) {
            index = LLVMBuildIntCast2(llvm_builder, index, LLVMInt64TypeInContext(llvm_ctx), 1, "idx_64_cast");
        } else {
            index = LLVMConstInt(LLVMInt64TypeInContext(llvm_ctx), 0, false);
        }

        LLVMTypeRef elem_ty = NULL;

        if (expr->ty.tag == Type_Char) elem_ty = LLVMInt8TypeInContext(llvm_ctx);

        else if (expr->data.idx.object) {
            Type obj_ty = expr->data.idx.object->ty;
            if (obj_ty.tag == Type_Array && obj_ty.data.array_t.inner) {
                elem_ty = set_custom_type(*obj_ty.data.array_t.inner);
            } else if (obj_ty.tag == Type_Ptr && obj_ty.data.ptr.inner) {
                elem_ty = set_custom_type(*obj_ty.data.ptr.inner);
            } else if (obj_ty.tag == Type_RawPtr && obj_ty.data.raw_ptr.inner) {
                elem_ty = set_custom_type(*obj_ty.data.raw_ptr.inner);
            }
        }

        if (!elem_ty || LLVMGetTypeKind(elem_ty) == LLVMVoidTypeKind) elem_ty = set_custom_type(expr->ty);
        if (!elem_ty || LLVMGetTypeKind(elem_ty) == LLVMVoidTypeKind) elem_ty = LLVMInt32TypeInContext(llvm_ctx);
    

        if (is_array) {
            LLVMValueRef indices[2] = {
                LLVMConstInt(LLVMInt32TypeInContext(llvm_ctx), 0, false),
                index
            };
            size_t array_len = expr->data.idx.object->ty.data.array_t.len;
            LLVMTypeRef array_ty = LLVMArrayType(elem_ty, array_len);
            
            return LLVMBuildGEP2(llvm_builder, array_ty, base, indices, 2, "idx_addr");
        } else {
            return LLVMBuildGEP2(llvm_builder, elem_ty, base, &index, 1, "idx_addr");
        }
    }

    if (expr->tag == IR_Expr_Field) {
        LLVMValueRef obj = codegen_expr_addr(reg, expr->data.field.object);
        RegisterEntry* type_entry = register_from_global(expr->data.field.type_eid.id);
        if (!type_entry || !obj) return NULL;

        StructParam* fields = NULL;
        size_t fields_count = 0;

        switch (expr->data.field.kind) {
            case FieldOwner_Struct: fields = type_entry->data.strct.fields; fields_count = type_entry->data.strct.fields_count; break;
            case FieldOwner_Class:  fields = type_entry->data._class.fields; fields_count = type_entry->data._class.fields_count; break;
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

        if (field_index == -1) return NULL;

        const char* type_name = null_terminated(type_entry->decl_name_range);
        LLVMTypeRef container_type = LLVMGetTypeByName2(llvm_ctx, type_name);
        if (!container_type) return NULL;

        LLVMValueRef indices[] = {
            LLVMConstInt(LLVMInt32Type(), 0, 0),
            LLVMConstInt(LLVMInt32Type(), field_index, 0),
        };
        return LLVMBuildGEP2(llvm_builder, container_type, obj, indices, 2, "field_addr");
    }

    return NULL;
}

LLVMValueRef codegen_expr_binop(Register *reg, IR_Expr *expr) {
    if (!expr) return NULL;

    switch (expr->data.bin.op) {
        case Equalss: {
            LLVMValueRef rhs = codegen_expr(reg, expr->data.bin.rhs);
            LLVMValueRef lhs_ptr = codegen_expr_addr(reg, expr->data.bin.lhs);

            if (!lhs_ptr || !rhs) {
                fprintf(stderr, "[DEBUG BINOP ERROR] Equalss failed! lhs_ptr=%p, rhs=%p\n",(void*)lhs_ptr, (void*)rhs);
                return NULL;
            }

            LLVMTypeRef rhs_ty = LLVMTypeOf(rhs);
            if (LLVMGetTypeKind(rhs_ty) == LLVMPointerTypeKind) {
                if (expr->data.bin.rhs->ty.tag == Type_Custom) {
                    LLVMTypeRef struct_ty = set_custom_type(expr->data.bin.rhs->ty);
                    LLVMValueRef field_ptr = LLVMBuildStructGEP2(llvm_builder, struct_ty, rhs, 0, "rhs_struct_field");
                    rhs = LLVMBuildLoad2(llvm_builder, LLVMInt32TypeInContext(llvm_ctx), field_ptr, "rhs_unwrapped_val");
                } else {
                    rhs = LLVMBuildLoad2(llvm_builder, LLVMInt64TypeInContext(llvm_ctx), rhs, "rhs_load");
                }
                rhs_ty = LLVMTypeOf(rhs);
            } else if (LLVMGetTypeKind(rhs_ty) == LLVMStructTypeKind) {
                rhs = LLVMBuildExtractValue(llvm_builder, rhs, 0, "rhs_extracted_val");
                rhs_ty = LLVMTypeOf(rhs);
            }

            LLVMValueRef lhs_val = codegen_expr(reg, expr->data.bin.lhs);
            if (lhs_val) {
                LLVMTypeRef target_ty = LLVMTypeOf(lhs_val);

                if (LLVMGetTypeKind(rhs_ty) == LLVMIntegerTypeKind && LLVMGetTypeKind(target_ty) == LLVMIntegerTypeKind && 
                    rhs_ty != target_ty) {
                    rhs = LLVMBuildIntCast2(llvm_builder, rhs, target_ty, 1, "assign_cast");
                }
            }

            LLVMBuildStore(llvm_builder, rhs, lhs_ptr);
            return rhs;
        }

        case PlusEqualss:
        case MinusEqualss:
        case StarEqualss:
        case SlashEqualss:
        case PercentEqualss:
        case PipeEqualss:
        case AmpersandEqualss:
        case CaretEqualss:
        case LeftShiftEqualss:
        case RightShiftEqualss: {
            LLVMValueRef lhs_val = codegen_expr(reg, expr->data.bin.lhs);
            LLVMValueRef rhs = codegen_expr(reg, expr->data.bin.rhs);
            LLVMValueRef lhs_ptr = codegen_expr_addr(reg, expr->data.bin.lhs);

            if (!lhs_val || !rhs || !lhs_ptr) {
                fprintf(stderr, "[DEBUG BINOP ERROR] Compound assign failed!\n");
                return NULL;
            }

            LLVMTypeRef lhs_ty = LLVMTypeOf(lhs_val);
            if (LLVMGetTypeKind(lhs_ty) == LLVMPointerTypeKind) {
                if (expr->data.bin.lhs->ty.tag == Type_Custom) {
                    LLVMTypeRef struct_ty = set_custom_type(expr->data.bin.lhs->ty);
                    LLVMValueRef field_ptr = LLVMBuildStructGEP2(llvm_builder, struct_ty, lhs_val, 0, "lhs_field");
                    lhs_val = LLVMBuildLoad2(llvm_builder, LLVMInt32TypeInContext(llvm_ctx), field_ptr, "lhs_unwrapped");
                } else {
                    lhs_val = LLVMBuildLoad2(llvm_builder, LLVMInt64TypeInContext(llvm_ctx), lhs_val, "lhs_load");
                }
                lhs_ty = LLVMTypeOf(lhs_val);
            } else if (LLVMGetTypeKind(lhs_ty) == LLVMStructTypeKind) {
                lhs_val = LLVMBuildExtractValue(llvm_builder, lhs_val, 0, "lhs_extracted");
                lhs_ty = LLVMTypeOf(lhs_val);
            }

            LLVMTypeRef rhs_ty = LLVMTypeOf(rhs);
            if (LLVMGetTypeKind(rhs_ty) == LLVMPointerTypeKind) {
                if (expr->data.bin.rhs->ty.tag == Type_Custom) {
                    LLVMTypeRef struct_ty = set_custom_type(expr->data.bin.rhs->ty);
                    LLVMValueRef field_ptr = LLVMBuildStructGEP2(llvm_builder, struct_ty, rhs, 0, "rhs_field");
                    rhs = LLVMBuildLoad2(llvm_builder, LLVMInt32TypeInContext(llvm_ctx), field_ptr, "rhs_unwrapped");
                } else {
                    rhs = LLVMBuildLoad2(llvm_builder, LLVMInt64TypeInContext(llvm_ctx), rhs, "rhs_load");
                }
                rhs_ty = LLVMTypeOf(rhs);
            } else if (LLVMGetTypeKind(rhs_ty) == LLVMStructTypeKind) {
                rhs = LLVMBuildExtractValue(llvm_builder, rhs, 0, "rhs_extracted");
                rhs_ty = LLVMTypeOf(rhs);
            }

            if (lhs_ty != rhs_ty && LLVMGetTypeKind(lhs_ty) == LLVMIntegerTypeKind && LLVMGetTypeKind(rhs_ty) == LLVMIntegerTypeKind) {
                rhs = LLVMBuildIntCast2(llvm_builder, rhs, lhs_ty, 1, "rhs_compound_cast");
            }

            LLVMValueRef res = NULL;
            switch (expr->data.bin.op) {
                case PlusEqualss:       res = LLVMBuildAdd(llvm_builder, lhs_val, rhs, "add_tmp"); break;
                case MinusEqualss:      res = LLVMBuildSub(llvm_builder, lhs_val, rhs, "sub_tmp"); break;
                case StarEqualss:       res = LLVMBuildMul(llvm_builder, lhs_val, rhs, "mul_tmp"); break;
                case SlashEqualss:      res = LLVMBuildSDiv(llvm_builder, lhs_val, rhs, "div_tmp"); break;
                case PercentEqualss:    res = LLVMBuildSRem(llvm_builder, lhs_val, rhs, "rem_tmp"); break;
                case PipeEqualss:       res = LLVMBuildOr(llvm_builder, lhs_val, rhs, "or_tmp"); break;
                case AmpersandEqualss:  res = LLVMBuildAnd(llvm_builder, lhs_val, rhs, "and_tmp"); break;
                case CaretEqualss:      res = LLVMBuildXor(llvm_builder, lhs_val, rhs, "xor_tmp"); break;
                case LeftShiftEqualss:  res = LLVMBuildShl(llvm_builder, lhs_val, rhs, "shl_tmp"); break;
                case RightShiftEqualss: res = LLVMBuildLShr(llvm_builder, lhs_val, rhs, "shr_tmp"); break;
                default: break;
            }

            LLVMBuildStore(llvm_builder, res, lhs_ptr);
            return res;
        }

        default: break;
    }

    LLVMValueRef lhs = codegen_expr(reg, expr->data.bin.lhs);
    LLVMValueRef rhs = codegen_expr(reg, expr->data.bin.rhs);

    if (!lhs || !rhs) {
        fprintf(stderr, "[DEBUG BINOP ERROR] Failed to lower operands! lhs=%p, rhs=%p (op: %d)\n",  (void*)lhs, (void*)rhs, expr->data.bin.op);
        return NULL;
    }

    LLVMTypeRef lhs_ty = LLVMTypeOf(lhs);
    if (LLVMGetTypeKind(lhs_ty) == LLVMPointerTypeKind) {
        if (expr->data.bin.lhs->ty.tag == Type_Custom) {
            LLVMTypeRef struct_ty = set_custom_type(expr->data.bin.lhs->ty);
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(llvm_builder, struct_ty, lhs, 0, "lhs_field");
            lhs = LLVMBuildLoad2(llvm_builder, LLVMInt32TypeInContext(llvm_ctx), field_ptr, "lhs_unwrapped");
        } else {
            lhs = LLVMBuildLoad2(llvm_builder, LLVMInt64TypeInContext(llvm_ctx), lhs, "lhs_load");
        }
        lhs_ty = LLVMTypeOf(lhs);
    } else if (LLVMGetTypeKind(lhs_ty) == LLVMStructTypeKind) {
        lhs = LLVMBuildExtractValue(llvm_builder, lhs, 0, "lhs_extracted");
        lhs_ty = LLVMTypeOf(lhs);
    }

    LLVMTypeRef rhs_ty = LLVMTypeOf(rhs);
    if (LLVMGetTypeKind(rhs_ty) == LLVMPointerTypeKind) {
        if (expr->data.bin.rhs->ty.tag == Type_Custom) {
            LLVMTypeRef struct_ty = set_custom_type(expr->data.bin.rhs->ty);
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(llvm_builder, struct_ty, rhs, 0, "rhs_field");
            rhs = LLVMBuildLoad2(llvm_builder, LLVMInt32TypeInContext(llvm_ctx), field_ptr, "rhs_unwrapped");
        } else {
            rhs = LLVMBuildLoad2(llvm_builder, LLVMInt64TypeInContext(llvm_ctx), rhs, "rhs_load");
        }
        rhs_ty = LLVMTypeOf(rhs);
    } else if (LLVMGetTypeKind(rhs_ty) == LLVMStructTypeKind) {
        rhs = LLVMBuildExtractValue(llvm_builder, rhs, 0, "rhs_extracted");
        rhs_ty = LLVMTypeOf(rhs);
    }


    if (lhs_ty != rhs_ty && LLVMGetTypeKind(lhs_ty) == LLVMIntegerTypeKind && LLVMGetTypeKind(rhs_ty) == LLVMIntegerTypeKind) {
        unsigned lhs_width = LLVMGetIntTypeWidth(lhs_ty);
        unsigned rhs_width = LLVMGetIntTypeWidth(rhs_ty);
        if (lhs_width < rhs_width) {
            lhs = LLVMBuildIntCast2(llvm_builder, lhs, rhs_ty, 1, "lhs_cast");
        } else {
            rhs = LLVMBuildIntCast2(llvm_builder, rhs, lhs_ty, 1, "rhs_cast");
        }
    }

    switch (expr->data.bin.op) {
        case Plus:     return LLVMBuildAdd(llvm_builder, lhs, rhs, "add");
        case Minuss:   return LLVMBuildSub(llvm_builder, lhs, rhs, "sub");
        case Stars:    return LLVMBuildMul(llvm_builder, lhs, rhs, "mul");
        case Slashs:   return LLVMBuildSDiv(llvm_builder, lhs, rhs, "div");
        case Percents: return LLVMBuildSRem(llvm_builder, lhs, rhs, "rem");
        case Lesses:         return LLVMBuildICmp(llvm_builder, LLVMIntSLT, lhs, rhs, "lt");
        case Greaters:       return LLVMBuildICmp(llvm_builder, LLVMIntSGT, lhs, rhs, "gt");
        case DoubleEqualss:  return LLVMBuildICmp(llvm_builder, LLVMIntEQ, lhs, rhs, "eq");
        case NotEqualss:     
        case NEqs:           return LLVMBuildICmp(llvm_builder, LLVMIntNE, lhs, rhs, "ne");
        case LessEqualss:    return LLVMBuildICmp(llvm_builder, LLVMIntSLE, lhs, rhs, "le");
        case GreaterEqualss: return LLVMBuildICmp(llvm_builder, LLVMIntSGE, lhs, rhs, "ge");
        case Ampersands: return LLVMBuildAnd(llvm_builder, lhs, rhs, "bit_and");
        case Pipes:      return LLVMBuildOr(llvm_builder, lhs, rhs, "bit_or");
        case Carets:     return LLVMBuildXor(llvm_builder, lhs, rhs, "bit_xor");
        case LeftShifts:  return LLVMBuildShl(llvm_builder, lhs, rhs, "shl");
        case RightShifts: return LLVMBuildLShr(llvm_builder, lhs, rhs, "shr");
        case Ands: {
            LLVMValueRef result = LLVMBuildAnd(llvm_builder, lhs, rhs, "and");
            LLVMTypeRef result_ty = LLVMTypeOf(result);
            if (LLVMGetTypeKind(result_ty) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(result_ty) != 1) { 
                return LLVMBuildICmp(llvm_builder, LLVMIntNE, result, LLVMConstInt(result_ty, 0, 0), "and_bool"); 
            }
            return result;
        }
        case Ors: {
            LLVMValueRef result = LLVMBuildOr(llvm_builder, lhs, rhs, "or");
            LLVMTypeRef result_ty = LLVMTypeOf(result);
            if (LLVMGetTypeKind(result_ty) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(result_ty) != 1) { 
                return LLVMBuildICmp(llvm_builder, LLVMIntNE, result, LLVMConstInt(result_ty, 0, 0), "or_bool"); 
            }
            return result;
        }

        default: fprintf(stderr, "[DEBUG BINOP ERROR] Unhandled binary operator enum value: %d\n", expr->data.bin.op); return NULL;
    }
}

LLVMValueRef codegen_expr_unop(Register *reg, IR_Expr *expr) {
    if (!expr) return NULL;

    LLVMValueRef operand = codegen_expr(reg, expr->data.unary.operand);
    if (!operand) {
        fprintf(stderr, "[DEBUG UNOP ERROR] Unary operand evaluated to NULL!\n");
        return NULL;
    }

    LLVMTypeRef ty = LLVMTypeOf(operand);
    LLVMTypeKind kind = LLVMGetTypeKind(ty);

    if (expr->data.unary.op != Ampersands && expr->data.unary.op != Stars) {
        if (kind == LLVMPointerTypeKind) {
            if (expr->data.unary.operand->ty.tag == Type_Custom) {
                LLVMTypeRef struct_ty = set_custom_type(expr->data.unary.operand->ty);
                LLVMValueRef field_ptr = LLVMBuildStructGEP2(llvm_builder, struct_ty, operand, 0, "unop_field");
                operand = LLVMBuildLoad2(llvm_builder, LLVMInt32TypeInContext(llvm_ctx), field_ptr, "unop_unwrapped");
            } else {
                operand = LLVMBuildLoad2(llvm_builder, LLVMInt64TypeInContext(llvm_ctx), operand, "unop_load");
            }
            ty = LLVMTypeOf(operand);
            kind = LLVMGetTypeKind(ty);
        } else if (kind == LLVMStructTypeKind) {
            operand = LLVMBuildExtractValue(llvm_builder, operand, 0, "unop_extracted");
            ty = LLVMTypeOf(operand);
            kind = LLVMGetTypeKind(ty);
        }
    }

    switch (expr->data.unary.op) {
        case Minuss: {
            if (kind == LLVMIntegerTypeKind) {
                return LLVMBuildNeg(llvm_builder, operand, "neg");
            } else if (kind == LLVMFloatTypeKind || kind == LLVMDoubleTypeKind) {
                return LLVMBuildFNeg(llvm_builder, operand, "fneg");
            } 
            fprintf(stderr, "[DEBUG UNOP ERROR] Cannot negate non-numeric type!\n");
            return NULL;
        }

        case Stars: {
            LLVMTypeRef elem_ty = set_custom_type(expr->ty);
            if (!elem_ty || LLVMGetTypeKind(elem_ty) == LLVMVoidTypeKind) {
                elem_ty = LLVMInt8TypeInContext(llvm_ctx);
            }
            return LLVMBuildLoad2(llvm_builder, elem_ty, operand, "deref");
        }

        case Bangs:
        case Nots: {
            if (kind == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(ty) == 1) {
                return LLVMBuildNot(llvm_builder, operand, "not_bool");
            }

            LLVMValueRef zero = LLVMConstNull(ty);
            return LLVMBuildICmp(llvm_builder, LLVMIntEQ, operand, zero, "lnot");
        }


        case Tildes: {
            if (kind == LLVMIntegerTypeKind) {
                return LLVMBuildNot(llvm_builder, operand, "bitnot");
            }
            fprintf(stderr, "[DEBUG UNOP ERROR] Bitwise NOT requires an integer type!\n");
            return NULL;
        }

        case Ampersands: {
            return codegen_expr_addr(reg, expr->data.unary.operand);
        }

        default:
            fprintf(stderr, "[DEBUG UNOP ERROR] Unhandled unary operator tag: %d\n", expr->data.unary.op);
            return operand;
    }
}