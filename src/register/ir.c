#include "import.h"
#include "ast.h"
#include "register.h"
#include "ir.h"

static IR_Expr lower_expr(Exprs *e, Register *reg);
static IR_Expr *lower_expr_alloc(Exprs *e, Register *reg);
static void lower_stmt(Stmts *s, Register *reg, IR_StmtArr *out, SourceRange fn_ret);
bool range_eq(SourceRange r, const char* str);
bool ranges_equal(SourceRange a, SourceRange b);
Register register_new(Register* parent, IDCounter* counter);
void register_free(Register* reg);
void register_insert(Register* reg, StringView name, RegisterEntry entry);
RegisterEntry* register_get(Register* reg, StringView name);
uint32_t register_fresh_id(Register* reg);
void insert_param(Register* reg, Param* p, Type t);
static void lower_stmt(Stmts *s, Register *reg, IR_StmtArr *out, SourceRange fn_ret);
static void lower_body(Stmts *body, size_t n, Register *reg, IR_StmtArr *out, SourceRange fn_ret);
static void hoist_decls(Stmts *body, size_t n, Register *reg, IR_StmtArr *out);


static StringView sv_of(SourceRange r) {
    return (StringView){ r.start, (size_t)(r.end - r.start) };
}

static Type range_to_type(SourceRange r, Register *reg) {
    if (range_eq(r, "i32"))  return (Type){ .tag = Type_Int, .data.int_t.bits = 32 };
    if (range_eq(r, "i64"))  return (Type){ .tag = Type_Int, .data.int_t.bits = 64 };
    if (range_eq(r, "int"))  return (Type){ .tag = Type_Int, .data.int_t.bits = 32 };
    if (range_eq(r, "f32"))  return (Type){ .tag = Type_Float, .data.float_t.bits = 32 };
    if (range_eq(r, "f64"))  return (Type){ .tag = Type_Float, .data.float_t.bits = 64 };
    if (range_eq(r, "bool")) return (Type){ .tag = Type_Bool };
    if (range_eq(r, "char")) return (Type){ .tag = Type_Char };
    if (range_eq(r, "str"))  return (Type){ .tag = Type_Str  };
    if (range_eq(r, "void")) return (Type){ .tag = Type_Void };

    RegisterEntry *e = register_get(reg, sv_of(r));

    if (e) return e->type;
    return (Type){ .tag = Type_Custom, .data.custom.name = r };
}

static uint32_t eid_of(Register *reg, SourceRange name) {
    RegisterEntry *e = register_get(reg, sv_of(name));
    return e ? e->eid.id : 0;
}

RegisterEntry *reg_get(Register *reg, SourceRange name) {
    return register_get(reg, sv_of(name));
}

static SourceRange expr_range(Exprs *e) {
    if (!e) return (SourceRange){0};
    switch (e->tag) {
    case Expr_Literals:     return e->data.literals.range;
    case Expr_Identifiers:  return e->data.identifiers.range;
    case Expr_Vars:         return e->data.vars.range;
    case Expr_BinaryOps:    return e->data.binary_ops.range;
    case Expr_Function:     return e->data.function_call.range;
    case Expr_MethodCalls:  return e->data.method_calls.range;
    case Expr_Struct_Calls: return e->data.struct_calls.range;
    case Expr_Class_Calls:  return e->data.class_calls.range;
    case Expr_Enum_Calls:   return e->data.enum_calls.range;
    default:                return (SourceRange){0};
    }
}

static IR_Expr emit_ssa_temp(IR_StmtArr *out, IR_Expr val, Register *reg) {
    uint32_t id = register_fresh_id(reg);
    SourceRange origin = val.origin;

    ARR_PUSH(*out, ((IR_Stmt){
        .tag = IR_Stmt_SsaTemp,
        .origin = origin,
        .data.ssa_temp = {
            .eid = id,
            .ty = val.ty,
            .val = ir_expr_alloc(val),
        },
    }));
    return (IR_Expr){
        .tag = IR_Expr_VarRef,
        .ty = val.ty,
        .origin = origin,
        .data.var_ref = { .name = origin, .eid = id },
    };
}

