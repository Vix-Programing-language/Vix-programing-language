#include "import.h"
#include "ast.h"
#include "register.h"
#include "ir.h"
#include "codegen.h"
#include "symbol_table.h"

void codegen_function(Register *reg, uint32_t id, IR_Def def) {
    IR_FuncDef fn = def.data.function.def;
    RegisterEntry *fn_entry = register_get_by_id(reg, id);
    Register *child_reg = (fn_entry && fn_entry->data.function.child_reg) ? fn_entry->data.function.child_reg : reg;
    uint32_t *param_ids = malloc(sizeof(uint32_t) * fn.params_count);

    for (size_t i = 0; i < fn.params_count; i++) {
        StringView name = sv_from_range(fn.params[i].name);
        RegisterEntry *e = register_get(child_reg, name);
        param_ids[i] = e ? e->eid.id : 0;
    }

    symbol_table_clear();
    codegen_generate_function(child_reg, param_ids, (Codegen_FuncDef){
        .name = null_terminated(fn.name),
        .params = set_param(fn.params, fn.params_count),
        .params_count = fn.params_count,
        .param_names = set_param_names(fn.params, fn.params_count),
        .return_type = set_type(fn.return_type),
    });

    codegen_stmts(child_reg, fn.body, fn.body_count);
    free(param_ids);
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
        .type = set_type(def.data.var_.ty),
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
        .type = set_type(def.data.let_.ty),
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
        .type = set_type(def.data.const_.ty),
        .init = init,
    });
}

void codegen_extern(uint32_t id, IR_Def def) {
    codegen_generate_extern((Codegen_Extern){
        .name = null_terminated(def.data.extern_.name),
        .params = set_param(def.data.extern_.params, def.data.extern_.params_count),
        .param_names  = set_param_names(def.data.extern_.params, def.data.extern_.params_count),
        .params_count = def.data.extern_.params_count,
        .return_type  = set_type(def.data.extern_.return_type),
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
    codegen_generate_enum((Codegen_Enum){
        .name = null_terminated(def.data.enum_.name),
        .variants_count = def.data.enum_.variants_count,
    });
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

        default: return NULL;
    }
}

IR_Module codegen_def(Register *reg, IR_Module mod) {
    for (size_t i = 0; i < mod.defs.len; i++) {
        IR_Def def = mod.defs.data[i];

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
                StringView name = sv_from_range(def.data.const_.name);
                RegisterEntry *e = register_get(reg, name);
                uint32_t eid = e ? e->eid.id : i;
                codegen_const(reg, eid, def);
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
                .type = set_type(s->data.var_decl.ty),
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
                .type = set_type(s->data.let_decl.ty),
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
                .type = set_type(s->data.const_decl.ty),
                .init = init,
            });
            break;
        }
            case IR_Stmt_Return: {
                LLVMValueRef val = codegen_expr(reg, s->data.ret.val);
                LLVMBuildRet(llvm_builder, val);
                break;
            }
            case IR_Stmt_Expr: {
                codegen_expr(reg, s->data.expr.expr);
                break;
            }
            case IR_Stmt_Assign: {
                LLVMValueRef val = codegen_expr(reg, s->data.assign.value);
                LLVMValueRef target = codegen_expr_addr(reg, s->data.assign.target); // address, not value
                if (target && val) LLVMBuildStore(llvm_builder, val, target);
                break;
            }

            case IR_Stmt_If: {
                LLVMValueRef cond = codegen_expr(reg, s->data.if_.cond);

                codegen_generate_if(reg, cond,  s->data.if_.body, s->data.if_.body_count, s->data.if_.else_body, s->data.if_.else_body_count);
                break;
            }

            case IR_Stmt_While: { codegen_generate_while( reg, s->data.while_.cond, s->data.while_.body, s->data.while_.body_count); break; }
            case IR_Stmt_For: { codegen_generate_for(reg, s->data.for_.iter, s->data.for_.body, s->data.for_.body_count); break; }
            
            default: break;
        }
    }
}

