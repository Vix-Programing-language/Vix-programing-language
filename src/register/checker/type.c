#include "import.h"
#include "register.h"
#include "type.h"

Type infer_expr_type(Exprs* expr, Register* reg);
Type resolve_type(SourceRange r, Register* reg);
RegisterEntry* register_get(Register* reg, StringView key);
void register_insert(Register* reg, StringView key, RegisterEntry entry);
bool register_class(Stmts* stmt, Register* reg, CheckerErrList* errors);

static inline StringView string_view_from_range(SourceRange range) {
    return (StringView){
        .ptr = range.start,
        .len = (size_t)(range.end - range.start),
    };
}

static inline const char* op_tag_to_str(LexerTokenTag tag) {
    switch (tag) {
        case Plus:              return "+";
        case Minuss:            return "-";
        case Stars:             return "*";
        case Slashs:            return "/";
        case Percents:          return "%";
        case Lesses:            return "<";
        case Greaters:          return ">";
        case Equalss:           return "=";
        case Ampersands:        return "&";
        case Pipes:             return "|";
        case Carets:            return "^";
        case Tildes:            return "~";
        case Bangs:             return "!";
        case PlusEqualss:       return "+=";
        case MinusEqualss:      return "-=";
        case StarEqualss:       return "*=";
        case SlashEqualss:      return "/=";
        case PercentEqualss:    return "%=";
        case PipeEqualss:       return "|=";
        case AmpersandEqualss:  return "&=";
        case CaretEqualss:      return "^=";
        case LeftShiftEqualss:  return "<<=";
        case RightShiftEqualss: return ">>=";
        case LeftShifts:        return "<<";
        case RightShifts:       return ">>";
        case LessEqualss:       return "<=";
        case GreaterEqualss:    return ">=";
        case NotEqualss:        return "!=";
        case DoubleEqualss:     return "==";
        case Ands:              return "&&";
        case Ors:               return "||";
        case Nots:              return "!";
        default:                return "?";
    }
}

char* type_tag_to_str(Type t) {
    switch (t.tag) {
        case Type_Int:   return t.data.int_t.bits == 64 ? "i64" : "i32";
        case Type_Float: return t.data.float_t.bits == 64 ? "f64" : "f32";
        case Type_Bool:  return "bool";
        case Type_Char:  return "char";
        case Type_Str:   return "str";
        case Type_Void:  return "void";
        case Type_Custom: {
            size_t len = t.data.custom.name.end - t.data.custom.name.start;
            char*  buf = malloc(len + 1);

            memcpy(buf, t.data.custom.name.start, len);
            buf[len] = '\0';
            return buf;
        }

        default: return "unknown";
    }
}


