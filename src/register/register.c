#include "import.h"
#include "register.h"
#include "error.h"
#include "ir.h"


bool register_expr(Exprs* expr, Register* reg, CheckerErrList* errors);
void check_expr(Exprs* expr, Register* reg, CheckerErrList* errors);
void resolve_operations(Exprs* expr, Register* reg, CheckerErrList* errors);
Type infer_expr_type(Exprs* expr, Register* reg);
bool register_class(Stmts* stmt, Register* reg, CheckerErrList* errors);
StringView string(const char* s);
void check_expr(Exprs* expr, Register* reg, CheckerErrList* errors);
void check_if_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors, Exprs* parent_cond);
void check_while_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors, Exprs* parent_cond);
void check_for_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors);
void check_match_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors);
void check_vars_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors);
void check_lets_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors);
void check_consts_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors);
void check_locals_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors);
void check_assign_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors);
void check_return_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors, SourceRange fn_return_type);
void check_function_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors);
void check_struct_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors);
void check_enum_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors);
void check_class_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors);
void check_trait_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors);
StringView string_range(SourceRange range);
static void check_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors, SourceRange fn_return_type, Exprs* parent_cond);

StringView string_range(SourceRange range) {
    return (StringView){
        .ptr = range.start,
        .len = (size_t)(range.end - range.start),
    };
}

static inline StringView sv(const char* s) {
    return (StringView){ .ptr = s, .len = s ? strlen(s) : 0 };
}

static inline StringView type_tag_to_view(Type t) {
    switch (t.tag) {
        case Type_Int:    return sv(t.data.int_t.bits == 64 ? "i64" : "i32");
        case Type_Float:  return sv(t.data.float_t.bits == 64 ? "f64" : "f32");
        case Type_Bool:   return sv("bool");
        case Type_Char:   return sv("char");
        case Type_Str:    return sv("str");
        case Type_Void:   return sv("void");
        case Type_Custom: return string_range(t.data.custom.name);
        default:          return sv("unknown");
    }
}

static bool expr_exists(Exprs expr) {
    return expr.data.literals.range.start != NULL;
}


static char* null_term_view_alloc(StringView name) {
    char* out = malloc(name.len + 1);
    memcpy(out, name.ptr, name.len);
    out[name.len] = '\0';
    return out;
}

Register register_new(Register* parent, IDCounter* counter) {
    return (Register){ 
        .table = register_table_init(), 
        .pending = pending_table_init(),
        .parent = parent, 
        .counter = counter 
    };
}


void register_free(Register* reg) {
    if (!reg->table) return;

    khint_t it;
    kh_foreach(reg->table, it) {
        free(kh_val(reg->table, it).name);
    }

    register_table_destroy(reg->table);
    pending_table_destroy(reg->pending);
    reg->table = NULL;
    reg->pending = NULL;
}

void register_insert(Register* reg, StringView name, RegisterEntry entry) {
    khint_t pit = pending_table_get(reg->pending, name);
    int absent = 0;

    if (pit != kh_end(reg->pending)) { entry.eid = kh_val(reg->pending, pit); pending_table_del(reg->pending, pit); } else { entry.eid = (EntityID){ .id = reg->counter->next_id++, .kind = entry.tag }; }
    khint_t it = register_table_put(reg->table, name, &absent);

    if (!absent) {
        if (entry.name && entry.name != kh_val(reg->table, it).name) free(entry.name);
        entry.name = kh_val(reg->table, it).name;
        kh_val(reg->table, it) = entry;
        return;
    }

    if (!entry.name) entry.name = null_term_view_alloc(name);
    kh_key(reg->table, it) = (StringView){ .ptr = entry.name, .len = strlen(entry.name) };
    kh_val(reg->table, it) = entry;
}


RegisterEntry* register_get(Register* reg, StringView name) {
    for (Register* r = reg; r != NULL; r = r->parent) {
        khint_t it = register_table_get(r->table, name);
        if (it != kh_end(r->table)) return &kh_val(r->table, it);
    }
    return NULL;
}


