#include "import.h"
#include "register.h"
#include "ast.h"
#include "error.h"
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


void check_cond_type(Exprs* cond, SourceRange stmt_range, Register* reg, CheckerErrList* errors) {
    Type t = { .tag = Type_Void };

    switch (cond->tag) {
        case Expr_Literals: { t = infer_literal_type(cond->data.literals.range); break; }
        case Expr_Vars:
        case Expr_Identifiers: {
            SourceRange r = cond->tag == Expr_Identifiers ? cond->data.identifiers.name : cond->data.vars.name;
            RegisterEntry* entry = register_get(reg, string_range(r));
            if (!entry) return;
            t = entry->type;
            break;
        }
        case Expr_Function: {
            RegisterEntry* entry = register_get(reg, string_range(cond->data.function_call.name));
            if (!entry || entry->tag != Reg_Function) return;
            t = entry->data.function.return_type;
            if (!is_conditionable(t)) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_NBC,
                    .data.nbc = { .range = stmt_range, .cond_type = type_tag_to_view(t) }
                });
            }
            return;
        }
        case Expr_MethodCalls: {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_NBC,
                .data.nbc = { .range = stmt_range, .cond_type = string("void") }
            });
            return;
        }
        case Expr_Struct_Calls:
        case Expr_Class_Calls: {
            SourceRange r = cond->tag == Expr_Class_Calls ? cond->data.class_calls.name : cond->data.struct_calls.name;
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_NBC,
                .data.nbc = { .range = stmt_range, .cond_type = string_range(r) }
            });
            return;
        }
        case Expr_BinaryOps: { t = infer_expr_type(cond, reg); break; }
        default: return;
    }

    if (!is_conditionable(t)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_NBC,
            .data.nbc = { .range = stmt_range, .cond_type = type_tag_to_view(t) }
        });
    }
}


void check_guard_pattern(Pattern* p, SourceRange range, Register* reg, CheckerErrList* errors) {
    switch (p->tag) {
        case Pattern_Guard: {
            if (p->data.guard.is_var) {
                // warn: binding to "_" is pointless
                StringView bname = p->data.guard.binding;
                if (bname.len == 1 && bname.ptr[0] == '_') {
                    checker_err_push(errors, (CheckerErr){
                        .tag = Err_Tag_EMB,
                        .data.emb = { .range = range }
                    });
                }

                Type rhs_type = infer_expr_type(p->data.guard.expr, reg);
                if (rhs_type.tag == Type_Void) {
                    checker_err_push(errors, (CheckerErr){
                        .tag = Err_Tag_NBC,
                        .data.nbc = { .range = range, .cond_type = string("void") }
                    });
                }
                return;
            }

            // pattern = expr form
            check_guard_pattern(p->data.guard.pattern, range, reg, errors);

            Type rhs_type = infer_expr_type(p->data.guard.expr, reg);
            if (rhs_type.tag == Type_Void) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_NBC,
                    .data.nbc = { .range = range, .cond_type = string("void") }
                });
            }
            return;
        }

        case Pattern_Struct: {
            for (size_t i = 0; i < p->data.struct_pattern.fields_count; i++) {
                SourceRange field = p->data.struct_pattern.fields[i];
                StringView sv = string_range(field);
                RegisterEntry* existing = register_get(reg, sv);
                if (existing) {
                    checker_err_push(errors, (CheckerErr){
                        .tag = Err_Tag_RDL,
                        .data.rdl = { .range = field, .var_name = sv }
                    });
                }
            }
            return;
        }

        case Pattern_Variant: {
            StringView name = string(p->data.variant.name);
            RegisterEntry* entry = register_get(reg, name);

            if (!entry) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_VSF,
                    .data.vsf = { .range = range, .var_name = name }
                });
                return;
            }

            if (entry->tag != Reg_Enum) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_TNC,
                    .data.tnc = {
                        .range = range,
                        .type_name = name,
                        .actual_kind = string(
                            entry->tag == Reg_Struct ? "struct" :
                            entry->tag == Reg_Class  ? "class"  : "unknown"
                        ),
                    }
                });
                return;
            }

            
            if (!p->data.variant.inner || p->data.variant.inner[0] == '\0') return;

            EnumVariant* matched = NULL;
            for (size_t i = 0; i < entry->data.enm.variants_count; i++) {
                if (range_eq_sv(entry->data.enm.variants[i].name, name)) {
                    matched = &entry->data.enm.variants[i];
                    break;
                }
            }

            if (!matched) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_VSF,
                    .data.vsf = { .range = range, .var_name = name }
                });
                return;
            }

            if (matched->fields_count != 1) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_WFC,
                    .data.wfc = {
                        .range = range,
                        .variant_name = name,
                        .expected_count = matched->fields_count,
                        .actual_count = 1,
                    }
                });
            }
            return;
        }

        case Pattern_VariantTuple: {
            StringView name = p->data.variant_tuple.name;
            RegisterEntry* entry = register_get(reg, name);

            if (!entry) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_VSF,
                    .data.vsf = { .range = range, .var_name = name }
                });
                return;
            }

            if (entry->tag != Reg_Enum) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_TNC,
                    .data.tnc = {
                        .range = range,
                        .type_name = name,
                        .actual_kind = string(
                            entry->tag == Reg_Struct ? "struct" :
                            entry->tag == Reg_Class  ? "class"  : "unknown"
                        ),
                    }
                });
                return;
            }

            EnumVariant* matched = NULL;
            for (size_t i = 0; i < entry->data.enm.variants_count; i++) {
                if (range_eq_sv(entry->data.enm.variants[i].name, name)) {
                    matched = &entry->data.enm.variants[i];
                    break;
                }
            }

            if (!matched) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_VSF,
                    .data.vsf = { .range = range, .var_name = name }
                });
                return;
            }

            if (p->data.variant_tuple.bindings_count != matched->fields_count) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_WFC,
                    .data.wfc = {
                        .range = range,
                        .variant_name = name,
                        .expected_count = matched->fields_count,
                        .actual_count = p->data.variant_tuple.bindings_count,
                    }
                });
            }
            return;
        }

        case Pattern_Wildcard: return;
        default:               return;
    }
}