static IR_Expr lower_literal(SourceRange r) {
    if (range_eq(r, "true"))  return ir_literal_bool(true,  r);
    if (range_eq(r, "false")) return ir_literal_bool(false, r);

    if (r.start[0] == '"')  return ir_literal_str(r);
    if (r.start[0] == '\'') {
        size_t len = r.end - r.start;
        char ch = (len >= 2) ? r.start[1] : '\0';
        return ir_literal_char(ch, r);
    }

    size_t len = r.end - r.start;
    for (size_t i = 0; i < len; i++) {
        if (r.start[i] == '.') return ir_literal_float(strtod(r.start, NULL), r);
    }

    return ir_literal_int(strtoll(r.start, NULL, 10), r);
}

static IR_Expr lower_expr(Exprs *e, Register *reg) {
    if (!e) return (IR_Expr){ .tag = IR_Expr_Literal, .ty = { .tag = Type_Void } };

    switch (e->tag) {

    case Expr_Literals: return lower_literal(e->data.literals.range);

    case Expr_Identifiers: {
        SourceRange name = e->data.identifiers.name;
        RegisterEntry *ent = reg_get(reg, name);
        Type ty = ent ? ent->type : (Type){ .tag = Type_Void };
        return (IR_Expr){
            .tag = IR_Expr_VarRef,
            .ty = ty,
            .origin = name,
            .data.var_ref = { .name = name, .eid = eid_of(reg, name) },
        };
    }

    case Expr_Vars: {
        SourceRange name = e->data.vars.name;
        RegisterEntry *ent = reg_get(reg, name);
        Type ty = ent ? ent->type : (Type){ .tag = Type_Void };
        return (IR_Expr){
            .tag = IR_Expr_VarRef,
            .ty = ty,
            .origin = name,
            .data.var_ref = { .name = name, .eid = eid_of(reg, name) },
        };
    }

    case Expr_BinaryOps: {
        IR_StmtArr side_stmts = {0};
        IR_Expr lhs_raw = lower_expr(e->data.binary_ops.left,  reg);
        IR_Expr rhs_raw = lower_expr(e->data.binary_ops.right, reg);
        IR_Expr lhs = emit_ssa_temp(&side_stmts, lhs_raw, reg);
        IR_Expr rhs = emit_ssa_temp(&side_stmts, rhs_raw, reg);
        (void)side_stmts;

        LexerTokenTag op = e->data.binary_ops.op;
        Type ty = lhs.ty;
        switch (op) {
            case DoubleEqualss: 
            case NotEqualss: 
            case Lesses:
            case Greaters: 
            case LessEqualss: 
            case GreaterEqualss:
            case Ands: 
            case Ors: ty = (Type){ .tag = Type_Bool }; break;
            default: break;
        }
        return (IR_Expr){
            .tag = IR_Expr_BinOp,
            .ty = ty,
            .origin = e->data.binary_ops.range,
            .data.bin = {
                .op = op,
                .lhs = ir_expr_alloc(lhs),
                .rhs = ir_expr_alloc(rhs),
            },
        };
    }

    case Expr_Function: {
        SourceRange fname = e->data.function_call.name;
        RegisterEntry *ent = reg_get(reg, fname);
        Type ret = (ent && ent->tag == Reg_Function) ? ent->data.function.return_type : (Type){ .tag = Type_Void };
        IR_ExprPtrArr args_arr = {0};

        for (size_t i = 0; i < e->data.function_call.param_count; i++) {
            Exprs arg = {
                .tag = Expr_Identifiers,
                .data.identifiers = { .name = e->data.function_call.param[i].name },
            };
            ARR_PUSH(args_arr, ir_expr_alloc(lower_expr(&arg, reg)));
        }
        return (IR_Expr){
            .tag = IR_Expr_Call,
            .ty = ret,
            .origin = e->data.function_call.range,
            .data.call = {
                .name = fname,
                .eid = eid_of(reg, fname),
                .args = args_arr.data,
                .args_count = args_arr.len,
            },
        };
    }

    case Expr_MethodCalls: {
        IR_Expr obj = lower_expr(e->data.method_calls.object, reg);
        SourceRange mname = e->data.method_calls.method;
        Type ret = { .tag = Type_Void };
        if (obj.ty.tag == Type_Custom) {
            RegisterEntry *ce = reg_get(reg, obj.ty.data.custom.name);
            if (ce && ce->tag == Reg_Class) {
                for (size_t i = 0; i < ce->data.class.methods_count; i++) {
                    FunctionMethod *m = &ce->data.class.methods[i];
                    if (ranges_equal(m->name, mname)) {
                        ret = range_to_type(m->return_type, reg);
                        break;
                    }
                }
            }
        }
        IR_ExprPtrArr args_arr = {0};
        for (size_t i = 0; i < e->data.method_calls.args_count; i++)
            ARR_PUSH(args_arr, ir_expr_alloc(lower_expr(&e->data.method_calls.args[i], reg)));
        return (IR_Expr){
            .tag = IR_Expr_MethodCall,
            .ty = ret,
            .origin = e->data.method_calls.range,
            .data.method_call = {
                .object = ir_expr_alloc(obj),
                .method = mname,
                .method_eid = eid_of(reg, mname),
                .args = args_arr.data,
                .args_count = args_arr.len,
            },
        };
    }

    case Expr_Struct_Calls: {
        SourceRange sname = e->data.struct_calls.name;
        ARR(IR_FieldInit) fields_arr = {0};
        for (size_t i = 0; i < e->data.struct_calls.param_count; i++) {
            SourceRange fn = e->data.struct_calls.param[i].name;
            Exprs val_expr = {
                .tag = Expr_Identifiers,
                .data.identifiers = { .name = fn },
            };
            ARR_PUSH(fields_arr, ((IR_FieldInit){
                .name = fn,
                .val = ir_expr_alloc(lower_expr(&val_expr, reg)),
            }));
        }
        return (IR_Expr){
            .tag = IR_Expr_MakeStruct,
            .ty = (Type){ .tag = Type_Custom, .data.custom.name = sname },
            .origin = e->data.struct_calls.range,
            .data.make_struct = {
                .name = sname,
                .eid = eid_of(reg, sname),
                .fields = fields_arr.data,
                .fields_count = fields_arr.len,
            },
        };
    }

    case Expr_Class_Calls: {
        SourceRange cname = e->data.class_calls.name;
        IR_ExprPtrArr args_arr = {0};

        for (size_t i = 0; i < e->data.class_calls.param_count; i++) {
            Exprs arg = {
                .tag = Expr_Identifiers,
                .data.identifiers = { .name = e->data.class_calls.param[i].name },
            };
            ARR_PUSH(args_arr, ir_expr_alloc(lower_expr(&arg, reg)));
        }
        return (IR_Expr){
            .tag = IR_Expr_MakeClass,
            .ty = (Type){ .tag = Type_Custom, .data.custom.name = cname },
            .origin = e->data.class_calls.range,
            .data.make_class = {
                .name = cname,
                .eid = eid_of(reg, cname),
                .args = args_arr.data,
                .args_count = args_arr.len,
            },
        };
    }

    case Expr_Enum_Calls: {
        SourceRange ename = e->data.enum_calls.name;
        SourceRange variant = e->data.enum_calls.field;
        IR_ExprPtrArr args_arr = {0};
        for (size_t i = 0; i < e->data.enum_calls.param_count; i++) {
            Exprs arg = {
                .tag = Expr_Identifiers,
                .data.identifiers = { .name = e->data.enum_calls.param[i].name },
            };
            ARR_PUSH(args_arr, ir_expr_alloc(lower_expr(&arg, reg)));
        }
        return (IR_Expr){
            .tag = IR_Expr_MakeEnum,
            .ty = (Type){ .tag = Type_Custom, .data.custom.name = ename },
            .origin = e->data.enum_calls.range,
            .data.make_enum = {
                .type_name = ename,
                .variant = variant,
                .eid = eid_of(reg, variant),
                .args = args_arr.data,
                .args_count = args_arr.len,
            },
        };
    }

    default:
        return (IR_Expr){ .tag = IR_Expr_Literal, .ty = { .tag = Type_Void } };
    }
}

