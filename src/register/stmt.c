#include "import.h"
#include "register.h"
#include "ir.h"
#include "import.h"
#include "third-party/khashl.h"
#include "helper.h"
#include "register_tables.h"

uint32_t register_pattern_condition(Register* cond_child, Exprs* condition_expr, IfPat s_pat, SourceRange s_range, SourceRange class_name) {
    if (!condition_expr) return 0;

    RegisterEntry* cond_entry = register_expr(cond_child, condition_expr, class_name);
    RegisterEntry* pat_bind = NULL;
    uint32_t cond_id = cond_entry ? cond_entry->eid.id : 0;

    switch (s_pat.kind) {
        case IfPat_None:
        case IfPat_Wildcard: 
            break;

        case IfPat_Enum:
        case IfPat_Var:
        case IfPat_Let: {
            if (!cond_entry || 
                (cond_entry->tag != Reg_ExprFunctionCall && 
                 cond_entry->tag != Reg_ExprMethodCall && 
                 cond_entry->tag != Reg_ExprClassCall)) {
                break;
            }

            Type ret_type = get_function_type(cond_entry);
            SourceRange tmp_name = register_generate_name(cond_child->parent, s_pat.inner_bind);
            RegisterEntryTag bind_tag = (s_pat.kind == IfPat_Let) ? Reg_Let : Reg_Var;

            EntityID tmp_eid = register_insert(cond_child, (RegisterEntry){
                .tag = bind_tag,
                .decl_range = s_range,
                .decl_name_range = tmp_name,
                .data.var = {
                    .init = cond_entry,
                    .type = ret_type,
                    .child_reg = cond_child,
                }
            });

            pat_bind = register_from_global(tmp_eid.id);
            register_insert(cond_child, (RegisterEntry){
                .tag = Reg_Var,
                .decl_range = s_range,
                .decl_name_range = s_pat.inner_bind,
                .data.var = {
                    .init = pat_bind, 
                    .type = ret_type,
                    .child_reg = cond_child
                }
            });

            RegisterEntry tag_field_entry = (RegisterEntry){
                .tag = Reg_ExprField,
                .decl_range = s_range,
                .data.expr_field = {
                    .object = pat_bind,
                    .field = (SourceRange){ .start = "tag", .end = "tag" + 3, .file_id = s_range.file_id },
                    .range = s_range,
                    .kind = FieldOwner_Enum,
                    .type_eid = (EntityID){0},
                },
            };
            EntityID tag_field_eid = register_insert(cond_child, tag_field_entry);
            RegisterEntry* tag_field = register_from_scope(cond_child, tag_field_eid.id);

            RegisterEntry variant_ident_entry = (RegisterEntry){
                .tag = Reg_ExprIdentifier,
                .decl_range = s_pat.variant,
                .data.expr_identifier = {
                    .name = s_pat.variant,
                    .resolved_type = (Type){0},
                },
            };
            EntityID variant_ident_eid = register_insert(cond_child, variant_ident_entry);
            RegisterEntry* variant_ident = register_from_scope(cond_child, variant_ident_eid.id);

            RegisterEntry cmp_entry = (RegisterEntry){
                .tag = Reg_ExprBinaryOp,
                .decl_range = s_range,
                .data.expr_binary_op = {
                    .op = DoubleEqualss,
                    .left_id = tag_field ? tag_field->eid.id : 0,
                    .right_id = variant_ident ? variant_ident->eid.id : 0,
                    .left_type = (Type){0},
                    .right_type = (Type){0},
                    .resolved_type = (Type){ .tag = Type_Bool },
                },
            };
            EntityID cmp_eid = register_insert(cond_child, cmp_entry);
            RegisterEntry* cmp = register_from_scope(cond_child, cmp_eid.id);

            cond_id = cmp ? cmp->eid.id : cond_id;
            break;
        }
    }

    return cond_id;
}