Type resolve_type(SourceRange r, Register* reg) {
    size_t len = r.end - r.start;

    if (len == 3 && memcmp(r.start, "i32",  3) == 0) return (Type){ .tag = Type_Int,   .data.int_t.bits = 32 };
    if (len == 3 && memcmp(r.start, "i64",  3) == 0) return (Type){ .tag = Type_Int,   .data.int_t.bits = 64 };
    if (len == 3 && memcmp(r.start, "f32",  3) == 0) return (Type){ .tag = Type_Float, .data.float_t.bits = 32 };
    if (len == 3 && memcmp(r.start, "f64",  3) == 0) return (Type){ .tag = Type_Float, .data.float_t.bits = 64 };
    if (len == 4 && memcmp(r.start, "bool", 4) == 0) return (Type){ .tag = Type_Bool };
    if (len == 4 && memcmp(r.start, "char", 4) == 0) return (Type){ .tag = Type_Char };
    if (len == 3 && memcmp(r.start, "str",  3) == 0) return (Type){ .tag = Type_Str  };

    RegisterEntry* entry = register_get(reg, string_range(r));

    if (entry) {
        switch (entry->tag) {
            case Reg_Class:  return (Type){ .tag = Type_Custom, .data.custom.name = r };
            case Reg_Struct: return (Type){ .tag = Type_Custom, .data.custom.name = r };
            case Reg_Enum:   return (Type){ .tag = Type_Custom, .data.custom.name = r };
            default: break;
        }
    }

    return (Type){ .tag = Type_Void };
}


