#include "import.h"
#include "ast.h"
#include "register.h"
#include "ir.h"
#include "helper.h"


Register* register_get_child(Register* reg, uint32_t id);
IR_Def lower_function(Register *reg, uint32_t id);
IR_Expr lower_expr(Register *reg, RegisterEntry *entry);

static IR_Expr lower_literal(SourceRange r) {
    const char* s = r.start;
    size_t len = (size_t)(r.end - r.start);
    if (len == 0) return ir_literal_null(r);

    if (s[0] == '"') {
        SourceRange inner = { .start = s + 1, .end = r.end - 1, .file_id = r.file_id };
        return ir_literal_str(inner);
    }

    if (s[0] == '\'') {
        char c = (len >= 2) ? s[1] : '\0';
        return ir_literal_char(c, r);
    }

    if (len == 4 && memcmp(s, "true", 4) == 0)  return ir_literal_bool(true, r);
    if (len == 5 && memcmp(s, "false", 5) == 0) return ir_literal_bool(false, r);
    if (len == 4 && memcmp(s, "null", 4) == 0)  return ir_literal_null(r);

    bool is_float = false;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '.') { is_float = true; break; }
    }

    if (is_float) {
        char buf[64];
        size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
        memcpy(buf, s, n);
        buf[n] = '\0';
        return ir_literal_float(strtod(buf, NULL), r);
    }

    char buf[64];
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, s, n);
    buf[n] = '\0';

    int64_t val;
    if (n >= 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) { val = (int64_t)strtoull(buf, NULL, 16);
    } else if (n >= 2 && buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B')) { val = (int64_t)strtoull(buf + 2, NULL, 2);
    } else if (n >= 2 && buf[0] == '0' && (buf[1] == 'o' || buf[1] == 'O')) { val = (int64_t)strtoull(buf + 2, NULL, 8);
    } else { val = strtoll(buf, NULL, 10); }

    return ir_literal_int(val, r);
}



void lower_var(Register *reg, uint32_t id, IR_Module *mod, IR_StmtArr *stmts_out) {
    RegisterEntry *entry = register_from_scope(reg, id);
    IR_Expr* init = entry->data.var.init ? ir_expr_alloc(lower_expr(reg, entry->data.var.init)) : NULL;

    if (stmts_out) {
        ARR_PUSH(*stmts_out, ((IR_Stmt){
            .tag = IR_Stmt_VarDecl,
            .origin = entry->decl_range,
            .data.var_decl = {
                .name = entry->decl_name_range,
                .eid = entry->eid,
                .ty = entry->data.var.type,
                .mode = entry->data.var.mode,
                .init = init,
            },
        }));
    } else {
        IR_MOD_PUSH(mod, ((IR_Def){
            .tag = IR_Def_Var,
            .origin = entry->decl_range,
            .data.var_ = {
                .name = entry->decl_name_range,
                .eid = entry->eid,
                .ty = entry->data.var.type,
                .mode = entry->data.var.mode,
                .init = init,
            },
        }));
    }
}

void lower_let(Register *reg, uint32_t id, IR_Module *mod, IR_StmtArr *stmts_out) {
    RegisterEntry *entry = register_from_scope(reg, id);
    IR_Expr* init = entry->data.let.init ? ir_expr_alloc(lower_expr(reg, entry->data.let.init)) : NULL;
    if (stmts_out) {
        ARR_PUSH(*stmts_out, ((IR_Stmt){
            .tag = IR_Stmt_LetDecl,
            .origin = entry->decl_range,
            .data.let_decl = {
                .name = entry->decl_name_range,
                .eid = entry->eid,
                .ty = entry->data.let.type,
                .mode = entry->data.let.mode,
                .init = init,
            },
        }));
    } else {
        IR_MOD_PUSH(mod, ((IR_Def){
            .tag = IR_Def_Let,
            .origin = entry->decl_range,
            .data.let_ = {
                .name = entry->decl_name_range,
                .eid = entry->eid,
                .ty = entry->data.let.type,
                .mode = entry->data.let.mode,
                .init = init

            },
        }));
    }
}