void register_function(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_Functions);
    FunctionData fn = stmt->data.functions;
    Register* child = make_child(reg);

    EntityID fn_eid = register_insert(reg, (RegisterEntry){
        .tag = Reg_Function,
        .decl_range = fn.range,
        .decl_name_range = fn.name,
        .data.function = {
            .params = fn.params,
            .params_count = fn.params_count,
            .return_type = fn.return_type,
            .is_pub = fn.is_pub,
            .is_unsafe = fn.is_unsafe,
            .generic_params = fn.generic_params,
            .generic_params_count = fn.generic_params_count,
            .generic_param_nodes  = fn.generic_param_nodes,
            .body = fn.body,
            .body_count = fn.body_count,
            .child_reg = child,
        }
    });

    child->owner_id = fn_eid.id;

    for (size_t i = 0; i < fn.params_count; i++) {
        register_insert_child(child, (RegisterEntry){
            .tag = Reg_Param,
            .decl_name_range = fn.params[i].name,
            .data.var = {
                .type = fn.params[i].type,
                .mode = fn.params[i].mode,
                .is_mut = fn.params[i].mode.mutability == Mutability_Mutable,
            }
        }, fn_eid.id);
    }

    REG_PARAM_EXPRS(child, fn.params, fn.params_count, (SourceRange){0});
    REG_STMTS(child, fn.body, fn.body_count, (SourceRange){0});
}

void register_class(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_Classes);
    ClassData c = stmt->data.classes;

    RegisterEntry class_entry = (RegisterEntry){
        .tag = Reg_Class,
        .decl_range = stmt->data.classes.range,
        .decl_name_range = c.name,
        .data._class = {
            .fields = c.fields,
            .fields_count = c.fields_count,
            .methods = c.methods,
            .methods_count = c.methods_count,
            .generic_params = c.generic_params,
            .generic_params_count = c.generic_params_count,
            .generic_param_nodes = c.generic_param_nodes,
            .is_pub = c.is_pub,
        }
    };
    EntityID class_eid = register_insert(reg, class_entry);

    RegisterEntry* class_name = register_by_name(sv_from_range(class_entry.decl_name_range));

    for (size_t i = 0; i < c.methods_count; i++) {
        SourceRange mangled = mangle_name_unique(reg, c.name, c.methods[i].name);
        Register* method_child = make_child(reg);
        EntityID method_eid = register_insert(reg, (RegisterEntry){
            .tag = Reg_Function,
            .decl_range = c.methods[i].range,
            .decl_name_range = mangled,
            .data.function = {
                .params = c.methods[i].params,
                .params_count = c.methods[i].params_count,
                .return_type = c.methods[i].return_type,
                .is_pub = c.methods[i].is_pub,
                .child_reg = method_child,
                .body = c.methods[i].body,
                .body_count = c.methods[i].body_count,
                .generic_params = c.generic_params,
                .generic_params_count = c.generic_params_count,
                .generic_param_nodes = c.generic_param_nodes,
            }
        });
        method_child->owner_id = method_eid.id;

        for (size_t j = 0; j < c.methods[i].params_count; j++) {
            register_insert(method_child, (RegisterEntry){
                .tag = Reg_Param,
                .decl_name_range = c.methods[i].params[j].name,
                .data.var = {
                    .type = c.methods[i].params[j].type,
                    .mode = c.methods[i].params[j].mode,
                    .is_mut = c.methods[i].params[j].mode.mutability == Mutability_Mutable,
                }
            });
        }

        REG_PARAM_EXPRS(method_child, c.methods[i].params, c.methods[i].params_count, c.name);
        REG_STMTS(method_child, c.methods[i].body, c.methods[i].body_count, c.name);
    }
}

void register_struct(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_Structs);
    StructData s = stmt->data.structs;
    register_insert(reg, (RegisterEntry){
        .tag = Reg_Struct,
        .decl_range = s.range,
        .decl_name_range = s.name,
        .data.strct = {
            .fields = s.fields,
            .fields_count = s.fields_count,
            .is_pub = s.is_pub,
            .generic_params = s.generic_params,
            .generic_params_count = s.generic_params_count,
            .generic_param_nodes = s.generic_param_nodes,
        }
    });
}