void resolve_operations(Exprs* operations, Register* reg, CheckerErrList* errors) {
    Exprs* lhs = operations->data.binary_ops.left;
    Exprs* rhs = operations->data.binary_ops.right;
    LexerTokenTag op_tag = operations->data.binary_ops.op;
    SourceRange name_r;

    if (lhs->tag == Expr_Identifiers) name_r = lhs->data.identifiers.name;
    else if (lhs->tag == Expr_Vars) name_r = lhs->data.vars.name;
    else {
        checker_err_push(errors, (CheckerErr){
            .tag      = Err_Tag_LHS,
            .data.lhs = {
                .range = operations->data.binary_ops.range,
            }
        });
        return;
    }

    StringView name_sv = string_view_from_range(name_r);
    RegisterEntry* entry = register_get(reg, name_sv);

    if (!entry) {
        char* name = strndup(name_r.start, name_r.end - name_r.start);
        checker_err_push(errors, (CheckerErr){
            .tag      = Err_Tag_VSF,
            .data.vsf = {
                .range    = name_r,
                .var_name = name,
            }
        });
        return;
    }

    if (entry->tag != Reg_Var) {
        checker_err_push(errors, (CheckerErr){
            .tag      = Err_Tag_VNM,
            .data.vnm = {
                .range = name_r,
                .var_name     = entry->name,
                .binding_kind = entry->tag == Reg_Let   ? "let"   : entry->tag == Reg_Const ? "const" : "local",
            }
        });
        return;
    }

    if (entry->type.tag != Type_Custom) {
        checker_err_push(errors, (CheckerErr){
            .tag      = Err_Tag_VPT,
            .data.vpt = {
                .range = name_r,
                .var_name = entry->name,
                .type_name = type_tag_to_str(entry->type),
            }
        });
        return;
    }

    SourceRange class_name_r = entry->type.data.custom.name;
    char* class_name = strndup(class_name_r.start, class_name_r.end - class_name_r.start);
    RegisterEntry* class_entry = register_get(reg, string_view_from_range(class_name_r));

    if (!class_entry) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_TNF,
            .data.tnf = {
                .range = class_name_r,
                .type_name = class_name,
            }
        });
        free(class_name);
        return;
    }

    if (class_entry->tag == Reg_Struct || class_entry->tag == Reg_Enum) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_TNC,
            .data.tnc = {
                .range = class_name_r,
                .type_name = class_name,
                .actual_kind = class_entry->tag == Reg_Struct ? "struct" : "enum",
            }
        });
        free(class_name);
        return;
    }

    if (class_entry->tag != Reg_Class) {
        checker_err_push(errors, (CheckerErr){
            .tag      = Err_Tag_TNC,
            .data.tnc = {
                .range = class_name_r,
                .type_name = class_name,
                .actual_kind = "unknown",
            }
        });
        free(class_name);
        return;
    }

    FunctionMethod* matched = NULL;
    for (size_t i = 0; i < class_entry->data.class.methods_count; i++) {
        FunctionMethod* m = &class_entry->data.class.methods[i];
        if (m->has_operation && m->operation.op == op_tag) {
            matched = m;
            break;
        }
    }

    if (!matched) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_OUD,
            .data.oud = {
                .range = class_name_r,
                .class_name = class_name,
                .op = op_tag_to_str(op_tag),
            }
        });
        free(class_name);
        return;
    }

    if (matched->params_count == 0) {
        char* method_name = strndup(matched->name.start, matched->name.end - matched->name.start);
        checker_err_push(errors, (CheckerErr){
            .tag      = Err_Tag_OMP,
            .data.omp = {
                .range       = matched->range,
                .class_name  = class_name,
                .method_name = method_name,
                .op          = op_tag_to_str(op_tag),
            }
        });
        free(class_name);
        free(method_name);
        return;
    }

    Type rhs_type      = infer_expr_type(rhs, reg);
    Type expected_type = resolve_type(matched->params[0].c_type, reg);

    if (rhs_type.tag != expected_type.tag) {
        char* method_name = strndup(matched->name.start, matched->name.end - matched->name.start);
        checker_err_push(errors, (CheckerErr){
            .tag      = Err_Tag_OMM,
            .data.omm = {
                .range         = matched->range,
                .class_name    = class_name,
                .method_name   = method_name,
                .op            = op_tag_to_str(op_tag),
                .expected_type = type_tag_to_str(expected_type),
                .actual_type   = type_tag_to_str(rhs_type),
            }
        });
        free(class_name);
        free(method_name);
        return;
    }

    free(class_name);
}