static IR_Expr *lower_expr_alloc(Exprs *e, Register *reg) {
    return ir_expr_alloc(lower_expr(e, reg));
}

static IR_Expr lower_assign_target(Exprs *e, Register *reg) {
    if (e && e->tag == Expr_Unary && e->data.unary.op == Stars) {
        IR_Expr ptr = lower_expr(e->data.unary.operand, reg);
        Type inner = ptr.ty.tag == Type_Ptr ? *ptr.ty.data.ptr.inner : (Type){ .tag = Type_Void };

        return (IR_Expr){
            .tag = IR_Expr_Deref,
            .ty = inner,
            .origin = expr_range(e),
            .data.deref = { .ptr = ir_expr_alloc(ptr) },
        };
    }
    return lower_expr(e, reg);
}

static void hoist_decls(Stmts *body, size_t n, Register *reg, IR_StmtArr *out) {
    for (size_t i = 0; i < n; i++) {
        Stmts *s = &body[i];
        switch (s->tag) {
        case Stmt_Vars: {
            SourceRange name = s->data.vars.name;
            RegisterEntry *ent = reg_get(reg, name);
            Type ty = ent ? ent->type : (Type){ .tag = Type_Void };
            ARR_PUSH(*out, ((IR_Stmt){
                .tag = IR_Stmt_VarDecl,
                .origin = s->data.vars.range,
                .data.var_decl = {
                    .name = name,
                    .eid = ent ? ent->eid.id : 0,
                    .ty = ty,
                    .init = NULL,
                    .mode = s->data.vars.mode,
                },
            }));
            break;
        }

        case Stmt_Lets: {
            SourceRange name = s->data.lets.name;
            RegisterEntry *ent = reg_get(reg, name);
            Type ty = ent ? ent->type : (Type){ .tag = Type_Void };
            ARR_PUSH(*out, ((IR_Stmt){
                .tag = IR_Stmt_LetDecl,
                .origin = s->data.lets.range,
                .data.let_decl = {
                    .name = name,
                    .eid = ent ? ent->eid.id : 0,
                    .ty = ty,
                    .init = NULL,
                    .mode = s->data.lets.mode,
                },
            }));
            break;
        }

        case Stmt_Consts: {
            SourceRange name = s->data.consts.name;
            RegisterEntry *ent = reg_get(reg, name);
            Type ty = ent ? ent->type : (Type){ .tag = Type_Void };
            ARR_PUSH(*out, ((IR_Stmt){
                .tag = IR_Stmt_ConstDecl,
                .origin = s->data.consts.range,
                .data.const_decl = {
                    .name = name,
                    .eid = ent ? ent->eid.id : 0,
                    .ty = ty,
                    .init = NULL,
                },
            }));
            break;
        }

        case Stmt_Locals: {
            SourceRange name = s->data.locals.name;
            RegisterEntry *ent = reg_get(reg, name);
            Type ty = ent ? ent->type : (Type){ .tag = Type_Void };
            ARR_PUSH(*out, ((IR_Stmt){
                .tag = IR_Stmt_LocalDecl,
                .origin = s->data.locals.range,
                .data.local_decl = {
                    .name = name,
                    .eid = ent ? ent->eid.id : 0,
                    .ty = ty,
                },
            }));
            break;
        }

        case Stmt_Ifs:
            hoist_decls(s->data.ifs.body, s->data.ifs.body_count, reg, out);
            hoist_decls(s->data.ifs.else_body, s->data.ifs.else_body_count, reg, out);
            break;

        case Stmt_Whiles:
            hoist_decls(s->data.whiles.body, s->data.whiles.body_count, reg, out);
            break;

        case Stmt_Fors:
            hoist_decls(s->data.fors.body, s->data.fors.body_count, reg, out);
            break;

        case Stmt_Matchs:
            for (size_t c = 0; c < s->data.matchs.cases_count; c++)
                hoist_decls(s->data.matchs.cases[c].body, s->data.matchs.cases[c].body_count, reg, out);
            if (s->data.matchs.default_body_count > 0)
                hoist_decls(s->data.matchs.default_body, s->data.matchs.default_body_count, reg, out);
            break;

        default: break;
        }
    }
}