void lower_const(Register *reg, uint32_t id, IR_Module *mod, IR_StmtArr *stmts_out) {
    RegisterEntry *entry = register_from_scope(reg, id);
    IR_Expr* init = entry->data.const_.init ? ir_expr_alloc(lower_expr(reg, entry->data.const_.init)) : NULL;

    if (stmts_out) {
        ARR_PUSH(*stmts_out, ((IR_Stmt){
            .tag = IR_Stmt_ConstDecl,
            .origin = entry->decl_range,
            .data.const_decl = {
                .name = entry->decl_name_range,
                .eid = entry->eid,
                .ty = entry->data.const_.type,
                .init = init,
            },
        }));
    } else {
        IR_MOD_PUSH(mod, ((IR_Def){
            .tag = IR_Def_Const,
            .origin = entry->decl_range,
            .data.const_ = {
                .name = entry->decl_name_range,
                .eid = entry->eid,
                .ty = entry->data.const_.type,
                .init = init,
            },
        }));
    }
}

IR_Def lower_struct(Register *reg, uint32_t id) {
    RegisterEntry *entry = register_from_scope(reg, id);
    IR_FieldDefArr fields = {0};

    for (size_t i = 0; i < entry->data.strct.fields_count; i++) {
        ARR_PUSH(fields, ((IR_FieldDef){
            .name = entry->data.strct.fields[i].name,
            .ty = entry->data.strct.fields[i].type,
        }));
    }

    return (IR_Def){
        .tag = IR_Def_Struct,
        .origin = entry->decl_range,
        .data.struct_ = {
            .name = entry->decl_name_range,
            .eid = entry->eid,
            .fields = fields.data,
            .fields_count = fields.len,
            .is_pub = entry->data.strct.is_pub,
        },
    };
}


IR_Def lower_enum(Register *reg, uint32_t id) {
    RegisterEntry *entry = register_from_scope(reg, id);
    IR_VariantDefArr variants = {0};

    for (size_t i = 0; i < entry->data.enm.variants_count; i++) {
        EnumVariant *v = &entry->data.enm.variants[i];
        IR_FieldDefArr fields = {0};
        for (size_t j = 0; j < v->fields_count; j++) {
            ARR_PUSH(fields, ((IR_FieldDef){
                .name = v->fields[j].name,
                .ty = v->fields[j].type,
            }));
        }
        ARR_PUSH(variants, ((IR_VariantDef){
            .name = v->name,
            .fields = fields.data,
            .fields_count = fields.len,
        }));
    }

    return (IR_Def){
        .tag = IR_Def_Enum,
        .origin = entry->decl_range,
        .data.enum_ = {
            .name = entry->decl_name_range,
            .eid = entry->eid,
            .variants = variants.data,
            .variants_count = variants.len,
            .is_pub = entry->data.enm.is_pub,
        },
    };
}

IR_Def lower_trait(Register *reg, uint32_t id) {
    RegisterEntry *entry = register_from_scope(reg, id);
    IR_FuncDefArr methods = {0};

    for (size_t i = 0; i < entry->data.trait.methods_count; i++) {
        TraitMethod *m = &entry->data.trait.methods[i];
        IR_ParamArr params = {0};
        for (size_t j = 0; j < m->params_count; j++) {
            ARR_PUSH(params, ((IR_Param){
                .name = m->params[j].name,
                .ty = m->params[j].type,
                .mode = m->params[j].mode,
                .eid = (EntityID){0},
            }));
        }
        ARR_PUSH(methods, ((IR_FuncDef){
            .name = m->name,
            .return_type = m->return_type,
            .params = params.data,
            .params_count = params.len,
            .is_pub = m->is_pub,
            .cc = CC_Default,
        }));
    }

    return (IR_Def){
        .tag = IR_Def_Trait,
        .origin = entry->decl_range,
        .data.trait_ = {
            .name = entry->decl_name_range,
            .eid = entry->eid,
            .methods = methods.data,
            .methods_count = methods.len,
            .is_pub = entry->data.trait.is_pub,
        },
    };
}
static void lower_if(Register *reg, uint32_t id, IR_StmtArr *out) {
    RegisterEntry *entry = register_from_scope(reg, id);
    RegisterEntry *cond_entry = register_from_scope(entry->data.if_.cond_child, entry->data.if_.cond_id);

    IR_Expr cond = lower_expr(entry->data.if_.cond_child, cond_entry);
    IR_StmtArr body = {0};
    IR_StmtArr else_body = {0};

    lower_stmt(entry->data.if_.then_child, (IR_Module){0}, &body);

    if (entry->data.if_.else_child) {
        lower_stmt(entry->data.if_.else_child, (IR_Module){0}, &else_body);
    }

    ARR_PUSH(*out, ((IR_Stmt){
        .tag = IR_Stmt_If,
        .data.if_ = {
            .cond = ir_expr_alloc(cond),
            .body = body.data,
            .body_count = body.len,
            .else_body = else_body.data,
            .else_body_count = else_body.len,
        },
    }));
}