void populate_register(Stmts* body, size_t body_count, Register* reg, CheckerErrList* errors) {
    for (size_t i = 0; i < body_count; i++) {
        Stmts* stmt = &body[i];

        switch (stmt->tag) {
            case Stmt_Vars: {
                StringView key = string_range(stmt->data.vars.name);

                RegisterEntry* existing = register_get(reg, key);
                if (existing) {
                    checker_err_push(errors, (CheckerErr){
                        .tag = Err_Tag_RDL,
                        .data.rdl = { .range = stmt->data.vars.range, .var_name = sv(existing->name) }
                    });
                    break;
                }

                Type t = (stmt->data.vars.c_type.start != stmt->data.vars.c_type.end) ? resolve_type(stmt->data.vars.c_type, reg) : infer_expr_type(&stmt->data.vars.value, reg);

                if (expr_exists(stmt->data.vars.value) && stmt->data.vars.c_type.start != stmt->data.vars.c_type.end) {
                    Type value_type = infer_expr_type(&stmt->data.vars.value, reg);
                    if (t.tag != value_type.tag) {
                        checker_err_push(errors, (CheckerErr){
                            .tag = Err_Tag_VMV,
                            .data.vmv = {
                                .range = stmt->data.vars.range,
                                .var_name = key,
                                .expected_type = type_tag_to_view(t),
                                .actual_type = type_tag_to_view(value_type),
                            }
                        });
                        break;
                    }
                }

                register_insert(reg, key, (RegisterEntry){
                    .tag = Reg_Var,
                    .name = NULL,
                    .type = t,
                    .decl_range = stmt->data.vars.range,
                    .decl_name_range = stmt->data.vars.name,
                    .data.var = { .type = t, .mode = stmt->data.vars.mode, .is_mut = true }
                });
                break;
            }

            case Stmt_Lets: {
                StringView key = string_range(stmt->data.lets.name);
                RegisterEntry* existing = register_get(reg, key);
            
                if (existing) {
                    checker_err_push(errors, (CheckerErr){
                        .tag = Err_Tag_RDL,
                        .data.rdl = { .range = stmt->data.lets.range, .var_name = sv(existing->name) }
                    });
                    break;
                }

                Type t = (stmt->data.lets.c_type.start != stmt->data.lets.c_type.end) ? resolve_type(stmt->data.lets.c_type, reg) : infer_expr_type(&stmt->data.lets.value, reg);

                if (expr_exists(stmt->data.lets.value) && stmt->data.lets.c_type.start != stmt->data.lets.c_type.end) {
                    Type value_type = infer_expr_type(&stmt->data.lets.value, reg);
                    if (t.tag != value_type.tag) {
                        checker_err_push(errors, (CheckerErr){
                            .tag = Err_Tag_VMV,
                            .data.vmv = {
                                .range = stmt->data.lets.range,
                                .var_name = key,
                                .expected_type = type_tag_to_view(t),
                                .actual_type = type_tag_to_view(value_type),
                            }
                        });
                        break;
                    }
                }

                register_insert(reg, key, (RegisterEntry){
                    .tag = Reg_Let,
                    .name = NULL,
                    .type = t,
                    .decl_range = stmt->data.lets.range,
                    .decl_name_range = stmt->data.lets.name,
                    .data.let = { .type = t, .mode = stmt->data.lets.mode }
                });
                break;
            }

            case Stmt_Consts: {
                StringView key = string_range(stmt->data.consts.name);

                if (!expr_exists(stmt->data.consts.value)) {
                    checker_err_push(errors, (CheckerErr){
                        .tag = Err_Tag_CVN,
                        .data.cvn = { .range = stmt->data.consts.range, .var_name = key }
                    });
                    break;
                }

                RegisterEntry* existing = register_get(reg, key);
                if (existing) {
                    checker_err_push(errors, (CheckerErr){
                        .tag = Err_Tag_RDL,
                        .data.rdl = { .range = stmt->data.consts.range, .var_name = sv(existing->name) }
                    });
                    break;
                }

                Type t = (stmt->data.consts.c_type.start != stmt->data.consts.c_type.end) ? resolve_type(stmt->data.consts.c_type, reg) : infer_expr_type(&stmt->data.consts.value, reg);

                register_insert(reg, key, (RegisterEntry){
                    .tag = Reg_Const,
                    .name = NULL,
                    .type = t,
                    .decl_range = stmt->data.consts.range,
                    .decl_name_range = stmt->data.consts.name,
                    .data.const_ = { .type = t, .is_pub = stmt->data.consts.is_pub }
                });
                break;
            }

            case Stmt_Locals: {
                StringView key = string_range(stmt->data.locals.name);

                RegisterEntry* existing = register_get(reg, key);
                if (existing) {
                    checker_err_push(errors, (CheckerErr){
                        .tag = Err_Tag_RDL,
                        .data.rdl = { .range = stmt->data.locals.range, .var_name = sv(existing->name) }
                    });
                    break;
                }

                Type t = resolve_type(stmt->data.locals.c_type, reg);
                if (t.tag == Type_Void) {
                    checker_err_push(errors, (CheckerErr){
                        .tag = Err_Tag_TNF,
                        .data.tnf = { .range = stmt->data.locals.range, .type_name = key }
                    });
                    break;
                }

                register_insert(reg, key, (RegisterEntry){
                    .tag = Reg_Local,
                    .name = NULL,
                    .type = t,
                    .decl_range = stmt->data.locals.range,
                    .decl_name_range = stmt->data.locals.name,
                    .data.local = { .type = t, .is_pub = stmt->data.locals.is_pub }
                });
                break;
            }

            case Stmt_Classes: register_class(stmt, reg, errors); break;
            default: break;
        }
    }
}


Type infer_literal_type(SourceRange range) {
    size_t len = range.end - range.start;
    if (len == 0) return (Type){ .tag = Type_Void };
    if ((len == 4 && memcmp(range.start, "true",  4) == 0) ||
        (len == 5 && memcmp(range.start, "false", 5) == 0))
        return (Type){ .tag = Type_Bool };
    if (range.start[0] == '"')  return (Type){ .tag = Type_Str  };
    if (range.start[0] == '\'') return (Type){ .tag = Type_Char };

    bool has_dot = false;
    for (size_t i = 0; i < len; i++) {
        if (range.start[i] == '.') { has_dot = true; break; }
    }

    if (has_dot) return (Type){ .tag = Type_Float, .data.float_t.bits = 64 };
    return (Type){ .tag = Type_Int, .data.int_t.bits = 32 };
}