static void lower_body(Stmts *body, size_t n, Register *reg, IR_StmtArr *out, SourceRange fn_ret) {
    for (size_t i = 0; i < n; i++)
        lower_stmt(&body[i], reg, out, fn_ret);
}

static IR_Stmt lower_match(Stmts *s, Register *reg, SourceRange fn_ret) {
    IR_Expr *mexpr = lower_expr_alloc(&s->data.matchs.expr, reg);

    ARR(IR_MatchArm) arms_arr = {0};
    for (size_t i = 0; i < s->data.matchs.cases_count; i++) {
        MatchArm *src = &s->data.matchs.cases[i];
        IR_StmtArr arm_body = {0};

        lower_body(src->body, src->body_count, reg, &arm_body, fn_ret);
        ARR_PUSH(arms_arr, ((IR_MatchArm){
            .pattern = src->pattern,
            .body = arm_body.data,
            .body_count = arm_body.len,
        }));
    }

    IR_StmtArr def_arr = {0};

    if (s->data.matchs.default_body_count > 0) lower_body(s->data.matchs.default_body, s->data.matchs.default_body_count, reg, &def_arr, fn_ret);

    return (IR_Stmt){
        .tag = IR_Stmt_Match,
        .origin = s->data.matchs.range,
        .data.match = {
            .expr = mexpr,
            .arms = arms_arr.data,
            .arms_count = arms_arr.len,
            .default_body = def_arr.data,
            .default_body_count = def_arr.len,
        },
    };
}

