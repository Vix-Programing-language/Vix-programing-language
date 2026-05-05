#include "import.h"
#include "register.h"
#include "error.h"
#include "ast.h"
#include "ir.h"

StringView string_range(SourceRange range);
StringView string(const char* s);
Type infer_expr_type(Exprs* expr, Register* reg);
Type resolve_type(SourceRange r, Register* reg);
RegisterEntry* register_get(Register* reg, StringView key);
void register_insert(Register* reg, StringView key, RegisterEntry entry);
bool register_class(Stmts* stmt, Register* reg, CheckerErrList* errors);
Type infer_literal_type(SourceRange range);
bool range_eq(SourceRange r, const char* str);
void resolve_operations(Exprs* operations, Register* reg, CheckerErrList* errors);
const char* op_tag_to_str(LexerTokenTag tag);
StringView type_tag_to_view(Type t);
bool ranges_equal(SourceRange a, SourceRange b);
bool range_eq_sv(SourceRange r, StringView sv);
bool conds_equal(Exprs* a, Exprs* b);
bool conds_equal(Exprs* a, Exprs* b);
bool is_always(Exprs* cond);
bool is_conditionable(Type t);
bool is_builtin_type(SourceRange name);
bool is_valid_return_type(SourceRange r);
bool is_tautolog(Exprs* cond);
static void check_expr(Exprs* expr, Register* reg, CheckerErrList* errors);


void check_literal_expr(Exprs* expr, Register* reg, CheckerErrList* errors) {
    SourceRange range = expr->data.literals.range;
    size_t len = range.end - range.start;

    if (len == 0) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_CVN,
            .data.cvn = { .range = range, .var_name = string("") }
        });
        return;
    }

    if (range.start[0] == '"' && (len < 2 || range.start[len - 1] != '"')) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_CVN,
            .data.cvn = { .range = range, .var_name = string("<string literal>") }
        });
        return;
    }

    if (range.start[0] == '\'' && (len < 3 || range.start[len - 1] != '\'')) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_CVN,
            .data.cvn = { .range = range, .var_name = string("<char literal>") }
        });
        return;
    }

    if (range.start[0] == '\'' && len > 3) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_CVN,
            .data.cvn = { .range = range, .var_name = string("<char literal>") }
        });
    }
}


void check_identifier_expr(Exprs* expr, Register* reg, CheckerErrList* errors) {
    SourceRange range = expr->data.identifiers.name;
    StringView  name_sv = string_range(range);

    RegisterEntry* entry = register_get(reg, name_sv);
    if (!entry) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VSF,
            .data.vsf = { .range = range, .var_name = name_sv }
        });
    }
}


void check_var_expr(Exprs* expr, Register* reg, CheckerErrList* errors) {
    SourceRange range = expr->data.vars.name;
    StringView  name_sv = string_range(range);

    RegisterEntry* entry = register_get(reg, name_sv);
    if (!entry) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VSF,
            .data.vsf = { .range = range, .var_name = name_sv }
        });
    }
}


void check_binary_op_expr(Exprs* expr, Register* reg, CheckerErrList* errors) {
    Exprs* lhs = expr->data.binary_ops.left;
    Exprs* rhs = expr->data.binary_ops.right;
    LexerTokenTag op = expr->data.binary_ops.op;
    SourceRange range = expr->data.binary_ops.range;

    if (!lhs || !rhs) return;

    check_expr(lhs, reg, errors);
    check_expr(rhs, reg, errors);

    Type lhs_type = infer_expr_type(lhs, reg);
    Type rhs_type = infer_expr_type(rhs, reg);

    if (lhs_type.tag == Type_Void || rhs_type.tag == Type_Void) return;

    if (lhs_type.tag != rhs_type.tag) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VMV,
            .data.vmv = {
                .range = range,
                .var_name = string(op_tag_to_str(op)),
                .expected_type = type_tag_to_view(lhs_type),
                .actual_type = type_tag_to_view(rhs_type),
            }
        });
        return;
    }

    if ((op == Ands || op == Ors) && lhs_type.tag != Type_Bool) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_NBC,
            .data.nbc = { .range = range, .cond_type = type_tag_to_view(lhs_type) }
        });
        return;
    }

    if ((op == Plus || op == Minuss || op == Stars || op == Slashs || op == Percents) &&
        lhs_type.tag != Type_Int && lhs_type.tag != Type_Float) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VPT,
            .data.vpt = {
                .range = range,
                .var_name = string(op_tag_to_str(op)),
                .type_name = type_tag_to_view(lhs_type),
            }
        });
        return;
    }

    if ((op == Ampersands || op == Pipes || op == Carets ||
         op == LeftShifts || op == RightShifts) &&
        lhs_type.tag != Type_Int) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VPT,
            .data.vpt = {
                .range = range,
                .var_name = string(op_tag_to_str(op)),
                .type_name = type_tag_to_view(lhs_type),
            }
        });
        return;
    }

    if ((op == Slashs || op == Percents) &&
        rhs->tag == Expr_Literals) {
        SourceRange r = rhs->data.literals.range;
        size_t len = r.end - r.start;
        if (len == 1 && r.start[0] == '0') {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_DCB,
                .data.dcb = { .range = range, .is_always_true = false }
            });
        }
    }

    if (op == Equalss) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_AIC,
            .data.aic = { .range = range }
        });
    }
}