Type infer_expr_type(Exprs* expr, Register* reg) {
    if (!expr) return (Type){ .tag = Type_Void };

    switch (expr->tag) {
        case Expr_Literals: return infer_literal_type(expr->data.literals.range);

        case Expr_Identifiers:
        case Expr_Vars: {
            SourceRange r = (expr->tag == Expr_Identifiers) ? expr->data.identifiers.name : expr->data.vars.name;
            RegisterEntry* entry = register_get(reg, string_range(r));
            if (entry) return entry->type;
            return (Type){ .tag = Type_Void };
        }

        case Expr_BinaryOps: {
            LexerTokenTag op = expr->data.binary_ops.op;
            if (op == DoubleEqualss   
                || op == NotEqualss     
                || op == Lesses          
                || op == Greaters 
                || op == LessEqualss     
                || op == GreaterEqualss  
                || op == Ands            
                || op == Ors)
                return (Type){ .tag = Type_Bool };
            return infer_expr_type(expr->data.binary_ops.left, reg);
        }

        case Expr_Function: {
            RegisterEntry* entry = register_get(reg, string_range(expr->data.function_call.name));
            if (entry && entry->tag == Reg_Function) return entry->data.function.return_type;
            return (Type){ .tag = Type_Void };
        }

        case Expr_MethodCalls: {
            return (Type){ .tag = Type_Void };
        }

        case Expr_Class_Calls:
        case Expr_Struct_Calls: {
            SourceRange r = (expr->tag == Expr_Class_Calls)
                ? expr->data.class_calls.name
                : expr->data.struct_calls.name;
            return (Type){ .tag = Type_Custom, .data.custom.name = r };
        }

        default:
            return (Type){ .tag = Type_Void };
    }
}

EntityID register_call(Register* reg, StringView name, RegisterEntryTag kind) {
    int absent = 0;

    RegisterEntry* entry = register_get(reg, name);
    if (entry) return entry->eid;

    khint_t pit = pending_table_get(reg->pending, name);
    if (pit != kh_end(reg->pending)) return kh_val(reg->pending, pit);

    EntityID eid = (EntityID){ .id = reg->counter->next_id++, .kind = kind };
    khint_t it = pending_table_put(reg->pending, name, &absent);

    kh_val(reg->pending, it) = eid;
    return eid;
}


bool register_class(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    StringView key = string_range(stmt->data.classes.name);
    RegisterEntry* existing = register_get(reg, key);

    if (existing) {
        bool allowed = existing->tag == Reg_Struct || existing->tag == Reg_Enum;
        if (!allowed) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_RDL,
                .data.rdl = {
                    .range = stmt->data.classes.range,
                    .var_name = sv(existing->name),
                }
            });
            return true;
        }

        stmt->data.classes.attached_tag = existing->tag == Reg_Struct ? ClassAttach_Struct : ClassAttach_Enum;
        stmt->data.classes.attached_fields = existing->data.strct.fields;
        stmt->data.classes.attached_fields_count = existing->data.strct.fields_count;
    }

    register_insert(reg, key, (RegisterEntry){
        .tag = Reg_Class,
        .name = NULL,
        .type = (Type){ .tag = Type_Custom, .data.custom.name = stmt->data.classes.name },
        .data.class = {
            .methods = stmt->data.classes.methods,
            .methods_count = stmt->data.classes.methods_count,
            .attached_tag = stmt->data.classes.attached_tag,
            .attached_fields = stmt->data.classes.attached_fields,
            .attached_fields_count = stmt->data.classes.attached_fields_count,
        }
    });

    return true;
}