static IR_Stmt lower_if(Stmts *s, Register *reg, SourceRange fn_ret) {
    IR_Expr *cond = lower_expr_alloc(&s->data.ifs.cond, reg);

    IR_StmtArr then_arr = {0};
    IR_StmtArr else_arr = {0};

    lower_body(s->data.ifs.body, s->data.ifs.body_count, reg, &then_arr, fn_ret);
    lower_body(s->data.ifs.else_body, s->data.ifs.else_body_count, reg, &else_arr, fn_ret);

    return (IR_Stmt){
        .tag = IR_Stmt_If,
        .origin = s->data.ifs.range,
        .data.if_ = {
            .cond = cond,
            .guard_pattern = s->data.ifs.guard_pattern,
            .body = then_arr.data,
            .body_count = then_arr.len,
            .else_body = else_arr.data,
            .else_body_count = else_arr.len,
        },
    };
}

static bool expr_exists(Exprs expr) {
    return expr.data.literals.range.start != NULL;
}

static IR_Stmt lower_while(Stmts *s, Register *reg, SourceRange fn_ret) {
    IR_Expr *cond = lower_expr_alloc(&s->data.whiles.cond, reg);

    IR_StmtArr body_arr = {0};
    lower_body(s->data.whiles.body, s->data.whiles.body_count, reg, &body_arr, fn_ret);

    return (IR_Stmt){
        .tag = IR_Stmt_While,
        .origin = s->data.whiles.range,
        .data.while_ = {
            .cond = cond,
            .body = body_arr.data,
            .body_count = body_arr.len,
        },
    };
}

static IR_Stmt lower_for(Stmts *s, Register *reg, SourceRange fn_ret) {
    SourceRange var = s->data.fors._var;
    RegisterEntry *var_ent = reg_get(reg, var);
    Type var_ty = var_ent ? var_ent->type : (Type){ .tag = Type_Void };

    IR_Expr *iter = lower_expr_alloc(&s->data.fors.iter, reg);

    IR_StmtArr body_arr = {0};
    lower_body(s->data.fors.body, s->data.fors.body_count, reg, &body_arr, fn_ret);

    return (IR_Stmt){
        .tag = IR_Stmt_For,
        .origin = s->data.fors.range,
        .data.for_ = {
            .var = var,
            .var_eid = var_ent ? var_ent->eid.id : 0,
            .var_ty = var_ty,
            .iter = iter,
            .body = body_arr.data,
            .body_count = body_arr.len,
        },
    };
}