void check_function_call_expr(Exprs* expr, Register* reg, CheckerErrList* errors) {
    SourceRange func_name = expr->data.function_call.name;
    StringView func_sv = string_range(func_name);
    size_t call_count = expr->data.function_call.param_count;
    SourceRange range = expr->data.function_call.range;

    RegisterEntry* entry = register_get(reg, func_sv);

    if (!entry) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VSF,
            .data.vsf = { .range = func_name, .var_name = func_sv }
        });
        return;
    }

    if (entry->tag != Reg_Function) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_TNC,
            .data.tnc = {
                .range = func_name,
                .type_name = func_sv,
                .actual_kind = string(
                    entry->tag == Reg_Struct ? "struct" :
                    entry->tag == Reg_Class  ? "class"  :
                    entry->tag == Reg_Enum   ? "enum"   : "variable"
                ),
            }
        });
        return;
    }

    size_t decl_count = entry->data.function.params_count;

    if (call_count != decl_count) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_WFC,
            .data.wfc = {
                .range = range,
                .variant_name = func_sv,
                .expected_count = decl_count,
                .actual_count = call_count,
            }
        });
        return;
    }

    Param* params = entry->data.function.params;
    for (size_t i = 0; i < call_count; i++) {
        Exprs arg_expr = {
            .tag = Expr_Identifiers,
            .data.identifiers = { .name = expr->data.function_call.param[i].name }
        };

        Type arg_type = infer_expr_type(&arg_expr, reg);
        Type expected_type = resolve_type(params[i].c_type, reg);

        if (arg_type.tag == Type_Void || expected_type.tag == Type_Void) continue;

        if (arg_type.tag != expected_type.tag) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_VMV,
                .data.vmv = {
                    .range = expr->data.function_call.param[i].name,
                    .var_name = string_range(expr->data.function_call.param[i].name),
                    .expected_type = type_tag_to_view(expected_type),
                    .actual_type = type_tag_to_view(arg_type),
                }
            });
        }
    }
}