void register_enum(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_Enums);
    EnumData e = stmt->data.enums;
    Register* child = make_child(reg);

    for (size_t i = 0; i < e.variants_count; i++) {
        Register* variant_child = make_child(child);

        for (size_t j = 0; j < e.variants[i].fields_count; j++) {
            register_insert(variant_child, (RegisterEntry){
                .tag = Reg_Param,
                .decl_name_range = e.variants[i].fields[j].name,
                .data.var = {
                    .type   = e.variants[i].fields[j].type,
                    .is_mut = false,
                }
            });
        }

        register_insert(reg, (RegisterEntry){
            .tag = Reg_EnumVariant,
            .decl_name_range = e.variants[i].name,
            .decl_range      = e.variants[i].name,
            .data.enum_variant = {
                .enum_name    = e.name,
                .variant_name = e.variants[i].name,
                .tag_index    = (uint32_t)i,
                .fields       = e.variants[i].fields,
                .fields_count = e.variants[i].fields_count,
                .child_reg    = variant_child,
            }
        });
    }

    register_insert(reg, (RegisterEntry){
        .tag = Reg_Enum,
        .decl_range      = e.range,
        .decl_name_range = e.name,
        .data.enm = {
            .variants          = e.variants,
            .variants_count    = e.variants_count,
            .is_pub            = e.is_pub,
            .generic_params    = e.generic_params,
            .generic_params_count = e.generic_params_count,
            .generic_param_nodes  = e.generic_param_nodes,
            .child_reg         = child,
        }
    });
}

void register_trait(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_Traits);
    TraitData t = stmt->data.traits;

    for (size_t i = 0; i < t.methods_count; i++) {
        Register* method_child = make_child(reg);
        EntityID method_eid = register_insert(reg, (RegisterEntry){
            .tag = Reg_Function,
            .decl_range = t.methods[i].range,
            .decl_name_range = t.methods[i].name,
            .data.function = {
                .params = t.methods[i].params,
                .params_count = t.methods[i].params_count,
                .return_type = t.methods[i].return_type,
                .is_pub = t.methods[i].is_pub,
                .child_reg = method_child,
            }
        });

        method_child->owner_id = method_eid.id;

        for (size_t j = 0; j < t.methods[i].params_count; j++) {
            register_insert(method_child, (RegisterEntry){
                .tag = Reg_Param,
                .decl_name_range = t.methods[i].params[j].name,
                .data.var = {
                    .type = t.methods[i].params[j].type,
                    .mode = t.methods[i].params[j].mode,
                }
            });

            REG_PARAM_EXPRS(method_child, t.methods[i].params, t.methods[i].params_count, (SourceRange){0});
        }

        REG_STMTS(method_child, t.methods[i].body, t.methods[i].body_count, (SourceRange){0});
    }

    register_insert(reg, (RegisterEntry){
        .tag = Reg_Trait,
        .decl_range = t.range,
        .decl_name_range = t.name,
        .data.trait = {
            .methods = t.methods,
            .methods_count = t.methods_count,
            .is_pub = t.is_pub,
        }
    });
}

void register_extern(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_Externs);
    SourceRange abi = stmt->data.extern_.abi;
    ExternFunction* funcs = stmt->data.extern_.funcs;
    size_t funcs_count = stmt->data.extern_.funcs_count;

    Register* child = make_child(reg);

    for (size_t i = 0; i < funcs_count; i++) {
        ExternFunction *f = &funcs[i];

        for (size_t j = 0; j < f->params_count; j++) {
            register_insert(child, (RegisterEntry){
                .tag = Reg_Param,
                .decl_name_range = f->params[j].name,
                .data.var = {
                    .type = f->params[j].type,
                    .mode = f->params[j].mode,
                    .is_mut = f->params[j].mode.mutability == Mutability_Mutable,
                }
            });
        }

        register_insert(child, (RegisterEntry){
            .tag = Reg_ExternFunc,
            .decl_range = f->name,
            .decl_name_range = f->name,
            .data.extern_func = {
                .name = f->name,
                .return_type = f->return_type,
                .params = f->params,
                .params_count = f->params_count,
            }
        });
    }

    register_insert(reg, (RegisterEntry){
        .tag = Reg_Extern,
        .decl_range = funcs_count > 0 ? funcs[0].name : (SourceRange){0},
        .decl_name_range = abi,
        .data.extern_ = {
            .abi = abi,
            .funcs = funcs,
            .funcs_count = funcs_count,
            .child_reg   = child,
        }
    });
}


