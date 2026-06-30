#include "import.h"
#include "ast.h"
#include "register.h"
#include "ir.h"
#include "codegen.h"
#include "symbol_table.h"

#include <llvm-c/Core.h>

bool type_is_const_ptr(Type t) {
    if (t.tag == Type_Ptr)    return t.data.ptr.is_const;
    if (t.tag == Type_RawPtr) return t.data.raw_ptr.is_const;
    return false;
}


void codegen_function(Register *reg, uint32_t id, IR_Def def) {
    IR_FuncDef fn = def.data.function.def;
    RegisterEntry *fn_entry = register_from_scope(reg, id);
    Register *child_reg = (fn_entry && fn_entry->data.function.child_reg) ? fn_entry->data.function.child_reg : reg;

    ARR(uint32_t) param_ids = {0};
    for (size_t i = 0; i < fn.params_count; i++) {
        StringView name = sv_from_range(fn.params[i].name);
        RegisterEntry *e = register_get(child_reg, name);
        ARR_PUSH(param_ids, e ? e->eid.id : 0);
    }

    symbol_table_clear();
    codegen_generate_function(child_reg, param_ids.data, (Codegen_FuncDef){
        .name = null_terminated(fn.name),
        .params = set_param(fn.params, fn.params_count),
        .params_count = fn.params_count,
        .param_names = set_param_names(fn.params, fn.params_count),
        .return_type = set_custom_type(fn.return_type),
    });

    codegen_stmts(child_reg, fn.body, fn.body_count);
    ARR_FREE(param_ids);
}

void codegen_struct(uint32_t id, IR_Def def) {
    codegen_generate_struct((Codegen_Struct) {
        .name = null_terminated(def.data.struct_.name),
        .fields = set_fields(def.data.struct_.fields, def.data.struct_.fields_count),
        .fields_count = def.data.struct_.fields_count,
    });
}

void codegen_var(Register *reg, uint32_t id, IR_Def def) {
    LLVMValueRef init = codegen_expr(reg, def.data.var_.init);
    StringView name = sv_from_range(def.data.var_.name);
    RegisterEntry *entry = register_get(reg, name);
    uint32_t eid = entry ? entry->eid.id : id;
    codegen_generate_var(eid, (Codegen_Var){
        .name = null_terminated(def.data.var_.name),
        .type = set_custom_type(def.data.var_.ty),
        .init = init,
    });
}
void codegen_let(Register *reg, uint32_t id, IR_Def def) {
    LLVMValueRef init = codegen_expr(reg, def.data.let_.init);
    StringView name = sv_from_range(def.data.let_.name);
    RegisterEntry *entry = register_get(reg, name);
    uint32_t eid = entry ? entry->eid.id : id;
    codegen_generate_let(eid, (Codegen_Let){
        .name = null_terminated(def.data.let_.name),
        .type = set_custom_type(def.data.let_.ty),
        .init = init,
    });
}

void codegen_const(Register *reg, uint32_t id, IR_Def def) {
    LLVMValueRef init = codegen_expr(reg, def.data.const_.init);
    StringView name = sv_from_range(def.data.const_.name);
    RegisterEntry *entry = register_get(reg, name);
    uint32_t eid = entry ? entry->eid.id : id;
    codegen_generate_const(eid, (Codegen_Const){
        .name = null_terminated(def.data.const_.name),
        .type = set_custom_type(def.data.const_.ty),
        .init = init,
    }, false);
}

void codegen_extern(uint32_t id, IR_Def def) {
    codegen_generate_extern((Codegen_Extern){
        .name = null_terminated(def.data.extern_.name),
        .params = set_param(def.data.extern_.params, def.data.extern_.params_count),
        .param_names  = set_param_names(def.data.extern_.params, def.data.extern_.params_count),
        .params_count = def.data.extern_.params_count,
        .return_type  = set_custom_type(def.data.extern_.return_type),
    });
}

void codegen_trait(Register *reg, uint32_t id, IR_Def def) {
    for (size_t i = 0; i < def.data.trait_.methods_count; i++) {
        IR_Def method_def = {
            .tag = IR_Def_Function,
            .data.function.def = def.data.trait_.methods[i],
        };
        codegen_function(reg, i, method_def);
    }
}

void codegen_enum(uint32_t id, IR_Def def) {
    IR_VariantDef* variants = def.data.enum_.variants;
    size_t variants_count = def.data.enum_.variants_count;
    size_t max_fields = 0;

    for (size_t i = 0; i < variants_count; i++) {
        if (variants[i].fields_count > max_fields) max_fields = variants[i].fields_count;
    }

    ARR(LLVMTypeRef) payload_types = {0};
    for (size_t i = 0; i < variants_count; i++) {
        if (variants[i].fields_count == max_fields) { for (size_t j = 0; j < max_fields; j++) ARR_PUSH(payload_types, set_custom_type(variants[i].fields[j].ty)); break; }
    }

    codegen_generate_enum((Codegen_Enum){
        .name            = null_terminated(def.data.enum_.name),
        .variants_count  = variants_count,
        .payload_types   = payload_types.data,
        .payload_count   = payload_types.len,
    });

    ARR_FREE(payload_types);
}