static void lower_stmt(Stmts *s, Register *reg, IR_StmtArr *out, SourceRange fn_ret) {
    if (!s) return;

    switch (s->tag) {
    case Stmt_Vars: {
        if (expr_exists(s->data.vars.value)) {
            SourceRange name = s->data.vars.name;
            RegisterEntry *ent = reg_get(reg, name);
            IR_Expr target = (IR_Expr){
                .tag = IR_Expr_VarRef,
                .ty = ent ? ent->type : (Type){ .tag = Type_Void },
                .origin = name,
                .data.var_ref = { .name = name, .eid = ent ? ent->eid.id : 0 },
            };
            ARR_PUSH(*out, ((IR_Stmt){
                .tag = IR_Stmt_Assign,
                .origin = s->data.vars.range,
                .data.assign = {
                    .target = ir_expr_alloc(target),
                    .op = Equalss,
                    .value = lower_expr_alloc(&s->data.vars.value, reg),
                },
            }));
        }
        break;
    }

    case Stmt_Lets: {
        if (expr_exists(s->data.lets.value)) {
            SourceRange name = s->data.lets.name;
            RegisterEntry *ent = reg_get(reg, name);

            IR_Expr target = (IR_Expr){
                .tag = IR_Expr_VarRef,
                .ty = ent ? ent->type : (Type){ .tag = Type_Void },
                .origin = name,
                .data.var_ref = { .name = name, .eid = ent ? ent->eid.id : 0 },
            };
            ARR_PUSH(*out, ((IR_Stmt){
                .tag = IR_Stmt_Assign,
                .origin = s->data.lets.range,
                .data.assign = {
                    .target = ir_expr_alloc(target),
                    .op = Equalss,
                    .value = lower_expr_alloc(&s->data.lets.value, reg),
                },
            }));
        }
        break;
    }

    case Stmt_Consts: {
        if (expr_exists(s->data.consts.value)) {
            SourceRange name = s->data.consts.name;
            RegisterEntry *ent = reg_get(reg, name);
            IR_Expr target = (IR_Expr){
                .tag = IR_Expr_VarRef,
                .ty = ent ? ent->type : (Type){ .tag = Type_Void },
                .origin = name,
                .data.var_ref = { .name = name, .eid = ent ? ent->eid.id : 0 },
            };
            ARR_PUSH(*out, ((IR_Stmt){
                .tag = IR_Stmt_Assign,
                .origin = s->data.consts.range,
                .data.assign = {
                    .target = ir_expr_alloc(target),
                    .op = Equalss,
                    .value = lower_expr_alloc(&s->data.consts.value, reg),
                },
            }));
        }
        break;
    }

    case Stmt_Locals:
        break;

    case Stmt_Assigns: {
        IR_Expr target = lower_assign_target(&s->data.assigns.target, reg);
        IR_Expr value  = lower_expr(&s->data.assigns.value, reg);

        ARR_PUSH(*out, ((IR_Stmt){
            .tag = IR_Stmt_Assign,
            .origin = s->data.assigns.range,
            .data.assign = {
                .target = ir_expr_alloc(target),
                .op = s->data.assigns.op,
                .value = ir_expr_alloc(value),
            },
        }));
        break;
    }

    case Stmt_Returns: {
        IR_Expr tmp = lower_expr(&s->data.returns.expr, reg);
        IR_Expr *val = tmp.ty.tag != Type_Void ? ir_expr_alloc(tmp) : NULL;
        ARR_PUSH(*out, ((IR_Stmt){
            .tag = IR_Stmt_Return,
            .origin = s->data.returns.range,
            .data.ret = { .val = val },
        }));
        break;
    }

    case Stmt_Ifs:    ARR_PUSH(*out, lower_if(s, reg, fn_ret));    break;
    case Stmt_Whiles: ARR_PUSH(*out, lower_while(s, reg, fn_ret)); break;
    case Stmt_Fors:   ARR_PUSH(*out, lower_for(s, reg, fn_ret));   break;
    case Stmt_Matchs: ARR_PUSH(*out, lower_match(s, reg, fn_ret)); break;

    case Stmt_ExprStmt: {
        IR_Expr ex = lower_expr(&s->data.expr_stmt.expr, reg);
        ARR_PUSH(*out, ((IR_Stmt){
            .tag = IR_Stmt_Expr,
            .origin = expr_range(&s->data.expr_stmt.expr),
            .data.expr = { .expr = ir_expr_alloc(ex) },
        }));
        break;
    }

    case Stmt_Functions:
    case Stmt_Structs:
    case Stmt_Enums:
    case Stmt_Classes:
    case Stmt_Traits:
    default:
        break;
    }
}