static void lower_while(Register *reg, uint32_t id, IR_StmtArr *out) {
    RegisterEntry *entry = register_from_scope(reg, id);
    if (!entry) return;

    RegisterEntry *cond_entry = register_from_scope(entry->data.while_.cond_child, entry->data.while_.cond_id);
    IR_Expr cond = lower_expr(entry->data.while_.cond_child, cond_entry);
    
    IR_StmtArr body = {0};

    if (entry->data.while_.body_child) {
        lower_stmt(entry->data.while_.body_child, (IR_Module){0}, &body);
    }

    ARR_PUSH(*out, ((IR_Stmt){
        .tag = IR_Stmt_While,
        .origin = entry->decl_range,
        .data.while_ = {
            .cond = ir_expr_alloc(cond),
            .body = body.data,
            .body_count = body.len,
            .child_reg = entry->data.while_.body_child,
        },
    }));
}

static void lower_for(Register *reg, uint32_t id, IR_StmtArr *out) {
    RegisterEntry *entry = register_from_scope(reg, id);
    if (!entry) return;

    RegisterEntry *iter_entry = register_from_scope(entry->data.for_.iter_child, entry->data.for_.iter_id);
    IR_Expr iter = lower_expr(entry->data.for_.iter_child, iter_entry);
    
    IR_StmtArr body = {0};
    
    if (entry->data.for_.body_child) {
        lower_stmt(entry->data.for_.body_child, (IR_Module){0}, &body);
    }

    ARR_PUSH(*out, ((IR_Stmt){
        .tag = IR_Stmt_For,
        .origin = entry->decl_range,
        .data.for_ = {
            .var = entry->data.for_.var,
            .iter = ir_expr_alloc(iter),
            .body = body.data,
            .body_count = body.len,
            .child_reg = entry->data.for_.body_child,
        },
    }));
}

static void lower_match(Register *reg, uint32_t id, IR_StmtArr *out) {
    RegisterEntry *entry = register_from_scope(reg, id);
    RegisterEntry *expr_entry = register_from_scope(entry->data.match_.expr_child, entry->data.match_.expr_id);
    IR_Expr expr = lower_expr(entry->data.match_.expr_child, expr_entry);
    IR_StmtArr default_body = {0};
    size_t case_count = entry->data.match_.cases_count;
    IR_MatchArm* arms = case_count ? malloc(sizeof(IR_MatchArm) * case_count) : NULL;

    if (entry->data.match_.default_body_count > 0) lower_stmt(entry->data.match_.default_child, (IR_Module){0}, &default_body);

    for (size_t i = 0; i < case_count; i++) {
        IR_StmtArr case_body = {0};
        lower_stmt(entry->data.match_.arm_children[i], (IR_Module){0}, &case_body);
        arms[i] = (IR_MatchArm){
            .pattern = entry->data.match_.cases[i].pattern,
            .body = case_body.data,
            .body_count = case_body.len,
        };
    }

    ARR_PUSH(*out, ((IR_Stmt){
        .tag = IR_Stmt_Match,
        .data.match = {
            .expr = ir_expr_alloc(expr),
            .arms = arms,
            .arms_count = case_count,
            .default_body = default_body.data,
            .default_body_count = default_body.len,
        },
    }));
}