void resolve_generic_call(Exprs* call, Register* reg, GenericRegistry* greg, CheckerErrList* errors) {
    SourceRange func_name    = call->data.function_call.name;
    StringView   func_name_sv = {
        .ptr = func_name.start,
        .len = (size_t)(func_name.end - func_name.start),
    };
    size_t      params_count = call->data.function_call.param_count;
    GenericArg* args         = malloc(params_count * sizeof(GenericArg));

    for (size_t i = 0; i < params_count; i++) {
        SourceRange param_name = call->data.function_call.param[i].name;
        RegisterEntry* entry = register_get(reg, string_view_from_range(param_name));

        if (!entry) {
            char* key = strndup(param_name.start, param_name.end - param_name.start);
            checker_err_push(errors, (CheckerErr){
                .tag      = Err_Tag_VSF,
                .data.vsf = {
                    .range    = param_name,
                    .var_name = key,
                }
            });
            free(args);
            return;
        }

        args[i] = (GenericArg){
            .type_name = strdup(type_tag_to_str(entry->type)),
            .type      = entry->type
        };
    }

    khint_t existing_it = generic_instance_table_get(greg->table, func_name_sv);
    bool has_matching_instance = existing_it != kh_end(greg->table)
        && kh_val(greg->table, existing_it).args_count > 0
        && kh_val(greg->table, existing_it).args[0].type.tag == args[0].type.tag;

    if (has_matching_instance) {
        free(args);
        return;
    }

    GenericInstance inst = {
        .func_name    = func_name_sv,
        .args         = args,
        .args_count   = params_count,
        .return_type  = args[0].type,
        .params       = NULL,
        .params_count = 0,
    };

    int absent = 0;
    khint_t it = generic_instance_table_put(greg->table, func_name_sv, &absent);
    kh_val(greg->table, it) = inst;

}