static IR_FuncDef lower_func_def(Stmts *s, Register *reg) {
    SourceRange fname   = s->data.functions.name;
    Param      *params  = s->data.functions.params;
    size_t      pc      = s->data.functions.params_count;
    SourceRange ret_range = s->data.functions.return_type;

    IDCounter dummy_counter = *reg->counter;
    Register child = register_new(reg, &dummy_counter);

    ARR(IR_Param) params_arr = {0};
    for (size_t i = 0; i < pc; i++) {
        Type t = range_to_type(params[i].c_type, reg);
        ARR_PUSH(params_arr, ((IR_Param){
            .name = params[i].name,
            .ty = t,
            .mode = VarMode_Value,
        }));
        insert_param(&child, &params[i], t);
    }


    IR_StmtArr body_arr = {0};

    hoist_decls(s->data.functions.body, s->data.functions.body_count, &child, &body_arr);
    lower_body(s->data.functions.body, s->data.functions.body_count, &child, &body_arr, ret_range);

    register_free(&child);

    return (IR_FuncDef){
        .name = fname,
        .return_type = range_to_type(ret_range, reg),
        .params = params_arr.data,
        .params_count = params_arr.len,
        .body = body_arr.data,
        .body_count = body_arr.len,
        .is_pub = s->data.functions.is_pub,
        .is_unsafe = false,
        .operation_op = 0,
        .cc = CC_Default,
    };
}

static IR_FuncDef lower_method_def(FunctionMethod *m, Register *reg) {
    size_t pc = m->params_count;

    ARR(IR_Param) params_arr = {0};
    for (size_t i = 0; i < pc; i++) {
        ARR_PUSH(params_arr, ((IR_Param){
            .name = m->params[i].name,
            .ty = range_to_type(m->params[i].c_type, reg),
            .mode = VarMode_Value,
        }));
    }

    IDCounter dummy_counter = *reg->counter;
    Register child = register_new(reg, &dummy_counter);
    for (size_t i = 0; i < pc; i++)
        insert_param(&child, &m->params[i], params_arr.data[i].ty);

    IR_StmtArr body_arr = {0};

    hoist_decls(m->body, m->body_count, &child, &body_arr);
    lower_body(m->body, m->body_count, &child, &body_arr, m->return_type);

    register_free(&child);

    return (IR_FuncDef){
        .name = m->name,
        .return_type = range_to_type(m->return_type, reg),
        .params = params_arr.data,
        .params_count = params_arr.len,
        .body = body_arr.data,
        .body_count = body_arr.len,
        .is_pub = true,
        .is_unsafe = false,
        .operation_op = m->operation.function.start != NULL ? m->operation.op : 0,
        .cc = CC_Default,
    };
}