static void lower_return(Register *reg, uint32_t id, IR_StmtArr *out) {
    RegisterEntry *e = register_from_global(id);
    if (!e) {
        return;
    }

    uint32_t func_id = register_get_function(id);
    RegisterEntry *func_entry = register_from_global(func_id);
    if (!func_entry) return;

    Type return_type = func_entry->data.function.return_type;

    RegisterEntry *ret_entry = NULL;
    IR_Expr *val = NULL;

    if (e->data.return_.has_expr) {
        ret_entry = register_from_global(e->data.return_.expr.id);
        
        if (return_type.tag == Type_Custom && ret_entry && ret_entry->tag == Reg_ExprFunctionCall) {
            RegisterEntry* case_type = register_by_name(sv_from_range(return_type.data.custom.name));
            
            if (case_type && case_type->tag == Reg_Enum) {
                Register* child_scope = ret_entry->data.expr_function_call.child_reg;
                size_t argc = ret_entry->data.expr_function_call.arg_ids_count;
                uint32_t* arg_ids = ret_entry->data.expr_function_call.arg_ids;

                ARR(IR_Expr*) args = {0};

                for (size_t _i = 0; _i < argc; _i++) {
                    uint32_t arg_id = arg_ids[_i];
                    RegisterEntry* arg_entry = register_from_scope(child_scope, arg_id);
                    
                    if (arg_entry) {
                        IR_Expr lowered = lower_expr(child_scope, arg_entry);
                        ARR_PUSH(args, ir_expr_alloc(lowered));
                    } else {
                        ARR_PUSH(args, ir_expr_alloc((IR_Expr){0}));
                    }
                }

                size_t variant_count = case_type->data.enm.variants_count;
                EnumVariant* variants = case_type->data.enm.variants;
                
                size_t current = 0;
                EnumVariant tallest_variant = {0};
                for (size_t i = 0; i < variant_count; i++) {
                    if (variants[i].fields_count >= current) {
                        current = variants[i].fields_count;
                        tallest_variant = variants[i];
                    }
                }
                
                uint32_t active_tag_idx = 0;
                StringView active_field_sv = sv_from_range(ret_entry->data.expr_function_call.name);
                for (size_t i = 0; i < variant_count; i++) {
                    StringView v = sv_from_range(variants[i].name);
                    if (v.len == active_field_sv.len && memcmp(v.ptr, active_field_sv.ptr, v.len) == 0) {
                        active_tag_idx = (uint32_t)i;
                        break;
                    }
                }

                val = ir_expr_alloc((IR_Expr){
                    .tag = IR_Expr_MakeEnum,
                    .origin = ret_entry->decl_range,
                    .data.make_enum = {
                        .type_name  = return_type.data.custom.name,
                        .variant    = ret_entry->data.expr_function_call.name,
                        .eid        = case_type->eid,
                        .args       = args.data,
                        .args_count = argc,
                        .max_count  = current,
                        .tallest    = tallest_variant,
                        .active_idx = active_tag_idx,
                    },
                });
            }
        }

        if (!val) {
            val = ir_expr_alloc(lower_expr(reg, ret_entry));
        }
    }

    IR_Stmt stmt = (IR_Stmt){
        .tag = IR_Stmt_Return,
        .origin = e->decl_range,
        .data.ret = {
            .val = val,
            .return_type = e->data.return_.return_type,
        },
    };
    ARR_PUSH(*out, stmt);
}

static void lower_expr_stmt(Register *reg, uint32_t id, IR_StmtArr *out) {
    RegisterEntry *e = register_from_scope(reg, id);
    RegisterEntry *expr_entry = register_from_scope(reg, e->data.expr_stmt_.expr_id);
    IR_Expr lowered = lower_expr(reg, expr_entry);

    ARR_PUSH(*out, ((IR_Stmt){
        .tag = IR_Stmt_Expr,
        .data.expr = {
            .expr = ir_expr_alloc(lowered),
        },
    }));
}

static void lower_atomic(Register *reg, uint32_t id, IR_StmtArr *out) {
    RegisterEntry *e = register_from_scope(reg, id);
    ARR_PUSH(*out, ((IR_Stmt){
        .tag = IR_Stmt_AtomicOp,
        .origin = e->decl_range,
        .data.atomic_op = {
            .target = e->data.atomic_.target,
            .op = e->data.atomic_.op,
            .args_count = e->data.atomic_.args_count,
            .ordering = e->data.atomic_.ordering,
            .ordering2 = e->data.atomic_.ordering2,
        },
    }));
}