LLVMValueRef codegen_expr(Register *reg, IR_Expr *expr) {
    if (!expr) return NULL;

    switch (expr->tag) {
        case IR_Expr_Literal:    return codegen_expr_literal(reg, expr);
        case IR_Expr_BinOp:      return codegen_expr_binop(reg, expr);
        case IR_Expr_Call:       return codegen_expr_call(reg, expr);
        case IR_Expr_VarRef:     return codegen_expr_var(reg, expr);
        case IR_Expr_MethodCall: return codegen_expr_method(reg, expr);
        case IR_Expr_Field:      return codegen_expr_field(reg, expr);
        case IR_Expr_Cast:       return codegen_expr_cast(reg, expr);
        case IR_Expr_MakeTuple:  return codegen_expr_tuple(reg, expr);
        case IR_Expr_TupleIndex: return codegen_expr_tupleindex(reg, expr);
        case IR_Expr_Idx:        return codegen_expr_index(reg, expr);
        case IR_Expr_UnaryOp:    return codegen_expr_unop(reg, expr);
        case IR_Expr_AddrOf:     return codegen_expr_addr(reg, expr->data.addr_of.expr);
        case IR_Expr_MakeStruct: return codegen_expr_make_struct(reg, expr);
        case IR_Expr_MakeEnum:   return codegen_expr_make_enum(reg, expr);
        default:
            return NULL;
    }
}

static bool is_const_write_target(Register* reg, IR_Expr* target) {
    if (!target) return false;

    switch (target->tag) {
        case IR_Expr_Idx: {
            IR_Expr* object = target->data.index.object;
            if (!object) return false;
            return is_const_write_target(reg, object) || type_is_const_ptr(object->ty);
        }

        case IR_Expr_UnaryOp: {
            if (target->data.unary.op == Stars) {
                IR_Expr* operand = target->data.unary.operand;
                return operand && type_is_const_ptr(operand->ty);
            }
            return false;
        }

        case IR_Expr_Field: {
            IR_Expr* object = target->data.field.object;
            if (!object) return false;
            return type_is_const_ptr(object->ty);
        }

        default:
            return false;
    }
}


IR_Module codegen_def(Register *reg, IR_Module mod) {
    for (size_t i = 0; i < mod.defs.len; i++) {
        IR_Def def = mod.defs.data[i];

        for (size_t i = 0; i < mod.defs.len; i++) {
            IR_Def def = mod.defs.data[i];
            if (def.tag != IR_Def_Function) continue;
            IR_FuncDef fn = def.data.function.def;
            LLVMTypeRef func_type = LLVMFunctionType( set_custom_type(fn.return_type), set_param(fn.params, fn.params_count), fn.params_count, 0);
            if (!LLVMGetNamedFunction(llvm_mod, null_terminated(fn.name))) LLVMAddFunction(llvm_mod, null_terminated(fn.name), func_type);
        }

        switch (def.tag) {
            case IR_Def_Function: {
                StringView name = sv_from_range(def.data.function.def.name);
                RegisterEntry *e = register_get(reg, name);
                uint32_t eid = e ? e->eid.id : i;
                codegen_function(reg, eid, def);
                break;
            }
            case IR_Def_Var: {
                StringView name = sv_from_range(def.data.var_.name);
                RegisterEntry *e = register_get(reg, name);
                uint32_t eid = e ? e->eid.id : i;
                codegen_var(reg, eid, def);
                break;
            }
            case IR_Def_Let: {
                StringView name = sv_from_range(def.data.let_.name);
                RegisterEntry *e = register_get(reg, name);
                uint32_t eid = e ? e->eid.id : i;
                codegen_let(reg, eid, def);
                break;
            }
            case IR_Def_Const: {
                LLVMValueRef init = codegen_expr(reg, def.data.const_.init);
                StringView name = sv_from_range(def.data.const_.name);
                RegisterEntry *entry = register_get(reg, name);
                uint32_t eid = entry ? entry->eid.id : 0;
                codegen_generate_const(eid, (Codegen_Const){
                    .name = null_terminated(def.data.const_.name),
                    .type = set_custom_type(def.data.const_.ty),
                    .init = init,
                }, true);

                break;
            }
            case IR_Def_Struct:  codegen_struct(i, def);  break;
            case IR_Def_Enum:    codegen_enum(i, def);    break;
            case IR_Def_Extern:  codegen_extern(i, def);  break;
            case IR_Def_Trait:    codegen_trait(reg, i, def); break;
            default: break;
        }
    }
    return mod;
}