void check_method_call_expr(Exprs* expr, Register* reg, CheckerErrList* errors) {
    Exprs* object = expr->data.method_calls.object;
    SourceRange method_name = expr->data.method_calls.method;
    StringView method_sv = string_range(method_name);
    size_t args_count = expr->data.method_calls.args_count;
    SourceRange range = expr->data.method_calls.range;

    if (!object) return;

    check_expr(object, reg, errors);

    Type obj_type = infer_expr_type(object, reg);

    if (obj_type.tag == Type_Void) return;

    if (obj_type.tag != Type_Custom) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VPT,
            .data.vpt = {
                .range = range,
                .var_name = method_sv,
                .type_name = type_tag_to_view(obj_type),
            }
        });
        return;
    }

    StringView class_sv = string_range(obj_type.data.custom.name);
    RegisterEntry* class_entry = register_get(reg, class_sv);

    if (!class_entry) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_TNF,
            .data.tnf = {
                .range = obj_type.data.custom.name,
                .type_name = class_sv,
            }
        });
        return;
    }

    if (class_entry->tag != Reg_Class) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_TNC,
            .data.tnc = {
                .range = obj_type.data.custom.name,
                .type_name = class_sv,
                .actual_kind = string(
                    class_entry->tag == Reg_Struct ? "struct" :
                    class_entry->tag == Reg_Enum   ? "enum"   : "unknown"
                ),
            }
        });
        return;
    }

    FunctionMethod* matched = NULL;
    for (size_t i = 0; i < class_entry->data.class.methods_count; i++) {
        FunctionMethod* m = &class_entry->data.class.methods[i];
        size_t mlen = m->name.end - m->name.start;
        if (mlen == method_sv.len && memcmp(m->name.start, method_sv.ptr, mlen) == 0) {
            matched = m;
            break;
        }
    }

    if (!matched) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VSF,
            .data.vsf = { .range = method_name, .var_name = method_sv }
        });
        return;
    }

    if (args_count != matched->params_count) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_WFC,
            .data.wfc = {
                .range = range,
                .variant_name = method_sv,
                .expected_count = matched->params_count,
                .actual_count = args_count,
            }
        });
        return;
    }

    for (size_t i = 0; i < args_count; i++) {
        Type arg_type = infer_expr_type(&expr->data.method_calls.args[i], reg);
        Type expected_type = resolve_type(matched->params[i].c_type, reg);

        if (arg_type.tag == Type_Void || expected_type.tag == Type_Void) continue;

        if (arg_type.tag != expected_type.tag) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_VMV,
                .data.vmv = {
                    .range = range,
                    .var_name = method_sv,
                    .expected_type = type_tag_to_view(expected_type),
                    .actual_type = type_tag_to_view(arg_type),
                }
            });
        }
    }
}

void check_struct_call_expr(Exprs* expr, Register* reg, CheckerErrList* errors) {
    SourceRange name = expr->data.struct_calls.name;
    StringView name_sv = string_range(name);
    size_t arg_count = expr->data.struct_calls.param_count;
    SourceRange range = expr->data.struct_calls.range;

    RegisterEntry* entry = register_get(reg, name_sv);

    if (!entry) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VSF,
            .data.vsf = { .range = name, .var_name = name_sv }
        });
        return;
    }

    if (entry->tag != Reg_Struct) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_TNC,
            .data.tnc = {
                .range = name,
                .type_name = name_sv,
                .actual_kind = string(
                    entry->tag == Reg_Class ? "class" :
                    entry->tag == Reg_Enum  ? "enum"  : "unknown"
                ),
            }
        });
        return;
    }

    size_t fields_count = entry->data.strct.fields_count;

    if (arg_count != fields_count) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_WFC,
            .data.wfc = {
                .range = range,
                .variant_name = name_sv,
                .expected_count = fields_count,
                .actual_count = arg_count,
            }
        });
        return;
    }

    for (size_t i = 0; i < arg_count; i++) {
        SourceRange field_range = expr->data.struct_calls.param[i].name;
        StringView field_sv = string_range(field_range);

        bool found = false;
        StructParam* matched_field = NULL;
        for (size_t j = 0; j < fields_count; j++) {
            if (range_eq_sv(entry->data.strct.fields[j].name, field_sv)) {
                found = true;
                matched_field = &entry->data.strct.fields[j];
                break;
            }
        }

        if (!found) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_SFF,
                .data.sff = {
                    .range = field_range,
                    .field_name = field_sv,
                    .type_name = name_sv,
                }
            });
            continue;
        }

        Exprs arg_expr = { .tag = Expr_Identifiers, .data.identifiers = { .name = expr->data.struct_calls.param[i].name }};
        Type val_type = infer_expr_type(&arg_expr, reg);
        Type expected_type = resolve_type(matched_field->c_type, reg);

        if (val_type.tag == Type_Void || expected_type.tag == Type_Void) continue;

        if (val_type.tag != expected_type.tag) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_VMV,
                .data.vmv = {
                    .range = field_range,
                    .var_name = field_sv,
                    .expected_type = type_tag_to_view(expected_type),
                    .actual_type = type_tag_to_view(val_type),
                }
            });
        }
    }
}