void populate_register(Stmts* body, size_t body_count, Register* reg, CheckerErrList* errors) {
    for (size_t i = 0; i < body_count; i++) {
        Stmts* stmt = &body[i];

        switch (stmt->tag) {
            case Stmt_Vars: {
                StringView key = string_view_from_range(stmt->data.vars.name);

                RegisterEntry* existing = register_get(reg, key);
                if (existing) {
                    checker_err_push(errors, (CheckerErr){
                        .tag      = Err_Tag_RDL,
                        .data.rdl = {
                            .range               = stmt->data.vars.range,
                            .var_name            = existing->name,
                        }
                    });
                    break;
                }

                Type t = (stmt->data.vars.c_type.start != stmt->data.vars.c_type.end)
                    ? resolve_type(stmt->data.vars.c_type, reg)
                    : infer_expr_type(&stmt->data.vars.value, reg);

                if (stmt->data.vars.has_value &&
                    stmt->data.vars.c_type.start != stmt->data.vars.c_type.end) {
                    Type value_type = infer_expr_type(&stmt->data.vars.value, reg);
                    if (t.tag != value_type.tag) {
                        char* name = strndup(stmt->data.vars.name.start, stmt->data.vars.name.end - stmt->data.vars.name.start);
                        checker_err_push(errors, (CheckerErr){
                            .tag      = Err_Tag_VMV,
                            .data.vmv = {
                                .range         = stmt->data.vars.range,
                                .var_name      = name,
                                .expected_type = type_tag_to_str(t),
                                .actual_type   = type_tag_to_str(value_type),
                            }
                        });
                        break;
                    }
                }

                register_insert(reg, key, (RegisterEntry){
                    .tag      = Reg_Var,
                    .name     = NULL,
                    .type     = t,
                    .data.var = { .type = t, .mode = stmt->data.vars.mode, .is_mut = true }
                });
                break;
            }

            case Stmt_Lets: {
                StringView key = string_view_from_range(stmt->data.lets.name);

                RegisterEntry* existing = register_get(reg, key);
                if (existing) {
                    checker_err_push(errors, (CheckerErr){
                        .tag      = Err_Tag_RDL,
                        .data.rdl = {
                            .range               = stmt->data.lets.range,
                            .var_name            = existing->name,
                        }
                    });
                    break;
                }

                Type t = (stmt->data.lets.c_type.start != stmt->data.lets.c_type.end)
                    ? resolve_type(stmt->data.lets.c_type, reg)
                    : infer_expr_type(&stmt->data.lets.value, reg);

                if (stmt->data.lets.has_value &&
                    stmt->data.lets.c_type.start != stmt->data.lets.c_type.end) {
                    Type value_type = infer_expr_type(&stmt->data.lets.value, reg);
                    if (t.tag != value_type.tag) {
                        char* name = strndup(stmt->data.lets.name.start, stmt->data.lets.name.end - stmt->data.lets.name.start);
                        checker_err_push(errors, (CheckerErr){
                            .tag      = Err_Tag_VMV,
                            .data.vmv = {
                                .range         = stmt->data.lets.range,
                                .var_name      = name,
                                .expected_type = type_tag_to_str(t),
                                .actual_type   = type_tag_to_str(value_type),
                            }
                        });
                        break;
                    }
                }

                register_insert(reg, key, (RegisterEntry){
                    .tag      = Reg_Let,
                    .name     = NULL,
                    .type     = t,
                    .data.let = { .type = t, .mode = stmt->data.lets.mode }
                });
                break;
            }

            case Stmt_Consts: {
                StringView key = string_view_from_range(stmt->data.consts.name);

                if (!stmt->data.consts.has_value) {
                    char* name = strndup(stmt->data.consts.name.start, stmt->data.consts.name.end - stmt->data.consts.name.start);
                    checker_err_push(errors, (CheckerErr){
                        .tag      = Err_Tag_CVN,
                        .data.cvn = {
                            .range    = stmt->data.consts.range,
                            .var_name = name,
                        }
                    });
                    break;
                }

                RegisterEntry* existing = register_get(reg, key);
                if (existing) {
                    checker_err_push(errors, (CheckerErr){
                        .tag      = Err_Tag_RDL,
                        .data.rdl = {
                            .range               = stmt->data.consts.range,
                            .var_name            = existing->name,
                        }
                    });
                    break;
                }

                Type t = (stmt->data.consts.c_type.start != stmt->data.consts.c_type.end)
                    ? resolve_type(stmt->data.consts.c_type, reg)
                    : infer_expr_type(&stmt->data.consts.value, reg);

                register_insert(reg, key, (RegisterEntry){
                    .tag         = Reg_Const,
                    .name        = NULL,
                    .type        = t,
                    .data.const_ = { .type = t, .is_pub = stmt->data.consts.is_pub }
                });
                break;
            }

            case Stmt_Locals: {
                StringView key = string_view_from_range(stmt->data.locals.name);

                RegisterEntry* existing = register_get(reg, key);
                if (existing) {
                    checker_err_push(errors, (CheckerErr){
                        .tag      = Err_Tag_RDL,
                        .data.rdl = {
                            .range               = stmt->data.locals.range,
                            .var_name            = existing->name,
                        }
                    });
                    break;
                }

                Type t = resolve_type(stmt->data.locals.c_type, reg);
                if (t.tag == Type_Void) {
                    char* name = strndup(stmt->data.locals.name.start, stmt->data.locals.name.end - stmt->data.locals.name.start);
                    checker_err_push(errors, (CheckerErr){
                        .tag      = Err_Tag_TNF,
                        .data.tnf = {
                            .range     = stmt->data.locals.range,
                            .type_name = name,
                        }
                    });
                    break;
                }

                register_insert(reg, key, (RegisterEntry){
                    .tag        = Reg_Local,
                    .name       = NULL,
                    .type       = t,
                    .data.local = { .type = t, .is_pub = stmt->data.locals.is_pub }
                });
                break;
            }

            
            case Stmt_Classes: register_class(stmt, reg, errors); break;

            default: break;
        }
    }    
}

// Here what this all do:
// variables like:
// let a = 10 // convert to int32
// var i = 3.3 // convert to float
// const a = some_function() and that function return int32 so a is int32
// For check_generic() it will do:
// var a: int32 = 10 or even no "type" just 10 same thing. When do "some_function(a)" and function is Generic, will automaticlly convert the generic to the variable type
// about resolove_operation it do:
// var a = ExampleClass // if that class contains @operation("+=") func add()
// a += 10 // will allow it and will generate add
// This all just checker and register