static void lower_assign(Register *reg, uint32_t id, IR_StmtArr *out) {
    RegisterEntry *e = register_from_scope(reg, id);
    RegisterEntry *tgt = register_from_scope(reg, e->data.assign.target_id);
    RegisterEntry *val = register_from_scope(reg, e->data.assign.value_id);

    IR_Expr* target = tgt ? ir_expr_alloc(lower_expr(reg, tgt)) : NULL;
    IR_Expr* value  = val ? ir_expr_alloc(lower_expr(reg, val)) : NULL;

    ARR_PUSH(*out, ((IR_Stmt){
        .tag = IR_Stmt_Assign,
        .origin = e->decl_range,
        .data.assign = {
            .op = e->data.assign.op,
            .target = target,
            .value  = value,
        },
    }));
}

void lower_extern(Register *reg, uint32_t id, IR_Module *mod) {
    RegisterEntry *entry = register_from_scope(reg, id);
    Register *child = register_get_child(reg, id);
    if (!child) {
        return;
    }

    for (size_t i = 0; i < entry->data.extern_.funcs_count; i++) {
        ExternFunction *f = &entry->data.extern_.funcs[i];
        StringView fname = sv_from_range(f->name);
        RegisterEntry *fe = register_get(child, fname);
        IR_ParamArr params = {0};

        for (size_t j = 0; j < f->params_count; j++) {
            StringView pname = sv_from_range(f->params[j].name);
            RegisterEntry *pe = register_get(child, pname);
            ARR_PUSH(params, ((IR_Param){
                .name = f->params[j].name,
                .ty = f->params[j].type,
                .mode = f->params[j].mode,
                .eid = pe ? pe->eid : (EntityID){0},
            }));
        }

        IR_MOD_PUSH(mod, ((IR_Def){
            .tag = IR_Def_Extern,
            .origin = f->name,
            .data.extern_ = {
                .name = f->name,
                .eid = fe ? fe->eid : (EntityID){0},
                .return_type = f->return_type,
                .params = params.data,
                .params_count = params.len,
                .is_pub = entry->data.extern_.is_pub,
            },
        }));
    }
}

IR_Def lower_function(Register *reg, uint32_t id) {
    RegisterEntry *entry = register_from_scope(reg, id);
    Register *child = register_get_child(reg, id);
    if (!child) {
        return (IR_Def){0};
    }


    IR_ParamArr params = {0};

    for (size_t i = 0; i < entry->data.function.params_count; i++) {
        StringView name = sv_from_range(entry->data.function.params[i].name);
        RegisterEntry *pe = register_get(child, name);
        ARR_PUSH(params, ((IR_Param){
            .name = entry->data.function.params[i].name,
            .ty = entry->data.function.params[i].type,
            .mode = entry->data.function.params[i].mode,
            .eid  = pe ? pe->eid : (EntityID){0},
        }));
    }
    
    IR_StmtArr body = {0};
    lower_stmt(child, (IR_Module){0}, &body);

    IR_FuncDef fn = {
        .name = entry->decl_name_range,
        .eid = entry->eid,
        .return_type = entry->data.function.return_type,
        .params = params.data,
        .params_count = params.len,
        .body = body.data,
        .body_count  = body.len,
        .is_pub = entry->data.function.is_pub,
        .is_unsafe = entry->data.function.is_unsafe,
        .cc = CC_Default,
    };

    return (IR_Def){
        .tag = IR_Def_Function,
        .origin = entry->decl_range,
        .data.function.def = fn,
    };
}