void register_var(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Vars);
    VarData v = stmt->data.vars;

    bool has_init = v.value.tag != 0 || v.value.data.function_call.name.start != NULL;
    RegisterEntry* init_entry = NULL;
    Register* child = make_child(reg);

    if (has_init && v.type.tag == Type_Custom && stmt->data.vars.value.tag == Expr_Identifiers) {
        StringView type_sv = sv_from_range(v.type.data.custom.name);
        RegisterEntry* type_entry = register_get(reg, type_sv);
        if (type_entry && type_entry->tag == Reg_Enum) {
            stmt->data.vars.value = (Exprs){
                .tag = Expr_Enum_Calls,
                .data.enum_calls = {
                    .name = v.type.data.custom.name,
                    .field = stmt->data.vars.value.data.identifiers.name,
                    .param = NULL,
                    .param_count = 0,
                    .generic_params = v.type.data.custom.generic_args,
                    .generic_params_count = v.type.data.custom.generic_args_count,
                }
            };
        }
    }

    if (has_init && v.type.tag == Type_Custom && stmt->data.vars.value.tag == Expr_Function) {
        StringView type_sv = sv_from_range(v.type.data.custom.name);
        RegisterEntry* type_entry = register_get(reg, type_sv);

        if (type_entry && type_entry->tag == Reg_Enum) {
            Exprs* val_expr = &stmt->data.vars.value;
            SourceRange call_name = val_expr->data.function_call.name;

            if (is_enum_variant(type_entry, call_name)) {
                *val_expr = (Exprs){
                    .tag = Expr_Enum_Calls,
                    .data.enum_calls = {
                        .name                 = v.type.data.custom.name,
                        .field                = call_name,
                        .param                = val_expr->data.function_call.param,
                        .param_count          = val_expr->data.function_call.param_count,
                        .generic_params       = v.type.data.custom.generic_args,
                        .generic_params_count = v.type.data.custom.generic_args_count,
                    }
                };
            }
        }
    }

    if (has_init) {
        init_entry = register_expr(child, &stmt->data.vars.value, class_name);
    }

    register_insert(reg, (RegisterEntry){
        .tag = Reg_Var,
        .decl_range = v.range,
        .decl_name_range = v.name,
        .data.var = {
            .type      = v.type,
            .mode      = v.mode,
            .is_mut    = v.mode.mutability == Mutability_Mutable,
            .init      = init_entry,
            .child_reg = child,
        }
    });
}

void register_let(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Lets);
    LetData l = stmt->data.lets;
    
    bool has_init = l.value.tag != 0 || l.value.data.function_call.name.start != NULL;
    RegisterEntry* init_entry = NULL;
    Register* child = make_child(reg);

    if (has_init) {
        init_entry = register_expr(child, &stmt->data.lets.value, class_name);
    }

    register_insert(reg, (RegisterEntry){
        .tag             = Reg_Let,
        .decl_range      = l.range,
        .decl_name_range = l.name,
        .data.let = {
            .type = l.type,
            .mode = l.mode,
            .init = init_entry,
            .child_reg = child,
        }
    });
}

void register_const(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Consts);
    ConstData c = stmt->data.consts;
        
    bool has_init = c.value.tag != 0 || c.value.data.function_call.name.start != NULL;
    RegisterEntry* init_entry = NULL;
    Register* child = make_child(reg);

    if (has_init) {
        init_entry = register_expr(child, &stmt->data.consts.value, class_name);
    }

    register_insert(reg, (RegisterEntry){
        .tag = Reg_Const,
        .decl_range = c.range,
        .decl_name_range = c.name,
        .data.const_ = {
            .type = c.type,
            .is_pub = c.is_pub,
            .init   = init_entry,
            .child_reg = child,
        }
    });
}