void codegen_stmts(Register *reg, IR_Stmt *stmts, size_t count) {
    for (size_t i = 0; i < count; i++) {
        IR_Stmt *s = &stmts[i];
        switch (s->tag) {
            case IR_Stmt_VarDecl: {
                LLVMValueRef init = codegen_expr(reg, s->data.var_decl.init);
                StringView name = sv_from_range(s->data.var_decl.name);
                RegisterEntry *entry = register_get(reg, name);
                uint32_t eid = entry ? entry->eid.id : 0;

                codegen_generate_var(eid, (Codegen_Var){
                    .name = null_terminated(s->data.var_decl.name),
                    .type = set_custom_type(s->data.var_decl.ty),
                    .init = init,
                });
                break;
            }

            case IR_Stmt_LetDecl: {
                LLVMValueRef init = codegen_expr(reg, s->data.let_decl.init);
                StringView name = sv_from_range(s->data.let_decl.name);
                RegisterEntry *entry = register_get(reg, name);
                uint32_t eid = entry ? entry->eid.id : 0;
                codegen_generate_let(eid, (Codegen_Let){
                    .name = null_terminated(s->data.let_decl.name),
                    .type = set_custom_type(s->data.let_decl.ty),
                    .init = init,
                });
                break;
            }
            case IR_Stmt_ConstDecl: {
                LLVMValueRef init = codegen_expr(reg, s->data.const_decl.init);
                StringView name = sv_from_range(s->data.const_decl.name);
                RegisterEntry *entry = register_get(reg, name);
                uint32_t eid = entry ? entry->eid.id : 0;
                codegen_generate_const(eid, (Codegen_Const){
                    .name = null_terminated(s->data.const_decl.name),
                    .type = set_custom_type(s->data.const_decl.ty),
                    .init = init,
                }, false);
                break;
            }
            case IR_Stmt_Return: {
                LLVMValueRef val = codegen_expr(reg, s->data.ret.val);
                if (!val) { LLVMBuildRetVoid(llvm_builder); break; }

                LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(llvm_builder));
                const char *fn_name = LLVMGetValueName(fn);
                StringView fn_sv = { .ptr = fn_name, .len = strlen(fn_name) };
                RegisterEntry *entry = register_get(reg, fn_sv);
                LLVMTypeRef fn_ty  = LLVMTypeOf(val);

                if (entry && entry->tag == Reg_Function) { fn_ty = set_custom_type(entry->data.function.return_type); }

                LLVMTypeRef val_ty = LLVMTypeOf(val);
                if (val_ty != fn_ty) {
                    if (LLVMGetTypeKind(fn_ty)  == LLVMIntegerTypeKind && LLVMGetTypeKind(val_ty) == LLVMIntegerTypeKind) { val = LLVMBuildIntCast2(llvm_builder, val, fn_ty, 1, "ret_cast");
                    } else if (LLVMGetTypeKind(fn_ty)  == LLVMPointerTypeKind && LLVMGetTypeKind(val_ty) == LLVMPointerTypeKind) { val = LLVMBuildBitCast(llvm_builder, val, fn_ty, "ret_cast");
                    }
                }

                LLVMBuildRet(llvm_builder, val);
                break;
            }
            case IR_Stmt_Expr: {
                codegen_expr(reg, s->data.expr.expr);
                break;
            }
            case IR_Stmt_Assign: {
                LLVMValueRef val = codegen_expr(reg, s->data.assign.value);
                LLVMValueRef target = codegen_expr_addr(reg, s->data.assign.target);

                if (target && val) {
                    if (s->data.assign.target->tag == IR_Expr_Idx && s->data.assign.target->data.idx.is_const) {
                        break;
                    }
                    LLVMBuildStore(llvm_builder, val, target);
                }
                break;
            }

            case IR_Stmt_If: {
                LLVMValueRef cond = codegen_expr(reg, s->data.if_.cond);

                codegen_generate_if(reg, cond,  s->data.if_.body, s->data.if_.body_count, s->data.if_.else_body, s->data.if_.else_body_count);
                break;
            }

            case IR_Stmt_Match: {
                LLVMValueRef match_expr_val = codegen_expr(reg, s->data.match.expr);
                codegen_generate_match(reg, match_expr_val, s->data.match.arms, s->data.match.arms_count, s->data.match.default_body, s->data.match.default_body_count);
                break;
            }
            case IR_Stmt_While: { codegen_generate_while( reg, s->data.while_.cond, s->data.while_.body, s->data.while_.body_count); break; }
            case IR_Stmt_For: { codegen_generate_for(reg, s->data.for_.iter, s->data.for_.body, s->data.for_.body_count); break; }
            
            default: break;
        }
    }
}