IR_Expr lower_expr(Register *reg, RegisterEntry *entry) {
    if (!entry) {
        printf("Failed, NULL entry!");
        return (IR_Expr){0};
    }

    switch (entry->tag) {
        case Reg_ExprFunctionCall: {
            RegisterEntry *callee = register_by_name(sv_from_range(entry->data.expr_function_call.name));
            Register *child = entry->data.expr_function_call.child_reg;
            size_t argc = entry->data.expr_function_call.arg_ids_count;
            ARR(IR_Expr*) args = {0};   
            LOWER_ARR(IR_Expr*, args, argc, ir_expr_alloc(lower_expr(child, register_from_scope(child, entry->data.expr_function_call.arg_ids[_i]))));

            return (IR_Expr){
                .tag = IR_Expr_Call,
                .origin = entry->decl_range,
                .data.call = {
                    .name = entry->data.expr_function_call.name,
                    .eid = callee ? callee->eid : (EntityID){0},
                    .args = args.data,
                    .args_count = argc,
                },
            };
        }

        case Reg_ExprMethodCall: {
            IR_Expr* object = entry->data.expr_method_call.object ? ir_expr_alloc(lower_expr(reg, entry->data.expr_method_call.object)) : NULL;
            size_t argc = entry->data.expr_method_call.args_count;
            ARR(IR_Expr*) args = {0};
            LOWER_ARR(IR_Expr*, args, entry->data.expr_method_call.args_count, ir_expr_alloc(lower_expr(reg, entry->data.expr_method_call.args[_i])));


            EntityID method_eid = (EntityID){0};

            return (IR_Expr){
                .tag = IR_Expr_MethodCall,
                .origin = entry->decl_range,
                .data.method_call = {
                    .object = object,
                    .method = entry->data.expr_method_call.method,
                    .method_eid = method_eid,
                    .args = args.data,
                    .args_count = argc,
                },
            };
        }

        case Reg_ExprClassCall: {
            RegisterEntry *cls = register_get(reg, sv_from_range(entry->data.expr_class_call.name));
            size_t argc = entry->data.expr_class_call.params_count;

            ARR(IR_Expr*) args = {0};
            LOWER_ARR(IR_Expr*, args, entry->data.expr_class_call.params_count, ir_expr_alloc(lower_expr(reg, register_expr(reg, &entry->data.expr_class_call.params[_i].value, (SourceRange){0}))));


            return (IR_Expr){
                .tag = IR_Expr_MakeClass,
                .origin = entry->decl_range,
                .data.make_class = {
                    .name = entry->data.expr_class_call.name,
                    .eid = cls ? cls->eid : (EntityID){0},
                    .args = args.data,
                    .args_count = argc,
                },
            };
        }

        case Reg_ExprStructCall: {
            RegisterEntry *strct = register_get(reg, sv_from_range(entry->data.expr_struct_call.name));

            size_t fieldc = entry->data.expr_struct_call.params_count;
            ARR(IR_FieldInit) fields = {0};
            LOWER_ARR(IR_FieldInit, fields, entry->data.expr_struct_call.params_count,
                ((IR_FieldInit){
                    .name = entry->data.expr_struct_call.params[_i].name,
                    .val  = ir_expr_alloc(lower_expr(reg, register_expr(reg, &entry->data.expr_struct_call.params[_i].value, (SourceRange){0}))),
                }));

            return (IR_Expr){
                .tag = IR_Expr_MakeStruct,
                .origin = entry->decl_range,
                .data.make_struct = {
                    .name = entry->data.expr_struct_call.name,
                    .eid = strct ? strct->eid : (EntityID){0},
                    .fields = fields.data,
                    .fields_count = fieldc,
                },
            };
        }

        case Reg_ExprEnumCall: {
            Register* child_scope = entry->data.expr_enum_call.child_reg;
            size_t argc = entry->data.expr_enum_call.arg_ids_count;
            uint32_t* arg_ids = entry->data.expr_enum_call.arg_ids;
            ARR(IR_Expr*) args = {0};

            for (size_t _i = 0; _i < argc; _i++) {
                uint32_t id = arg_ids[_i];
                RegisterEntry* arg_entry = register_from_scope(child_scope, id);
                
                if (arg_entry) {
                    IR_Expr lowered = lower_expr(child_scope, arg_entry);
                    ARR_PUSH(args, ir_expr_alloc(lowered));
                } else {
                    ARR_PUSH(args, ir_expr_alloc((IR_Expr){0}));
                }
            }

            size_t final_max_count  = entry->data.expr_enum_call.variant.max_count;
            EnumVariant final_tallest = entry->data.expr_enum_call.variant.tallest;
            uint32_t final_active_idx = entry->data.expr_enum_call.variant.active_idx;

            return (IR_Expr){
                .tag = IR_Expr_MakeEnum,
                .origin = entry->decl_range,
                .data.make_enum = {
                    .type_name  = entry->data.expr_enum_call.name,
                    .variant    = entry->data.expr_enum_call.field,
                    .eid        = entry->eid,
                    .args       = args.data,
                    .args_count = argc,
                    .max_count  = final_max_count,
                    .tallest    = final_tallest,
                    .active_idx = final_active_idx,
                },
            };
        }

        case Reg_ExprLiteral: return lower_literal(entry->decl_range);
        case Reg_ExprIdentifier: {
            RegisterEntry* resolved = register_get(reg, sv_from_range(entry->data.expr_identifier.name));
            return (IR_Expr){
                .tag = IR_Expr_VarRef,
                .origin = entry->decl_range,
                .data.var_ref = {
                    .name = entry->data.expr_identifier.name,
                    .eid = resolved ? resolved->eid : (EntityID){0},
                },
            };
        }

        case Reg_ExprVar: {
            RegisterEntry* resolved = register_get(reg, sv_from_range(entry->data.expr_var.name));

            return (IR_Expr){
                .tag = IR_Expr_VarRef,
                .origin = entry->decl_range,
                .data.var_ref = {
                    .name = entry->data.expr_var.name,
                    .eid = resolved ? resolved->eid : (EntityID){0},
                },
            };
        }


        case Reg_ExprCast: {
            Register* cast_reg = entry->data.expr_cast.child_reg ? entry->data.expr_cast.child_reg  : reg;

            RegisterEntry* cast_inner = register_from_scope(cast_reg, entry->data.expr_cast.expr_id);
            IR_Expr* inner = cast_inner ? ir_expr_alloc(lower_expr(cast_reg, cast_inner)) : NULL;

            return (IR_Expr){
                .tag = IR_Expr_Cast,
                .origin = entry->decl_range,
                .ty = entry->data.expr_cast.ty ? *entry->data.expr_cast.ty : (Type){0},
                .data.cast = { .expr = inner },
            };
        }


        case Reg_ExprAddrOf: {
            IR_Expr* operand = entry->data.expr_unary.operand ? ir_expr_alloc(lower_expr(reg, entry->data.expr_unary.operand)) : NULL;
            return (IR_Expr){
                .tag = IR_Expr_AddrOf,
                .origin = entry->decl_range,
                .data.addr_of = { .expr = operand },
            };
        }

        case Reg_ExprUnary: {
            IR_Expr* operand = entry->data.expr_unary.operand ? ir_expr_alloc(lower_expr(reg, entry->data.expr_unary.operand)) : NULL;
            return (IR_Expr){
                .tag = IR_Expr_UnaryOp,
                .origin = entry->decl_range,
                .data.unary = {
                    .op = entry->data.expr_unary.op,
                    .operand = operand,
                },
            };
        }
        case Reg_ExprIdx: {
            RegisterEntry* base_entry  = register_from_global(entry->data.idx.base_id);
            RegisterEntry* index_entry = register_from_global(entry->data.idx.index_id);
            IR_Expr* object = base_entry  ? ir_expr_alloc(lower_expr(reg, base_entry))  : NULL;
            IR_Expr* index  = index_entry ? ir_expr_alloc(lower_expr(reg, index_entry)) : NULL;


            return (IR_Expr){
                .tag = IR_Expr_Idx,
                .ty  = entry->data.idx.elem_ty,
                .origin = entry->decl_range,
                .data.idx = {
                    .object   = object,
                    .index    = index,
                    .range    = entry->data.idx.range,
                    .is_const = entry->data.idx.is_const,
                },
            };
        }
        case Reg_ExprBinaryOp: {
            RegisterEntry* left_entry  = register_from_global(entry->data.expr_binary_op.left_id);
            RegisterEntry* right_entry = register_from_global(entry->data.expr_binary_op.right_id);

            IR_Expr* lhs = left_entry  ? ir_expr_alloc(lower_expr(reg, left_entry))  : NULL;
            IR_Expr* rhs = right_entry ? ir_expr_alloc(lower_expr(reg, right_entry)) : NULL;


            return (IR_Expr){
                .tag = IR_Expr_BinOp,
                .origin = entry->decl_range,
                .data.bin = {
                    .op  = entry->data.expr_binary_op.op,
                    .lhs = lhs,
                    .rhs = rhs,
                },
            };
        }

        case Reg_ExprArray: {
            size_t n = entry->data.array.elems_count;
            ARR(IR_Expr*) elems = {0};
            
            Register* array_reg = entry->data.array.child_reg ? entry->data.array.child_reg : reg;

            for (size_t i = 0; i < n; i++) {
                RegisterEntry* elem_entry = register_from_scope(array_reg, entry->data.array.elem_ids[i]);
                
                IR_Expr* lowered_elem = elem_entry ? ir_expr_alloc(lower_expr(array_reg, elem_entry)) : NULL;
                ARR_PUSH(elems, lowered_elem);
            }
                
            return (IR_Expr){
                .tag = IR_Expr_Array,
                .origin = entry->decl_range,
                .data.array = { 
                    .elems = elems.data, 
                    .elems_count = n, 
                    .ty = entry->data.array.ty,
                    .empty = entry->data.array.empty,
                },
            };
        }
        
        case Reg_ExprTuple: {
            size_t n = entry->data.expr_tuple.elems_count;
            ARR(IR_Expr*) elems = {0};
            LOWER_ARR(IR_Expr*, elems, entry->data.expr_tuple.elems_count, ir_expr_alloc(lower_expr(reg, entry->data.expr_tuple.elems[_i])));

            return (IR_Expr){
                .tag = IR_Expr_Tuple,
                .origin = entry->decl_range,
                .data.tuple = { .elems = elems.data, .elems_count = n },
            };
        }
        case Reg_Param:
        case Reg_Var:
        case Reg_Let: {
            return (IR_Expr){
                .tag = IR_Expr_VarRef,
                .data.var_ref = {
                    .name = entry->decl_name_range,
                    .eid  = entry->eid,
                },
                .origin = entry->decl_range,
                .ty = entry->data.var.type,
            };
        }


        case Reg_ExprField: {
            IR_Expr* object = entry->data.expr_field.object ? ir_expr_alloc(lower_expr(reg, entry->data.expr_field.object))  : NULL;

            return (IR_Expr){
                .tag = IR_Expr_Field,
                .data.field = {
                    .object   = object,
                    .field    = entry->data.expr_field.field,
                    .kind     = entry->data.expr_field.kind,
                    .type_eid = entry->data.expr_field.type_eid,
                },
                .origin = entry->data.expr_field.range,
            };
        }
        
        default:
            return (IR_Expr){0};
    }
}