bool register_var(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    StringView key = string_range(stmt->data.vars.name);
    RegisterEntry* existing = register_get(reg, key);

    if (existing) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_RDL,
            .data.rdl = { 
                .range = stmt->data.vars.range, 
                .var_name = sv(existing->name) 
            }
        });
        return false;
    }

    Type t;
    if (stmt->data.vars.c_type.start != stmt->data.vars.c_type.end) {
        t = resolve_type(stmt->data.vars.c_type, reg);
    } else {
        t = infer_expr_type(&stmt->data.vars.value, reg);
    }

    if (expr_exists(stmt->data.vars.value) && stmt->data.vars.c_type.start != stmt->data.vars.c_type.end) {
        Type vt = infer_expr_type(&stmt->data.vars.value, reg);
        if (t.tag != vt.tag) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_VMV,
                .data.vmv = { 
                    .range = stmt->data.vars.range, 
                    .var_name = key, 
                    .expected_type = type_tag_to_view(t), 
                    .actual_type = type_tag_to_view(vt) 
                }
            });
            return false;
        }
    }

      register_insert(reg, key, (RegisterEntry){
          .tag = Reg_Var,
          .name = NULL,
          .type = t,
          .decl_range = stmt->data.vars.range,
          .decl_name_range = stmt->data.vars.name,
          .data.var = { .type = t, .mode = stmt->data.vars.mode, .is_mut = true }
      });
      return true;
  }

bool register_let(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    StringView key = string_range(stmt->data.lets.name);
    RegisterEntry* existing = register_get(reg, key);

    if (existing) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_RDL,
            .data.rdl = { 
                .range = stmt->data.lets.range, 
                .var_name = sv(existing->name) 
            }
        });
        return false;
    }

    Type t;
    if (stmt->data.lets.c_type.start != stmt->data.lets.c_type.end) {
        t = resolve_type(stmt->data.lets.c_type, reg);
    } else {
        t = infer_expr_type(&stmt->data.lets.value, reg);
    }

    if (expr_exists(stmt->data.lets.value) && stmt->data.lets.c_type.start != stmt->data.lets.c_type.end) {
        Type vt = infer_expr_type(&stmt->data.lets.value, reg);
        if (t.tag != vt.tag) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_VMV,
                .data.vmv = { 
                    .range = stmt->data.lets.range, 
                    .var_name = key, 
                    .expected_type = type_tag_to_view(t), 
                    .actual_type = type_tag_to_view(vt) 
                }
            });
            return false;
        }
    }

      register_insert(reg, key, (RegisterEntry){
          .tag = Reg_Let,
          .name = NULL,
          .type = t,
          .decl_range = stmt->data.lets.range,
          .decl_name_range = stmt->data.lets.name,
          .data.let = { .type = t, .mode = stmt->data.lets.mode }
      });
      return true;
  }

bool register_const(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    StringView key = string_range(stmt->data.consts.name);
    RegisterEntry* existing = register_get(reg, key);

    if (!expr_exists(stmt->data.consts.value)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_CVN,
            .data.cvn = { 
                .range = stmt->data.consts.range, 
                .var_name = key 
            }
        });
        return false;
    }

    if (existing) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_RDL,
            .data.rdl = { 
                .range = stmt->data.consts.range, 
                .var_name = sv(existing->name) 
            }
        });
        return false;
    }

    Type t = (stmt->data.consts.c_type.start != stmt->data.consts.c_type.end) 
        ? resolve_type(stmt->data.consts.c_type, reg) 
        : infer_expr_type(&stmt->data.consts.value, reg);

      register_insert(reg, key, (RegisterEntry){
          .tag = Reg_Const,
          .name = NULL,
          .type = t,
          .decl_range = stmt->data.consts.range,
          .decl_name_range = stmt->data.consts.name,
          .data.const_ = { .type = t, .is_pub = stmt->data.consts.is_pub }
      });
      return true;
  }

bool register_local(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    StringView key = string_range(stmt->data.locals.name);
    RegisterEntry* existing = register_get(reg, key);

    if (existing) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_RDL,
            .data.rdl = {
                .range = stmt->data.locals.range,
                .var_name = sv(existing->name),
            }
        });
        return false;
    }

    Type t = resolve_type(stmt->data.locals.c_type, reg);
    if (t.tag == Type_Void) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_TNF,
            .data.tnf = {
                .range = stmt->data.locals.range,
                .type_name = key,
            }
        });
        return false;
    }

      register_insert(reg, key, (RegisterEntry){
          .tag = Reg_Local,
          .name = NULL,
          .type = t,
          .decl_range = stmt->data.locals.range,
          .decl_name_range = stmt->data.locals.name,
          .data.local = { .type = t, .is_pub = stmt->data.locals.is_pub }
      });
      return true;
  }