void check_class_call_expr(Exprs* expr, Register* reg, CheckerErrList* errors) {
    SourceRange name = expr->data.class_calls.name;
    StringView name_sv = string_range(name);
    SourceRange range = expr->data.class_calls.range;

    RegisterEntry* entry = register_get(reg, name_sv);

    if (!entry) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VSF,
            .data.vsf = { .range = name, .var_name = name_sv }
        });
        return;
    }

    if (entry->tag != Reg_Class) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_TNC,
            .data.tnc = {
                .range = name,
                .type_name = name_sv,
                .actual_kind = string(
                    entry->tag == Reg_Struct ? "struct" :
                    entry->tag == Reg_Enum   ? "enum"   : "unknown"
                ),
            }
        });
        return;
    }

    size_t call_count = expr->data.class_calls.param_count;
    size_t param_count = entry->data.class.methods_count;

    for (size_t i = 0; i < call_count; i++) {
        SourceRange arg_range = expr->data.class_calls.param[i].name;
        StringView arg_sv = string_range(arg_range);

        RegisterEntry* arg_entry = register_get(reg, arg_sv);
        if (!arg_entry) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_VSF,
                .data.vsf = { .range = arg_range, .var_name = arg_sv }
            });
        }
    }
}

void check_enum_call_expr(Exprs* expr, Register* reg, CheckerErrList* errors) {
    SourceRange name = expr->data.enum_calls.name;
    SourceRange field = expr->data.enum_calls.field;
    StringView name_sv = string_range(name);
    StringView field_sv = string_range(field);
    SourceRange range = expr->data.enum_calls.range;

    RegisterEntry* entry = register_get(reg, name_sv);

    if (!entry) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VSF,
            .data.vsf = { .range = name, .var_name = name_sv }
        });
        return;
    }

    if (entry->tag != Reg_Enum) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_TNC,
            .data.tnc = {
                .range = name,
                .type_name = name_sv,
                .actual_kind = string(
                    entry->tag == Reg_Struct ? "struct" :
                    entry->tag == Reg_Class  ? "class"  :
                    entry->tag == Reg_Enum   ? "enum"   : "unknown"
                ),
            }
        });
        return;
    }

    EnumVariant* matched = NULL;
    for (size_t i = 0; i < entry->data.enm.variants_count; i++) {
        if (range_eq_sv(entry->data.enm.variants[i].name, field_sv)) {
            matched = &entry->data.enm.variants[i];
            break;
        }
    }

    if (!matched) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VSF,
            .data.vsf = { .range = field, .var_name = field_sv }
        });
        return;
    }

    size_t call_count = expr->data.enum_calls.param_count;
    size_t fields_count = matched->fields_count;

    if (call_count != fields_count) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_WFC,
            .data.wfc = {
                .range = range,
                .variant_name = field_sv,
                .expected_count = fields_count,
                .actual_count = call_count,
            }
        });
        return;
    }

    for (size_t i = 0; i < call_count; i++) {
        SourceRange arg_range = expr->data.enum_calls.param[i].name;
        StringView arg_sv = string_range(arg_range);

        Exprs arg_expr = {
            .tag = Expr_Identifiers,
            .data.identifiers = { .name = arg_range }
        };
        Type arg_type = infer_expr_type(&arg_expr, reg);
        Type expected_type = resolve_type(matched->fields[i].second, reg);

        if (arg_type.tag == Type_Void || expected_type.tag == Type_Void) continue;

        if (arg_type.tag != expected_type.tag) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_VMV,
                .data.vmv = {
                    .range = arg_range,
                    .var_name = arg_sv,
                    .expected_type = type_tag_to_view(expected_type),
                    .actual_type = type_tag_to_view(arg_type),
                }
            });
        }
    }
}

void check_expr(Exprs* expr, Register* reg, CheckerErrList* errors) {
    if (!expr) return;

    switch (expr->tag) {
        case Expr_Literals:    check_literal_expr(expr, reg, errors);       break;
        case Expr_Identifiers: check_identifier_expr(expr, reg, errors);    break;
        case Expr_Vars:        check_var_expr(expr, reg, errors);            break;
        case Expr_BinaryOps:   check_binary_op_expr(expr, reg, errors);     break;
        case Expr_Function:    check_function_call_expr(expr, reg, errors); break;
        case Expr_MethodCalls: check_method_call_expr(expr, reg, errors);   break;
        case Expr_Struct_Calls:check_struct_call_expr(expr, reg, errors);   break;
        case Expr_Class_Calls: check_class_call_expr(expr, reg, errors);    break;
        case Expr_Enum_Calls:  check_enum_call_expr(expr, reg, errors);     break;
        default: break;
    }
}