IR_Module lower_module(const char *name, Stmts *body, size_t n, Register *reg) {
    IR_Module mod = {
        .name = (char *)name,
        .defs = NULL,
        .defs_count = 0,
        .defs_cap = 0,
    };

    for (size_t i = 0; i < n; i++) {
        Stmts *s = &body[i];

        switch (s->tag) {

        case Stmt_Functions: {
            IR_FuncDef def = lower_func_def(s, reg);
            ir_module_push(&mod, (IR_Def){
                .tag = IR_Def_Function,
                .origin = s->data.functions.range,
                .data.function = { .def = def },
            });
            break;
        }

        case Stmt_Structs: {
            SourceRange sname = s->data.structs.name;
            ARR(IR_FieldDef) fields_arr = {0};
            for (size_t f = 0; f < s->data.structs.fields_count; f++) {
                ARR_PUSH(fields_arr, ((IR_FieldDef){
                    .name = s->data.structs.fields[f].name,
                    .ty = range_to_type(s->data.structs.fields[f].c_type, reg),
                }));
            }
            RegisterEntry *ent = reg_get(reg, sname);
            ir_module_push(&mod, (IR_Def){
                .tag = IR_Def_Struct,
                .origin = s->data.structs.range,
                .data.struct_ = {
                    .name = sname,
                    .eid = ent ? ent->eid.id : 0,
                    .fields = fields_arr.data,
                    .fields_count = fields_arr.len,
                    .is_pub = s->data.structs.is_pub,
                },
            });
            break;
        }

        case Stmt_Enums: {
            SourceRange ename = s->data.enums.name;
            ARR(IR_VariantDef) variants_arr = {0};
            for (size_t v = 0; v < s->data.enums.variants_count; v++) {
                EnumVariant *ev = &s->data.enums.variants[v];
                ARR(IR_FieldDef) fds_arr = {0};
                for (size_t f = 0; f < ev->fields_count; f++) {
                    ARR_PUSH(fds_arr, ((IR_FieldDef){
                        .name = ev->fields[f].first,
                        .ty = range_to_type(ev->fields[f].second, reg),
                    }));
                }
                ARR_PUSH(variants_arr, ((IR_VariantDef){
                    .name = ev->name,
                    .fields = fds_arr.data,
                    .fields_count = fds_arr.len,
                }));
            }
            RegisterEntry *ent = reg_get(reg, ename);
            ir_module_push(&mod, (IR_Def){
                .tag = IR_Def_Enum,
                .origin = s->data.enums.range,
                .data.enum_ = {
                    .name = ename,
                    .eid = ent ? ent->eid.id : 0,
                    .variants = variants_arr.data,
                    .variants_count = variants_arr.len,
                    .is_pub = s->data.enums.is_pub,
                },
            });
            break;
        }

        case Stmt_Externs: {
            ExternBlock* block = &s->data.externs.block;
            SourceRange ffi = s->data.externs.ffi;

            for (size_t i = 0; i < block->funcs_count; i++) {
                ExternFunction* fn = &block->funcs[i];

                ARR(IR_Param) params_arr = {0};
                for (size_t j = 0; j < fn->params_count; j++) {
                    ARR_PUSH(params_arr, ((IR_Param){
                        .name = fn->params[j].name,
                        .ty = range_to_type(fn->params[j].c_type, reg),
                        .mode = VarMode_Value,
                    }));
                }

                ir_module_push(&mod, (IR_Def){
                    .tag = IR_Def_Extern,
                    .origin = block->range,
                    .data.extern_ = {
                        .abi = block->abi,
                        .ffi = ffi,
                        .name = fn->name,
                        .eid = eid_of(reg, fn->name),
                        .return_type = range_to_type(fn->return_type, reg),
                        .params = params_arr.data,
                        .params_count = params_arr.len,
                        .is_pub = true,
                    }
                });
            }
            break;
        }

        case Stmt_Classes: {
            SourceRange cname = s->data.classes.name;
            ARR(IR_FieldDef) fields_arr = {0};
            for (size_t f = 0; f < s->data.classes.fields_count; f++) {
                ARR_PUSH(fields_arr, ((IR_FieldDef){
                    .name = s->data.classes.fields[f].name,
                    .ty = range_to_type(s->data.classes.fields[f].c_type, reg),
                }));
            }
            ARR(IR_FuncDef) methods_arr = {0};
            for (size_t m = 0; m < s->data.classes.methods_count; m++)
                ARR_PUSH(methods_arr, lower_method_def(&s->data.classes.methods[m], reg));
            RegisterEntry *ent = reg_get(reg, cname);
            ir_module_push(&mod, (IR_Def){
                .tag = IR_Def_Class,
                .origin = s->data.classes.range,
                .data.class_ = {
                    .name = cname,
                    .eid = ent ? ent->eid.id : 0,
                    .fields = fields_arr.data,
                    .fields_count = fields_arr.len,
                    .methods = methods_arr.data,
                    .methods_count = methods_arr.len,
                    .is_pub = true,
                },
            });
            break;
        }

        case Stmt_Traits: {
            SourceRange tname = s->data.traits.name;
            ARR(IR_FuncDef) methods_arr = {0};
            for (size_t m = 0; m < s->data.traits.methods_count; m++) {
                TraitMethod *tm = &s->data.traits.methods[m];
                ARR(IR_Param) params_arr = {0};
                for (size_t p = 0; p < tm->params_count; p++) {
                    ARR_PUSH(params_arr, ((IR_Param){
                        .name = tm->params[p].name,
                        .ty = range_to_type(tm->params[p].c_type, reg),
                        .mode = VarMode_Value,
                    }));
                }
                ARR_PUSH(methods_arr, ((IR_FuncDef){
                    .name = tm->name,
                    .return_type = range_to_type(tm->return_type, reg),
                    .params = params_arr.data,
                    .params_count = params_arr.len,
                    .body = NULL,
                    .body_count = 0,
                    .is_pub = true,
                    .cc = CC_Default,
                }));
            }
            RegisterEntry *ent = reg_get(reg, tname);
            ir_module_push(&mod, (IR_Def){
                .tag = IR_Def_Trait,
                .origin = s->data.traits.range,
                .data.trait_ = {
                    .name = tname,
                    .eid = ent ? ent->eid.id : 0,
                    .methods = methods_arr.data,
                    .methods_count = methods_arr.len,
                    .is_pub = s->data.traits.is_pub,
                },
            });
            break;
        }

        default:
            break;
        }
    }

    return mod;
}