bool register_expr(Exprs* expr, Register* reg, CheckerErrList* errors) {
    if (!expr) return true;

    switch (expr->tag) {
        case Expr_Literals:
        case Expr_Vars:
        case Expr_Identifiers:
            return true;

        case Expr_BinaryOps:
            register_expr(expr->data.binary_ops.left,  reg, errors);
            register_expr(expr->data.binary_ops.right, reg, errors);
            return true;

        case Expr_MethodCalls: {
            register_expr(expr->data.method_calls.object, reg, errors);
            for (size_t i = 0; i < expr->data.method_calls.args_count; i++) {
                register_expr(&expr->data.method_calls.args[i], reg, errors);
            }
            return true;
        }

        case Expr_Function: {
            StringView name = string_range(expr->data.function_call.name);
            register_call(reg, name, Reg_Function);
            for (size_t i = 0; i < expr->data.function_call.param_count; i++) {
                StringView arg = string_range(expr->data.function_call.param[i].name);
                register_call(reg, arg, Reg_Var);
            }
            return true;
        }

        case Expr_Class_Calls: {
            StringView name = string_range(expr->data.class_calls.name);
            register_call(reg, name, Reg_Class);
            for (size_t i = 0; i < expr->data.class_calls.param_count; i++) {
                StringView arg = string_range(expr->data.class_calls.param[i].name);
                register_call(reg, arg, Reg_Var);
            }
            return true;
        }

        case Expr_Struct_Calls: {
            StringView name = string_range(expr->data.struct_calls.name);
            register_call(reg, name, Reg_Struct);
            for (size_t i = 0; i < expr->data.struct_calls.param_count; i++) {
                StringView arg = string_range(expr->data.struct_calls.param[i].name);
                register_call(reg, arg, Reg_Var);
            }
            return true;
        }

        case Expr_Enum_Calls: {
            StringView name = string_range(expr->data.enum_calls.name);
            register_call(reg, name, Reg_Enum);
            for (size_t i = 0; i < expr->data.enum_calls.param_count; i++) {
                StringView arg = string_range(expr->data.enum_calls.param[i].name);
                register_call(reg, arg, Reg_Var);
            }
            return true;
        }

        default: return true;
    }
}

static void check_body(Stmts* body, size_t count, Register* reg, CheckerErrList* errors, SourceRange fn_return_type, Exprs* parent_cond) {
    populate_register(body, count, reg, errors);

    for (size_t i = 0; i < count; i++) {
        check_stmt(&body[i], reg, errors, fn_return_type, parent_cond);
    }
}