void register_if(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Ifs);
    IfData s = stmt->data.ifs;

    Register* cond_child = make_child(reg);
    Register* then_child = make_child(reg);
    Register* else_child = s.else_body ? make_child(reg) : NULL;

    uint32_t cond_id = register_pattern_condition(cond_child, &s.cond, s.pat, s.range, class_name);

    REG_STMTS(then_child, s.body, s.body_count, class_name);
    if (s.else_body) {
        REG_STMTS(else_child, s.else_body, s.else_body_count, class_name);
    }

    register_insert(reg, (RegisterEntry){
        .tag = Reg_If,
        .decl_range = s.range,
        .data.if_ = {
            .pat = s.pat,
            .cond_id = cond_id,
            .cond_child = cond_child,
            .then_child = then_child,
            .else_child = else_child,
            .body = s.body,
            .body_count = s.body_count,
            .else_body = s.else_body,
            .else_body_count = s.else_body_count,
        }
    });
}

void register_elif(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Elifs);
    IfData s = stmt->data.ifs;
    Register* cond_child = make_child(reg);
    Register* then_child = make_child(reg);
    Register* else_child = make_child(reg);
    uint32_t cond_id = register_pattern_condition(cond_child, &s.cond, s.pat, s.range, class_name);

    REG_STMTS(then_child, s.body, s.body_count, class_name);
    REG_STMTS(else_child, s.else_body, s.else_body_count, class_name);

    register_insert(reg, (RegisterEntry){
        .tag = Reg_Elif,
        .decl_range = s.range,
        .data.if_ = {
            .pat = s.pat,
            .cond_id    = cond_id,
            .cond_child = cond_child,
            .then_child = then_child,
            .else_child = else_child,
            .body = s.body,
            .body_count = s.body_count,
            .else_body = s.else_body,
            .else_body_count = s.else_body_count,
        }
    });
}

void register_while(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Whiles);
    WhileData s = stmt->data.whiles;
    Register* cond_child = make_child(reg);
    Register* body_child = make_child(reg);
    RegisterEntry* cond_entry = register_expr(cond_child, &s.cond, class_name);
    uint32_t cond_id = cond_entry ? cond_entry->eid.id : 0;

    REG_STMTS(body_child, s.body, s.body_count, class_name);
    register_insert(reg, (RegisterEntry){
        .tag = Reg_While,
        .decl_range = s.range,
        .data.while_ = {
            .cond_id = cond_id, 
            .cond_child = cond_child, 
            .body_child = body_child,
            .body = s.body, 
            .body_count = s.body_count,
        }
    });
}


void register_for(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Fors);
    ForData s = stmt->data.fors;
    Register* iter_child = make_child(reg);
    Register* body_child = make_child(reg);
    RegisterEntry* iter_entry = register_expr(iter_child, &s.iter, class_name);
    uint32_t iter_id = iter_entry ? iter_entry->eid.id : 0;
    REG_STMTS(body_child, s.body, s.body_count, class_name);
    register_insert(reg, (RegisterEntry){
        .tag = Reg_For,
        .decl_range = s.range,
        .data.for_ = {
            .var = s._var, .iter_id = iter_id, .iter_child = iter_child, .body_child = body_child,
            .body = s.body, .body_count = s.body_count,
        }
    });
}

void register_match(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Matchs);
    MatchData s = stmt->data.matchs;

    Register* expr_child    = make_child(reg);
    Register* default_child = make_child(reg);
    Register** arm_children = s.cases_count ? malloc(sizeof(Register*) * s.cases_count) : NULL;

    RegisterEntry* expr_entry = register_expr(expr_child, &s.expr, class_name);
    uint32_t expr_id = expr_entry ? expr_entry->eid.id : 0;

    for (size_t i = 0; i < s.cases_count; i++) {
        arm_children[i] = make_child(reg);

        if (s.cases[i].pattern.tag == Pattern_Guard) {
            register_pattern_condition(expr_child, s.cases[i].pattern.data.guard.expr, (IfPat){0}, s.range, class_name);
        }

        REG_STMTS(arm_children[i], s.cases[i].body, s.cases[i].body_count, class_name);
    }

    if (s.default_body) REG_STMTS(default_child, s.default_body, s.default_body_count, class_name);

    register_insert(reg, (RegisterEntry){
        .tag = Reg_Match,
        .decl_range = s.range,
        .data.match_ = {
            .expr_id = expr_id, 
            .expr_child = expr_child, 
            .default_child = default_child, 
            .arm_children = arm_children,
            .cases = s.cases, 
            .cases_count = s.cases_count, 
            .default_body = s.default_body, 
            .default_body_count = s.default_body_count,
        }
    });
}