IR_Module lower_stmt(Register *reg, IR_Module mod, IR_StmtArr *stmts_out) {
    for (uint32_t i = 0; i < reg->counter->next_id; i++) {
        RegisterEntry *entry = register_from_scope(reg, i);
        if (!entry) continue;

        switch (entry->tag) {
            case Reg_Function:  IR_MOD_PUSH(&mod, lower_function(reg, i));      break;
            case Reg_Struct:    IR_MOD_PUSH(&mod, lower_struct(reg, i));         break;
            case Reg_Enum:      IR_MOD_PUSH(&mod, lower_enum(reg, i));           break;
            case Reg_Trait:     IR_MOD_PUSH(&mod, lower_trait(reg, i));          break;
            
            case Reg_Extern:    lower_extern(reg, i, &mod); break;
            case Reg_Var:       lower_var(reg, i, &mod, stmts_out);              break;
            case Reg_Let:       lower_let(reg, i, &mod, stmts_out);              break;
            case Reg_Const:     lower_const(reg, i, &mod, stmts_out);            break;
            case Reg_If:
            case Reg_Elif:      if (stmts_out) lower_if(reg, i, stmts_out);      break;
            case Reg_While:     if (stmts_out) lower_while(reg, i, stmts_out);   break;
            case Reg_For:       if (stmts_out) lower_for(reg, i, stmts_out);     break;
            case Reg_Match:     if (stmts_out) lower_match(reg, i, stmts_out);   break;
            case Reg_Return:    if (stmts_out) lower_return(reg, i, stmts_out);  break;
            case Reg_ExprStmt:  if (stmts_out) lower_expr_stmt(reg, i, stmts_out); break;
            case Reg_Assign:    if (stmts_out) lower_assign(reg, i, stmts_out);  break;
            case Reg_Atomic:    if (stmts_out) lower_atomic(reg, i, stmts_out);  break;
            case Reg_EnumVariant: break;
            default: break;
        }
    }
    return mod;
}