static void check_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors, SourceRange fn_return_type, Exprs* parent_cond) {
    if (!stmt) return;

    switch (stmt->tag) {
        case Stmt_Vars:    check_vars_stmt(stmt, reg, errors);   break;
        case Stmt_Lets:    check_lets_stmt(stmt, reg, errors);   break;
        case Stmt_Consts:  check_consts_stmt(stmt, reg, errors); break;
        case Stmt_Locals:  check_locals_stmt(stmt, reg, errors); break;
        case Stmt_Assigns: check_assign_stmt(stmt, reg, errors); break;
        case Stmt_Returns: check_return_stmt(stmt, reg, errors, fn_return_type); break;
        case Stmt_ExprStmt: check_expr(&stmt->data.expr_stmt.expr, reg, errors); break;

        case Stmt_Ifs: {
            check_if_stmt(stmt, reg, errors, parent_cond);

            Register child = register_new(reg, reg->counter);
            check_body(stmt->data.ifs.body, stmt->data.ifs.body_count, &child, errors, fn_return_type, &stmt->data.ifs.cond);
            register_free(&child);

            if (stmt->data.ifs.else_body_count > 0) {
                Register else_child = register_new(reg, reg->counter);
                check_body(stmt->data.ifs.else_body, stmt->data.ifs.else_body_count, &else_child, errors, fn_return_type, parent_cond);
                register_free(&else_child);
            }
            break;
        }

        case Stmt_Whiles: {
            check_while_stmt(stmt, reg, errors, parent_cond);

            Register child = register_new(reg, reg->counter);

            check_body(stmt->data.whiles.body, stmt->data.whiles.body_count, &child, errors, fn_return_type, &stmt->data.whiles.cond);
            register_free(&child);
            break;
        }

        case Stmt_Fors: {
            check_for_stmt(stmt, reg, errors);

            Register child = register_new(reg, reg->counter);
            StringView var_sv = string_range(stmt->data.fors._var);
            Type iter_type = infer_expr_type(&stmt->data.fors.iter, reg);

            register_insert(&child, var_sv, (RegisterEntry){
                .tag = Reg_Var,
                .name = NULL,
                .type = iter_type,
                .data.var = { .type = iter_type, .is_mut = false }
            });

            check_body(stmt->data.fors.body, stmt->data.fors.body_count,
                       &child, errors, fn_return_type, NULL);
            register_free(&child);
            break;
        }

        case Stmt_Matchs: check_match_stmt(stmt, reg, errors); break;
        case Stmt_Externs: {
            ExternBlock* block = &stmt->data.externs.block;
            SourceRange ffi = stmt->data.externs.ffi;

            register_insert(reg, string_range(block->abi), (RegisterEntry){
                .tag = Reg_Extern,
                .name = NULL,
                .data.extern_ = {
                    .abi = block->abi,
                    .ffi = ffi,
                    .funcs = block->funcs,
                    .funcs_count = block->funcs_count,
                    .is_pub = true,
                }
            });

            for (size_t i = 0; i < block->funcs_count; i++) {
                ExternFunction* fn = &block->funcs[i];
                Type ret = resolve_type(fn->return_type, reg);
                register_insert(reg, string_range(fn->name), (RegisterEntry){
                    .tag = Reg_Function,
                    .name = NULL,
                    .type = ret,
                    .data.function = {
                        .return_type = ret,
                        .params = fn->params,
                        .params_count = fn->params_count,
                        .is_pub = true,
                    }
                });
            }

            check_extern_stmt(stmt, reg, errors);
            break;
        }
        case Stmt_Functions: {
            check_function_stmt(stmt, reg, errors);

            Register child = register_new(reg, reg->counter);

            for (size_t i = 0; i < stmt->data.functions.params_count; i++) {
                Param* p = &stmt->data.functions.params[i];
                Type t = resolve_type(p->c_type, reg);
                register_insert(&child, string_range(p->name), (RegisterEntry){
                    .tag = Reg_Var,
                    .name = NULL,
                    .type = t,
                    .data.var = { .type = t, .is_mut = false }
                });
            }
            check_body(stmt->data.functions.body, stmt->data.functions.body_count, &child, errors, stmt->data.functions.return_type, NULL);
            register_free(&child);
            break;
        }

        case Stmt_Structs: check_struct_stmt(stmt, reg, errors); break;
        case Stmt_Enums:   check_enum_stmt(stmt, reg, errors);   break;
        case Stmt_Classes: check_class_stmt(stmt, reg, errors);  break;
        case Stmt_Traits:  check_trait_stmt(stmt, reg, errors);  break;

        default: break;
    }
}