void register_return(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Returns);
    RegisterEntry* expr_entry = !expr_is_empty(&stmt->data.returns.expr) ? register_expr(reg, &stmt->data.returns.expr, class_name) : NULL;

    register_insert(reg, (RegisterEntry){
        .tag = Reg_Return,
        .decl_range = stmt->data.returns.range,
        .data.return_ = {
            .expr = expr_entry ? expr_entry->eid : (EntityID){0},
            .has_expr = expr_entry != NULL,
        }
    });
}

void register_expr_stmt(Register* reg, Stmts* stmt, SourceRange class_name) {
    RegisterEntry* expr_entry = register_expr(reg, &stmt->data.expr_stmt.expr, class_name);

    register_insert(reg, (RegisterEntry){
        .tag = Reg_ExprStmt,
        .data.expr_stmt_ = { .expr_id = expr_entry ? expr_entry->eid.id : 0 }
    });
}

void register_assign(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Assigns);
    AssignData a = stmt->data.assigns;

    RegisterEntry* target_entry = register_expr(reg, &a.target, class_name);
    RegisterEntry* value_entry  = register_expr(reg, &a.value, class_name);

    register_insert(reg, (RegisterEntry){
        .tag = Reg_Assign,
        .decl_range = a.range,
        .data.assign = {
            .op        = a.op,
            .target_id = target_entry ? target_entry->eid.id : 0,
            .value_id  = value_entry  ? value_entry->eid.id  : 0,
        }
    });
}

void register_module(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_Modules);
    ModuleData m = stmt->data.modules;
    Register* child = make_child(reg);
    REG_STMTS(child, m.body, m.body_count, (SourceRange){0});
    register_insert(reg, (RegisterEntry){
        .tag = Reg_Module,
        .decl_range = m.range,
        .decl_name_range = m.name,
        .data.module_ = {
            .name       = m.name,
            .body       = m.body,
            .body_count = m.body_count,
            .is_pub     = m.is_pub,
        }
    });
}

void register_atomic(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_AtomicOp);
    AtomicData a = stmt->data.atomic_op;
    REG_EXPRS(reg, a.args, a.args_count, (SourceRange){0});
    register_insert(reg, (RegisterEntry){
        .tag = Reg_Atomic,
        .decl_range = a.range,
        .data.atomic_ = {
            .target     = a.target,
            .op         = a.op,
            .args_count = a.args_count,
            .ordering   = a.ordering,
            .ordering2  = a.ordering2,
        }
    });
}


void register_stmt(Register* reg, Stmts* stmt, SourceRange class_name) {
    switch (stmt->tag) {
        case Stmt_Functions: register_function(reg, stmt); break;
        case Stmt_Structs:   register_struct(reg, stmt);   break;
        case Stmt_Enums:     register_enum(reg, stmt);     break;
        case Stmt_Classes:   register_class(reg, stmt);    break;
        case Stmt_Traits:    register_trait(reg, stmt);    break;
        case Stmt_Externs:   register_extern(reg, stmt);   break;
        case Stmt_Vars:      register_var(reg, stmt, class_name);      break;
        case Stmt_Lets:      register_let(reg, stmt, class_name);      break;
        case Stmt_Consts:    register_const(reg, stmt, class_name);    break;
        case Stmt_Ifs:       register_if(reg, stmt, class_name);       break;
        case Stmt_Whiles:    register_while(reg, stmt, class_name);    break;
        case Stmt_Fors:      register_for(reg, stmt, class_name);      break;
        case Stmt_Matchs:    register_match(reg, stmt, class_name);    break;
        case Stmt_Returns:   register_return(reg, stmt, class_name);   break;
        case Stmt_ExprStmt:  register_expr_stmt(reg, stmt, class_name); break;
        case Stmt_Assigns:   register_assign(reg, stmt, class_name);   break;
        case Stmt_Elifs:     register_elif(reg, stmt, class_name);    break;
        case Stmt_Modules:   register_module(reg, stmt);  break;
        case Stmt_AtomicOp:  register_atomic(reg, stmt);  break;

        default: break;
    }
}