bool register_body(Stmts* body, size_t count, Register* reg, CheckerErrList* errors) {
    for (size_t i = 0; i < count; i++) {
        Stmts* s = &body[i];
        switch (s->tag) {
            case Stmt_Functions: {
                StringView key = string_range(s->data.functions.name);
                Type ret = resolve_type(s->data.functions.return_type, reg);
                register_insert(reg, key, (RegisterEntry){
                    .tag = Reg_Function,
                    .name = NULL,
                    .type = ret,
                    .data.function = {
                        .return_type = ret,
                        .params = s->data.functions.params,
                        .params_count = s->data.functions.params_count,
                        .is_pub = s->data.functions.is_pub,
                    }
                });
                break;
            }
            case Stmt_Structs: {
                StringView key = string_range(s->data.structs.name);
                register_insert(reg, key, (RegisterEntry){
                    .tag = Reg_Struct,
                    .name = NULL,
                    .type = (Type){ .tag = Type_Custom, .data.custom.name = s->data.structs.name },
                    .data.strct = {
                        .fields = s->data.structs.fields,
                        .fields_count = s->data.structs.fields_count,
                        .is_pub = s->data.structs.is_pub,
                    }
                });
                break;
            }
            case Stmt_Enums: {
                StringView key = string_range(s->data.enums.name);
                register_insert(reg, key, (RegisterEntry){
                    .tag = Reg_Enum,
                    .name = NULL,
                    .type = (Type){ .tag = Type_Custom, .data.custom.name = s->data.enums.name },
                    .data.enm = {
                        .variants = s->data.enums.variants,
                        .variants_count = s->data.enums.variants_count,
                        .is_pub = s->data.enums.is_pub,
                    }
                });
                break;
            }
            case Stmt_Traits: {
                StringView key = string_range(s->data.traits.name);
                register_insert(reg, key, (RegisterEntry){
                    .tag = Reg_Trait,
                    .name = NULL,
                    .type = (Type){ .tag = Type_Custom, .data.custom.name = s->data.traits.name },
                    .data.trait = {
                        .methods = s->data.traits.methods,
                        .methods_count = s->data.traits.methods_count,
                    }
                });
                break;
            }
            
            case Stmt_Externs: {
                ExternBlock* block = &s->data.externs.block;
                SourceRange ffi = s->data.externs.ffi;

                register_insert(reg, string_range(block->abi), (RegisterEntry){
                    .tag = Reg_Extern,
                    .name = NULL,
                    .data.extern_ = {
                        .abi = block->abi,
                        .ffi = ffi,
                        .funcs = block->funcs,
                        .funcs_count = block->funcs_count,
                        .is_pub = true,
                    }
                });

                for (size_t i = 0; i < block->funcs_count; i++) {
                    ExternFunction* fn = &block->funcs[i];
                    Type ret = resolve_type(fn->return_type, reg);

                    register_insert(reg, string_range(fn->name), (RegisterEntry){
                        .tag = Reg_Function,
                        .name = NULL,
                        .type = ret,
                        .data.function = {
                            .return_type = ret,
                            .params = fn->params,
                            .params_count = fn->params_count,
                            .is_pub = true,
                        }
                    });
                }
                break;
            }
    
            case Stmt_Classes:
                register_class(s, reg, errors);
                break;
            default: break;
        }
    }
    

    SourceRange empty = {0};
    check_body(body, count, reg, errors, empty, NULL);
    return true;
}

void check_expr(Exprs* expr, Register* reg, CheckerErrList* errors) {
    if (!expr) return;

    switch (expr->tag) {
        case Expr_Function: {
            StringView name = string_range(expr->data.function_call.name);
            RegisterEntry* entry = register_get(reg, name);
            if (!entry) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_VSF,
                    .data.vsf = { .range = expr->data.function_call.name, .var_name = name }
                });
            }
            return;
        }


        case Expr_MethodCalls: {
            check_expr(expr->data.method_calls.object, reg, errors);
            for (size_t i = 0; i < expr->data.method_calls.args_count; i++)
                check_expr(&expr->data.method_calls.args[i], reg, errors);
            return;
        }

        case Expr_BinaryOps: check_expr(expr->data.binary_ops.left,  reg, errors); check_expr(expr->data.binary_ops.right, reg, errors); return;

        case Expr_Identifiers: {
            StringView name = string_range(expr->data.identifiers.name);
            RegisterEntry* entry = register_get(reg, name);
            if (!entry) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_VSF,
                    .data.vsf = { .range = expr->data.identifiers.name, .var_name = name }
                });
            }
            return;
        }

        case Expr_Vars: {
            StringView name = string_range(expr->data.vars.name);
            RegisterEntry* entry = register_get(reg, name);
            if (!entry) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_VSF,
                    .data.vsf = { .range = expr->data.vars.name, .var_name = name }
                });
            }
            return;
        }

        case Expr_Literals:
        default:
            return;
    }
}
