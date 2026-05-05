#include "import.h"
#include "register.h"
#include "error.h"
#include "ir.h"
#include "ast.h"

bool str_equal(const char* a, const char* b);
bool sv_equal(StringView a, StringView b);
void check_guard_pattern(Pattern* p, SourceRange range, Register* reg, CheckerErrList* errors);
void check_cond_type(Exprs* cond, SourceRange stmt_range, Register* reg, CheckerErrList* errors);
StringView string_range(SourceRange range);
StringView string(const char* s);
Type infer_expr_type(Exprs* expr, Register* reg);
Type resolve_type(SourceRange r, Register* reg);
RegisterEntry* register_get(Register* reg, StringView key);
void register_insert(Register* reg, StringView key, RegisterEntry entry);
bool register_class(Stmts* stmt, Register* reg, CheckerErrList* errors);
Type infer_literal_type(SourceRange range);
bool generic_param_used_in_type(SourceRange param, SourceRange type_name);
bool param_name_eq(Param* a, Param* b);
bool body_has_unreachable(Stmts* body, size_t body_count);
bool body_has_return(Stmts* body, size_t body_count);
bool stmt_references_var(Stmts* stmt, StringView var);
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



void check_if_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors, Exprs* parent_cond) {
    Exprs* cond = &stmt->data.ifs.cond;
    SourceRange range = stmt->data.ifs.range;

    if (stmt->data.ifs.guard_pattern.tag != 0) {
        check_guard_pattern(&stmt->data.ifs.guard_pattern, range, reg, errors);

        if (stmt->data.ifs.body_count == 0) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_EMB,
                .data.emb = { .range = range }
            });
        }
        return;
    }

    if (cond->tag == Expr_BinaryOps && cond->data.binary_ops.op == Equalss) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_AIC,
            .data.aic = { .range = range }
        });
        return;
    }

    check_cond_type(cond, range, reg, errors);

    if (cond->tag == Expr_Literals) {
        SourceRange r = cond->data.literals.range;
        if (range_eq(r, "true") || range_eq(r, "false")) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_DCB,
                .data.dcb = {
                    .range = range,
                    .is_always_true = range_eq(r, "true"),
                }
            });
        }
    }

    if (is_tautolog(cond)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_TAU,
            .data.tau = {
                .range = range,
                .is_always_true = !is_always(cond),
            }
        });
    }

    if (parent_cond != NULL && conds_equal(cond, parent_cond)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_RNC,
            .data.rnc = { .range = range }
        });
    }

    if (stmt->data.ifs.body_count == 0) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_EMB,
            .data.emb = { .range = range }
        });
    }

    if (stmt->data.ifs.else_body_count == 0) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_EMB,
            .data.emb = { .range = range }
        });
    }

    if (stmt->data.ifs.else_body_count == 1 && stmt->data.ifs.else_body[0].tag == Stmt_Ifs) {
        Stmts* elif_stmt = &stmt->data.ifs.else_body[0];
        Exprs* elif_cond = &elif_stmt->data.ifs.cond;
        if (conds_equal(cond, elif_cond)) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_DEC,
                .data.dec = { .range = elif_stmt->data.ifs.range }
            });
        }
    }
}


static void check_match_pattern(Pattern* p, SourceRange range, Type match_type, Register* reg, CheckerErrList* errors) {
    switch (p->tag) {
        case Pattern_Wildcard: return;

        case Pattern_LiteralInt: {
            if (match_type.tag != Type_Int) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_PTM,
                    .data.ptm = {
                        .range = range,
                        .expected_type = type_tag_to_view(match_type),
                        .actual_type = string("int"),
                    }
                });
            }
            return;
        }

        case Pattern_LiteralStr: {
            if (match_type.tag != Type_Str) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_PTM,
                    .data.ptm = {
                        .range = range,
                        .expected_type = type_tag_to_view(match_type),
                        .actual_type = string("str"),
                    }
                });
            }
            return;
        }

        case Pattern_LiteralBool: {
            if (match_type.tag != Type_Bool) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_PTM,
                    .data.ptm = {
                        .range = range,
                        .expected_type = type_tag_to_view(match_type),
                        .actual_type = string("bool"),
                    }
                });
            }
            return;
        }

        case Pattern_Binding: return;

        case Pattern_Struct: {
            if (match_type.tag != Type_Custom) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_PTM,
                    .data.ptm = {
                        .range = range,
                        .expected_type = type_tag_to_view(match_type),
                        .actual_type = string("struct pattern"),
                    }
                });
                return;
            }

            RegisterEntry* entry = register_get(reg, string_range(match_type.data.custom.name));
            if (!entry || entry->tag != Reg_Struct) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_TNC,
                    .data.tnc = {
                        .range = range,
                        .type_name = string_range(match_type.data.custom.name),
                        .actual_kind = string(entry ? "non-struct" : "unknown"),
                    }
                });
                return;
            }

            for (size_t i = 0; i < p->data.struct_pattern.fields_count; i++) {
                SourceRange field = p->data.struct_pattern.fields[i];
                bool found = false;
                for (size_t j = 0; j < entry->data.strct.fields_count; j++) {
                    if (range_eq_sv(entry->data.strct.fields[j].name, string_range(field))) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    checker_err_push(errors, (CheckerErr){
                        .tag = Err_Tag_SFF,
                        .data.sff = {
                            .range = field,
                            .field_name = string_range(field),
                            .type_name = string_range(match_type.data.custom.name),
                        }
                    });
                }
            }
            return;
        }

        case Pattern_Variant: {
            StringView     name = string(p->data.variant.name);
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

        default: return;
    }
}

static bool patterns_equal(Pattern* a, Pattern* b) {
    if (a->tag != b->tag) return false;
    switch (a->tag) {
        case Pattern_Wildcard:     return true;
        case Pattern_LiteralInt:   return a->data.value_int  == b->data.value_int;
        case Pattern_LiteralBool:  return a->data.value_bool == b->data.value_bool;
        case Pattern_LiteralStr:   return str_equal(a->data.value_str, b->data.value_str);
        case Pattern_Variant:      return str_equal(a->data.variant.name, b->data.variant.name);
        case Pattern_VariantTuple: return sv_equal(a->data.variant_tuple.name, b->data.variant_tuple.name) && a->data.variant_tuple.bindings_count == b->data.variant_tuple.bindings_count;
        case Pattern_Binding:      return str_equal(a->data.binding, b->data.binding);
        default: return false;
    }
}

void check_match_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    Exprs* expr = &stmt->data.matchs.expr;
    MatchArm* cases = stmt->data.matchs.cases;
    size_t cases_count = stmt->data.matchs.cases_count;
    size_t default_body_count = stmt->data.matchs.default_body_count;
    SourceRange range = stmt->data.matchs.range;

    Type match_type = infer_expr_type(expr, reg);

    if (expr->tag == Expr_Literals) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_MLM,
            .data.mlm = { .range = range }
        });
    }

    if (match_type.tag == Type_Bool) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_MBM,
            .data.mbm = { .range = range }
        });
    }

    if (cases_count == 0 && default_body_count == 0) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_EMB,
            .data.emb = { .range = range }
        });
        return;
    }

    bool found_wildcard = false;
    for (size_t i = 0; i < cases_count; i++) {
        MatchArm* arm = &cases[i];

        if (found_wildcard) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_URC,
                .data.urc = { .range = range }
            });
            break;
        }

        check_match_pattern(&arm->pattern, range, match_type, reg, errors);

        if (arm->body_count == 0) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_EMB,
                .data.emb = { .range = range }
            });
        }

        for (size_t j = 0; j < i; j++) {
            if (patterns_equal(&arm->pattern, &cases[j].pattern)) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_DCP,
                    .data.dcp = { .range = range }
                });
                break;
            }
        }

        if (arm->pattern.tag == Pattern_Wildcard || arm->pattern.tag == Pattern_Binding) {
            found_wildcard = true;
        }
    }

    if (default_body_count == 0 && stmt->data.matchs.default_body != NULL) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_EMB,
            .data.emb = { .range = range }
        });
    }

    if (found_wildcard && default_body_count > 0) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_URC,
            .data.urc = { .range = range }
        });
    }

    if (match_type.tag == Type_Custom && default_body_count == 0) {
        RegisterEntry* entry = register_get(reg, string_range(match_type.data.custom.name));
        if (entry && entry->tag == Reg_Enum) {
            for (size_t i = 0; i < entry->data.enm.variants_count; i++) {
                EnumVariant* v = &entry->data.enm.variants[i];
                StringView   vname = string_range(v->name);
                bool covered = false;

                for (size_t j = 0; j < cases_count; j++) {
                    Pattern* p = &cases[j].pattern;
                    if (p->tag == Pattern_Wildcard || p->tag == Pattern_Binding) { covered = true; break; }
                    if (p->tag == Pattern_Variant && strlen(p->data.variant.name) == vname.len && memcmp(p->data.variant.name, vname.ptr, vname.len) == 0) { covered = true; break; }
                    if (p->tag == Pattern_VariantTuple &&
                        p->data.variant_tuple.name.len == vname.len &&
                        memcmp(p->data.variant_tuple.name.ptr, vname.ptr, vname.len) == 0) { covered = true; break; }
                }

                if (!covered) {
                    checker_err_push(errors, (CheckerErr){
                        .tag = Err_Tag_NDF,
                        .data.ndf = {
                            .range = range,
                            .variant_name = vname,
                            .type_name = string_range(match_type.data.custom.name),
                        }
                    });
                }
            }
        }
    }

    if (match_type.tag != Type_Custom && default_body_count == 0 && !found_wildcard) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_NDF,
            .data.ndf = {
                .range = range,
                .type_name = type_tag_to_view(match_type),
            }
        });
    }
}

void check_while_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors, Exprs* parent_cond) {
    Exprs* cond = &stmt->data.whiles.cond;
    Stmts* body = stmt->data.whiles.body;
    size_t body_count = stmt->data.whiles.body_count;
    SourceRange range = stmt->data.whiles.range;

    if (cond->tag == Expr_BinaryOps && cond->data.binary_ops.op == Equalss) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_AIC,
            .data.aic = { .range = range }
        });
        return;
    }

    if (body_count == 0) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_EMB,
            .data.emb = { .range = range }
        });
    }

    if (cond->tag == Expr_Literals) {
        SourceRange r = cond->data.literals.range;
        if (range_eq(r, "true") || range_eq(r, "false")) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_WLC,
                .data.wlc = {
                    .range = range,
                    .is_always_true = range_eq(r, "true"),
                }
            });
        }
    }

    Type cond_type = infer_expr_type(cond, reg);
    if (cond_type.tag != Type_Bool && cond_type.tag != Type_Void) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_NBC,
            .data.nbc = { .range = range, .cond_type = type_tag_to_view(cond_type) }
        });
    }

    if (is_tautolog(cond)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_TAU,
            .data.tau = {
                .range = range,
                .is_always_true = !is_always(cond),
            }
        });
    }

    if (parent_cond != NULL && conds_equal(cond, parent_cond)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_RNC,
            .data.rnc = { .range = range }
        });
    }

    bool found_exit = false;
    for (size_t i = 0; i < body_count; i++) {
        if (found_exit) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_URC,
                .data.urc = { .range = range }
            });
            break;
        }
        if (body[i].tag == Stmt_Returns || body[i].tag == Stmt_Continues) found_exit = true;
    }
}

void check_for_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    SourceRange var = stmt->data.fors._var;
    Exprs* iter = &stmt->data.fors.iter;
    Stmts* body = stmt->data.fors.body;
    size_t body_count = stmt->data.fors.body_count;
    SourceRange range = stmt->data.fors.range;

    if (body_count == 0) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_EMB,
            .data.emb = { .range = range }
        });
    }

    Type iter_type = infer_expr_type(iter, reg);
    if (iter_type.tag == Type_Void) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_ITV,
            .data.itv = {
                .range = range,
                .iter_name = string_range(iter->tag == Expr_Identifiers ? iter->data.identifiers.name : var),
            }
        });
        return;
    }

    if (iter->tag == Expr_Identifiers) {
        StringView iter_sv = string_range(iter->data.identifiers.name);
        RegisterEntry* iter_entry = register_get(reg, iter_sv);

        if (!iter_entry) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_ITR,
                .data.itr = { .range = range, .iter_name = iter_sv }
            });
            return;
        }

        Type t = iter_entry->type;
        if (t.tag == Type_Int || t.tag == Type_Float || t.tag == Type_Bool || t.tag == Type_Char) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_NIT,
                .data.nit = {
                    .range = range,
                    .iter_name = iter_sv,
                    .iter_type = type_tag_to_view(t),
                }
            });
            return;
        }
    }

    StringView var_sv = string_range(var);
    RegisterEntry* existing = register_get(reg, var_sv);
    if (existing) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_BSV,
            .data.bsv = { .range = var, .var_name = var_sv }
        });
    }

    bool found_exit = false;
    for (size_t i = 0; i < body_count; i++) {
        if (found_exit) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_URC,
                .data.urc = { .range = range }
            });
            break;
        }
        if (body[i].tag == Stmt_Returns || body[i].tag == Stmt_Continues) found_exit = true;
    }

    bool var_used = false;
    for (size_t i = 0; i < body_count; i++) {
        if (stmt_references_var(&body[i], var_sv)) { var_used = true; break; }
    }
    if (!var_used) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_ULV,
            .data.ulv = { .range = var, .var_name = var_sv }
        });
    }
}

void check_struct_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    SourceRange name = stmt->data.structs.name;
    StructParam* fields = stmt->data.structs.fields;
    size_t fields_count = stmt->data.structs.fields_count;
    SourceRange* generic_params = stmt->data.structs.generic_params;
    size_t generic_params_count = stmt->data.structs.generic_params_count;
    SourceRange range = stmt->data.structs.range;

    if (is_builtin_type(name)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_NCB,
            .data.ncb = { .range = range, .type_name = string_range(name) }
        });
        return;
    }

    if (fields_count == 0) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_EST,
            .data.est = { .range = range, .type_name = string_range(name) }
        });
    }

    for (size_t i = 0; i < fields_count; i++) {
        StructParam* field = &fields[i];

        if (range_eq(field->c_type, "void")) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_VFT,
                .data.vft = {
                    .range = range,
                    .field_name = string_range(field->name),
                    .type_name = string_range(name),
                }
            });
        }

        if (!is_builtin_type(field->c_type)) {
            bool is_generic = false;
            for (size_t g = 0; g < generic_params_count; g++) {
                if (generic_param_used_in_type(generic_params[g], field->c_type)) { is_generic = true; break; }
            }
            if (!is_generic && !register_get(reg, string_range(field->c_type))) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_UFT,
                    .data.uft = {
                        .range = range,
                        .field_name = string_range(field->name),
                        .type_name = string_range(field->c_type),
                    }
                });
            }
        }

        for (size_t j = 0; j < i; j++) {
            if (ranges_equal(field->name, fields[j].name)) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_DFN,
                    .data.dfn = {
                        .range = range,
                        .field_name = string_range(field->name),
                        .type_name = string_range(name),
                    }
                });
                break;
            }
        }

        for (size_t g = 0; g < generic_params_count; g++) {
            size_t glen = generic_params[g].end - generic_params[g].start;
            size_t flen = field->name.end - field->name.start;
            if (glen == flen && memcmp(generic_params[g].start, field->name.start, glen) == 0) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_DFN,
                    .data.dfn = {
                        .range = range,
                        .field_name = string_range(field->name),
                        .type_name = string_range(name),
                    }
                });
                break;
            }
        }
    }

    for (size_t g = 0; g < generic_params_count; g++) {
        bool used = false;
        for (size_t i = 0; i < fields_count && !used; i++) {
            if (generic_param_used_in_type(generic_params[g], fields[i].c_type)) used = true;
        }
        if (!used) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_GPU,
                .data.gpu = {
                    .range = range,
                    .param_name = string_range(generic_params[g]),
                    .type_name = string_range(name),
                }
            });
        }
    }
}

void check_enum_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    SourceRange name = stmt->data.enums.name;
    EnumVariant* variants = stmt->data.enums.variants;
    size_t variants_count = stmt->data.enums.variants_count;
    SourceRange* generic_params = stmt->data.enums.generic_params;
    size_t generic_params_count = stmt->data.enums.generic_params_count;
    SourceRange range = stmt->data.enums.range;

    if (is_builtin_type(name)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_NCB,
            .data.ncb = { .range = range, .type_name = string_range(name) }
        });
        return;
    }

    if (variants_count == 0) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_EEN,
            .data.een = { .range = range, .type_name = string_range(name) }
        });
        return;
    }

    if (variants_count == 1) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_SVE,
            .data.sve = { .range = range, .type_name = string_range(name) }
        });
    }

    for (size_t i = 0; i < variants_count; i++) {
        EnumVariant* variant = &variants[i];

        for (size_t j = 0; j < i; j++) {
            size_t alen = variant->name.end - variant->name.start;
            size_t blen = variants[j].name.end - variants[j].name.start;
            if (alen == blen && memcmp(variant->name.start, variants[j].name.start, alen) == 0) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_DVN,
                    .data.dvn = {
                        .range = range,
                        .variant_name = string_range(variant->name),
                        .type_name = string_range(name),
                    }
                });
                break;
            }
        }

        for (size_t f = 0; f < variant->fields_count; f++) {
            EnumField* field = &variant->fields[f];

            if (range_eq(field->second, "void")) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_VFT,
                    .data.vft = {
                        .range = range,
                        .field_name = string_range(field->first),
                        .type_name = string_range(name),
                    }
                });
            }

            if (!is_builtin_type(field->second)) {
                bool is_generic = false;
                for (size_t g = 0; g < generic_params_count; g++) {
                    if (generic_param_used_in_type(generic_params[g], field->second)) { is_generic = true; break; }
                }
                if (!is_generic && !register_get(reg, string_range(field->second))) {
                    checker_err_push(errors, (CheckerErr){
                        .tag = Err_Tag_UFT,
                        .data.uft = {
                            .range = range,
                            .field_name = string_range(field->first),
                            .type_name = string_range(field->second),
                        }
                    });
                }
            }

            for (size_t k = 0; k < f; k++) {
                size_t alen = field->first.end - field->first.start;
                size_t blen = variant->fields[k].first.end - variant->fields[k].first.start;
                if (alen == blen && memcmp(field->first.start, variant->fields[k].first.start, alen) == 0) {
                    checker_err_push(errors, (CheckerErr){
                        .tag = Err_Tag_DFV,
                        .data.dfv = {
                            .range = range,
                            .field_name = string_range(field->first),
                            .variant_name = string_range(variant->name),
                            .type_name = string_range(name),
                        }
                    });
                    break;
                }
            }
        }
    }

    for (size_t g = 0; g < generic_params_count; g++) {
        bool used = false;
        for (size_t i = 0; i < variants_count && !used; i++) {
            for (size_t f = 0; f < variants[i].fields_count && !used; f++) {
                if (generic_param_used_in_type(generic_params[g], variants[i].fields[f].second)) used = true;
            }
        }
        if (!used) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_GPU,
                .data.gpu = {
                    .range = range,
                    .param_name = string_range(generic_params[g]),
                    .type_name = string_range(name),
                }
            });
        }
    }
}

void check_function_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    SourceRange name = stmt->data.functions.name;
    Param* params = stmt->data.functions.params;
    size_t params_count = stmt->data.functions.params_count;
    SourceRange return_type = stmt->data.functions.return_type;
    Stmts* body = stmt->data.functions.body;
    size_t body_count = stmt->data.functions.body_count;
    SourceRange* generic_params = stmt->data.functions.generic_params;
    size_t generic_params_count = stmt->data.functions.generic_params_count;
    SourceRange range = stmt->data.functions.range;

    if (is_builtin_type(name)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_NCB,
            .data.ncb = { .range = range, .type_name = string_range(name) }
        });
        return;
    }

    if (!is_valid_return_type(return_type)) {
        bool is_generic = false;
        for (size_t g = 0; g < generic_params_count; g++) {
            if (generic_param_used_in_type(generic_params[g], return_type)) { is_generic = true; break; }
        }
        if (!is_generic && !register_get(reg, string_range(return_type))) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_UFT,
                .data.uft = {
                    .range = range,
                    .field_name = string_range(name),
                    .type_name = string_range(return_type),
                }
            });
        }
    }

    if (body_count == 0) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_EMB,
            .data.emb = { .range = range }
        });
    }

    if (!range_eq(return_type, "void") && return_type.start != return_type.end) {
        if (!body_has_return(body, body_count)) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_MRT,
                .data.mrt = {
                    .range = range,
                    .func_name = string_range(name),
                    .return_type = string_range(return_type),
                }
            });
        }
    }

    if (body_has_unreachable(body, body_count)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_URC,
            .data.urc = { .range = range }
        });
    }

    for (size_t i = 0; i < params_count; i++) {
        Param* param = &params[i];

        if (range_eq(param->c_type, "void")) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_VFT,
                .data.vft = {
                    .range = range,
                    .field_name = string_range(param->name),
                    .type_name = string_range(name),
                }
            });
        }

        if (!is_builtin_type(param->c_type)) {
            bool is_generic = false;
            for (size_t g = 0; g < generic_params_count; g++) {
                if (generic_param_used_in_type(generic_params[g], param->c_type)) { is_generic = true; break; }
            }
            if (!is_generic && !register_get(reg, string_range(param->c_type))) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_UFT,
                    .data.uft = {
                        .range = range,
                        .field_name = string_range(param->name),
                        .type_name = string_range(param->c_type),
                    }
                });
            }
        }

        for (size_t j = 0; j < i; j++) {
            if (param_name_eq(param, &params[j])) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_DFN,
                    .data.dfn = {
                        .range = range,
                        .field_name = string_range(param->name),
                        .type_name = string_range(name),
                    }
                });
                break;
            }
        }

        for (size_t g = 0; g < generic_params_count; g++) {
            size_t glen = generic_params[g].end - generic_params[g].start;
            size_t plen = param->name.end - param->name.start;
            if (glen == plen && memcmp(generic_params[g].start, param->name.start, glen) == 0) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_DFN,
                    .data.dfn = {
                        .range = range,
                        .field_name = string_range(param->name),
                        .type_name = string_range(name),
                    }
                });
                break;
            }
        }

        StringView param_sv = string_range(param->name);
        bool used = false;
        for (size_t b = 0; b < body_count; b++) {
            if (stmt_references_var(&body[b], param_sv)) { used = true; break; }
        }
        if (!used) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_ULV,
                .data.ulv = { .range = param->name, .var_name = param_sv }
            });
        }
    }

    for (size_t g = 0; g < generic_params_count; g++) {
        bool used = false;
        for (size_t i = 0; i < params_count && !used; i++) {
            if (generic_param_used_in_type(generic_params[g], params[i].c_type)) used = true;
        }
        if (generic_param_used_in_type(generic_params[g], return_type)) used = true;
        if (!used) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_GPU,
                .data.gpu = {
                    .range = range,
                    .param_name = string_range(generic_params[g]),
                    .type_name = string_range(name),
                }
            });
        }
    }
}

void check_class_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    SourceRange name = stmt->data.classes.name;
    Param* class_params = stmt->data.classes.class_params;
    size_t class_params_count = stmt->data.classes.class_params_count;
    StructParam* fields = stmt->data.classes.fields;
    size_t fields_count = stmt->data.classes.fields_count;
    FunctionMethod* methods = stmt->data.classes.methods;
    size_t methods_count = stmt->data.classes.methods_count;
    SourceRange parent = stmt->data.classes.parent;
    SourceRange* traits = stmt->data.classes.traits;
    size_t traits_count = stmt->data.classes.traits_count;
    SourceRange* generic_params = stmt->data.classes.generic_params;
    size_t generic_params_count = stmt->data.classes.generic_params_count;
    SourceRange range = stmt->data.classes.range;

    if (is_builtin_type(name)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_NCB,
            .data.ncb = { .range = range, .type_name = string_range(name) }
        });
        return;
    }

    if (fields_count == 0 && methods_count == 0) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_EST,
            .data.est = { .range = range, .type_name = string_range(name) }
        });
    }

    if (parent.start != parent.end) {
        RegisterEntry* parent_entry = register_get(reg, string_range(parent));
        if (!parent_entry) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_UFT,
                .data.uft = {
                    .range = range,
                    .field_name = string_range(name),
                    .type_name = string_range(parent),
                }
            });
        } else if (parent_entry->tag != Reg_Class) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_TNC,
                .data.tnc = {
                    .range = range,
                    .type_name = string_range(parent),
                    .actual_kind = string(
                        parent_entry->tag == Reg_Struct ? "struct" :
                        parent_entry->tag == Reg_Enum   ? "enum"   : "unknown"
                    ),
                }
            });
        }
    }

    for (size_t i = 0; i < traits_count; i++) {
        RegisterEntry* trait_entry = register_get(reg, string_range(traits[i]));

        if (!trait_entry) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_UFT,
                .data.uft = {
                    .range = range,
                    .field_name = string_range(name),
                    .type_name = string_range(traits[i]),
                }
            });
            continue;
        }

        if (trait_entry->tag != Reg_Trait) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_TNC,
                .data.tnc = {
                    .range = range,
                    .type_name = string_range(traits[i]),
                    .actual_kind = string(
                        trait_entry->tag == Reg_Struct ? "struct" :
                        trait_entry->tag == Reg_Enum   ? "enum"   :
                        trait_entry->tag == Reg_Class  ? "class"  : "unknown"
                    ),
                }
            });
            continue;
        }

        for (size_t m = 0; m < trait_entry->data.trait.methods_count; m++) {
            TraitMethod* required = &trait_entry->data.trait.methods[m];
            bool         implemented = false;

            for (size_t j = 0; j < methods_count; j++) {
                size_t rlen = required->name.end - required->name.start;
                size_t mlen = methods[j].name.end - methods[j].name.start;
                if (rlen == mlen && memcmp(required->name.start, methods[j].name.start, rlen) == 0) {
                    implemented = true;
                    break;
                }
            }

            if (!implemented) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_TMI,
                    .data.tmi = {
                        .range = range,
                        .class_name = string_range(name),
                        .trait_name = string_range(traits[i]),
                        .method_name = string_range(required->name),
                    }
                });
            }
        }
    }

    for (size_t i = 0; i < fields_count; i++) {
        StructParam* field = &fields[i];

        if (range_eq(field->c_type, "void")) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_VFT,
                .data.vft = {
                    .range = range,
                    .field_name = string_range(field->name),
                    .type_name = string_range(name),
                }
            });
        }

        if (!is_builtin_type(field->c_type)) {
            bool is_generic = false;
            for (size_t g = 0; g < generic_params_count; g++) {
                if (generic_param_used_in_type(generic_params[g], field->c_type)) { is_generic = true; break; }
            }
            if (!is_generic && !register_get(reg, string_range(field->c_type))) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_UFT,
                    .data.uft = {
                        .range = range,
                        .field_name = string_range(field->name),
                        .type_name = string_range(field->c_type),
                    }
                });
            }
        }

        for (size_t j = 0; j < i; j++) {
            if (ranges_equal(field->name, fields[j].name)) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_DFN,
                    .data.dfn = {
                        .range = range,
                        .field_name = string_range(field->name),
                        .type_name = string_range(name),
                    }
                });
                break;
            }
        }

        for (size_t g = 0; g < generic_params_count; g++) {
            size_t glen = generic_params[g].end - generic_params[g].start;
            size_t flen = field->name.end - field->name.start;
            if (glen == flen && memcmp(generic_params[g].start, field->name.start, glen) == 0) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_DFN,
                    .data.dfn = {
                        .range = range,
                        .field_name = string_range(field->name),
                        .type_name = string_range(name),
                    }
                });
                break;
            }
        }
    }

    for (size_t i = 0; i < methods_count; i++) {
        FunctionMethod* method = &methods[i];

        for (size_t j = 0; j < i; j++) {
            size_t alen = method->name.end - method->name.start;
            size_t blen = methods[j].name.end - methods[j].name.start;
            if (alen == blen && memcmp(method->name.start, methods[j].name.start, alen) == 0) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_DFN,
                    .data.dfn = {
                        .range = range,
                        .field_name = string_range(method->name),
                        .type_name = string_range(name),
                    }
                });
                break;
            }
        }

        if (!range_eq(method->return_type, "void") && method->return_type.start != method->return_type.end) {
            if (!body_has_return(method->body, method->body_count)) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_MRT,
                    .data.mrt = {
                        .range = range,
                        .func_name = string_range(method->name),
                        .return_type = string_range(method->return_type),
                    }
                });
            }
        }

        if (method->body_count == 0) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_EMB,
                .data.emb = { .range = range }
            });
        }

        if (body_has_unreachable(method->body, method->body_count)) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_URC,
                .data.urc = { .range = range }
            });
        }
    }

    for (size_t g = 0; g < generic_params_count; g++) {
        bool used = false;
        for (size_t i = 0; i < fields_count && !used; i++)
            if (generic_param_used_in_type(generic_params[g], fields[i].c_type)) used = true;
        for (size_t i = 0; i < class_params_count && !used; i++)
            if (generic_param_used_in_type(generic_params[g], class_params[i].c_type)) used = true;
        if (!used) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_GPU,
                .data.gpu = {
                    .range = range,
                    .param_name = string_range(generic_params[g]),
                    .type_name = string_range(name),
                }
            });
        }
    }
}

void check_trait_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    SourceRange name = stmt->data.traits.name;
    TraitMethod* methods = stmt->data.traits.methods;
    size_t methods_count = stmt->data.traits.methods_count;
    SourceRange range = stmt->data.traits.range;

    if (is_builtin_type(name)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_NCB,
            .data.ncb = { .range = range, .type_name = string_range(name) }
        });
        return;
    }

    if (methods_count == 0) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_EST,
            .data.est = { .range = range, .type_name = string_range(name) }
        });
    }

    for (size_t i = 0; i < methods_count; i++) {
        TraitMethod* method = &methods[i];

        for (size_t j = 0; j < i; j++) {
            size_t alen = method->name.end - method->name.start;
            size_t blen = methods[j].name.end - methods[j].name.start;
            if (alen == blen && memcmp(method->name.start, methods[j].name.start, alen) == 0) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_DFN,
                    .data.dfn = {
                        .range = range,
                        .field_name = string_range(method->name),
                        .type_name = string_range(name),
                    }
                });
                break;
            }
        }

        if (!is_valid_return_type(method->return_type) && !register_get(reg, string_range(method->return_type))) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_UFT,
                .data.uft = {
                    .range = range,
                    .field_name = string_range(method->name),
                    .type_name = string_range(method->return_type),
                }
            });
        }

        for (size_t p = 0; p < method->params_count; p++) {
            Param* param = &method->params[p];

            if (range_eq(param->c_type, "void")) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_VFT,
                    .data.vft = {
                        .range = range,
                        .field_name = string_range(param->name),
                        .type_name = string_range(name),
                    }
                });
            }

            if (!is_builtin_type(param->c_type) && !register_get(reg, string_range(param->c_type))) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_UFT,
                    .data.uft = {
                        .range = range,
                        .field_name = string_range(param->name),
                        .type_name = string_range(param->c_type),
                    }
                });
            }
        }
    }
}

void check_assign_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    Exprs* target = &stmt->data.assigns.target;
    Exprs* value = &stmt->data.assigns.value;
    LexerTokenTag op = stmt->data.assigns.op;
    SourceRange range = stmt->data.assigns.range;

    SourceRange target_name = {0};
    if(target->tag == Expr_Identifiers) target_name = target->data.identifiers.name;
    else if(target->tag == Expr_Vars) target_name = target->data.vars.name;
    else {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_LHS,
            .data.lhs = { .range = range }
        });
        return;
    }

    StringView target_sv = string_range(target_name);
    RegisterEntry* entry = register_get(reg, target_sv);

    if (!entry) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VSF,
            .data.vsf = { .range = target_name, .var_name = target_sv }
        });
        return;
    }

      if (entry->tag == Reg_Let || entry->tag == Reg_Const || entry->tag == Reg_Local) {
          checker_err_push(errors, (CheckerErr){
              .tag = Err_Tag_VNM,
                .data.vnm = {
                    .range = target_name,
                    .var_name = target_sv,
                    .binding_kind = string(
                        entry->tag == Reg_Let   ? "let"   :
                        entry->tag == Reg_Const ? "const" : "local"
                    ),
                    .decl_range = entry->decl_range,
                    .decl_name_range = entry->decl_name_range,
                }
            });
          return;
      }

    Type target_type = entry->type;
    Type value_type = infer_expr_type(value, reg);

    if (target_type.tag != Type_Void && value_type.tag != Type_Void &&
        target_type.tag != value_type.tag) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VMV,
            .data.vmv = {
                .range = range,
                .var_name = target_sv,
                .expected_type = type_tag_to_view(target_type),
                .actual_type = type_tag_to_view(value_type),
            }
        });
        return;
    }

    if (op != Equalss && target_type.tag == Type_Custom) {
        resolve_operations(&stmt->data.assigns.value, reg, errors);
    }
}

void check_vars_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    SourceRange name  = stmt->data.vars.name;
    SourceRange ctype = stmt->data.vars.c_type;
    SourceRange range = stmt->data.vars.range;

    if (is_builtin_type(name)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_NCB,
            .data.ncb = { .range = range, .type_name = string_range(name) }
        });
        return;
    }

    if (range_eq(ctype, "void")) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VFT,
            .data.vft = {
                .range = range,
                .field_name = string_range(name),
                .type_name  = string_range(ctype),
            }
        });
        return;
    }

    if (!is_builtin_type(ctype) && !register_get(reg, string_range(ctype))) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_UFT,
            .data.uft = {
                .range      = range,
                .field_name = string_range(name),
                .type_name  = string_range(ctype),
            }
        });
    }
}

void check_lets_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    SourceRange name  = stmt->data.lets.name;
    SourceRange ctype = stmt->data.lets.c_type;
    SourceRange range = stmt->data.lets.range;

    if (is_builtin_type(name)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_NCB,
            .data.ncb = { .range = range, .type_name = string_range(name) }
        });
        return;
    }

    if (range_eq(ctype, "void")) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VFT,
            .data.vft = {
                .range      = range,
                .field_name = string_range(name),
                .type_name  = string_range(ctype),
            }
        });
        return;
    }

    if (!is_builtin_type(ctype) && !register_get(reg, string_range(ctype))) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_UFT,
            .data.uft = {
                .range      = range,
                .field_name = string_range(name),
                .type_name  = string_range(ctype),
            }
        });
    }
}

void check_consts_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    SourceRange name  = stmt->data.consts.name;
    SourceRange ctype = stmt->data.consts.c_type;
    SourceRange range = stmt->data.consts.range;

    if (is_builtin_type(name)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_NCB,
            .data.ncb = { .range = range, .type_name = string_range(name) }
        });
        return;
    }

    if (range_eq(ctype, "void")) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VFT,
            .data.vft = {
                .range      = range,
                .field_name = string_range(name),
                .type_name  = string_range(ctype),
            }
        });
        return;
    }

    if (!is_builtin_type(ctype) && !register_get(reg, string_range(ctype))) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_UFT,
            .data.uft = {
                .range      = range,
                .field_name = string_range(name),
                .type_name  = string_range(ctype),
            }
        });
    }
}

void check_return_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    Exprs* expr  = &stmt->data.returns.expr;
    SourceRange range = stmt->data.returns.range;

    Type actual = infer_expr_type(expr, reg);

    if (actual.tag == Type_Void) return;

    if (actual.tag == Type_Custom) {
        RegisterEntry* entry = register_get(reg, string_range(actual.data.custom.name));
        if (!entry) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_VSF,
                .data.vsf = {
                    .range    = range,
                    .var_name = string_range(actual.data.custom.name),
                }
            });
        }
    }
}

void check_locals_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    SourceRange name = stmt->data.locals.name;
    SourceRange ctype = stmt->data.locals.c_type;
    SourceRange range = stmt->data.locals.range;

    if (is_builtin_type(name)) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_NCB,
            .data.ncb = { .range = range, .type_name = string_range(name) }
        });
        return;
    }

    if (range_eq(ctype, "void")) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VFT,
            .data.vft = {
                .range = range,
                .field_name = string_range(name),
                .type_name = string_range(ctype),
            }
        });
        return;
    }

    if (!is_builtin_type(ctype) && !register_get(reg, string_range(ctype))) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_UFT,
            .data.uft = {
                .range = range,
                .field_name = string_range(name),
                .type_name = string_range(ctype),
            }
        });
    }
}

void resolve_operations(Exprs* operations, Register* reg, CheckerErrList* errors) {
    Exprs* lhs = operations->data.binary_ops.left;
    Exprs* rhs = operations->data.binary_ops.right;
    LexerTokenTag op_tag = operations->data.binary_ops.op;
    SourceRange name_r;

    if(lhs->tag == Expr_Identifiers) name_r = lhs->data.identifiers.name;
    else if(lhs->tag == Expr_Vars) name_r = lhs->data.vars.name;
    else {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_LHS,
            .data.lhs = { .range = operations->data.binary_ops.range }
        });
        return;
    }

    StringView name_sv = string_range(name_r);
    RegisterEntry* entry = register_get(reg, name_sv);

    if (!entry) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VSF,
            .data.vsf = { .range = name_r, .var_name = name_sv }
        });
        return;
    }

    if (entry->tag != Reg_Var) {
          checker_err_push(errors, (CheckerErr){
              .tag = Err_Tag_VNM,
                .data.vnm = {
                    .range = name_r,
                    .var_name = string(entry->name),
                    .binding_kind = string(
                        entry->tag == Reg_Let   ? "let"   :
                        entry->tag == Reg_Const ? "const" : "local"
                    ),
                    .decl_range = entry->decl_range,
                    .decl_name_range = entry->decl_name_range,
                }
            });
          return;
      }

    if (entry->type.tag != Type_Custom) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_VPT,
            .data.vpt = {
                .range = name_r,
                .var_name = string(entry->name),
                .type_name = type_tag_to_view(entry->type),
            }
        });
        return;
    }

    SourceRange class_name_r = entry->type.data.custom.name;
    StringView class_name = string_range(class_name_r);
    RegisterEntry* class_entry = register_get(reg, string_range(class_name_r));

    if (!class_entry) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_TNF,
            .data.tnf = { .range = class_name_r, .type_name = class_name }
        });
        return;
    }

    if (class_entry->tag == Reg_Struct || class_entry->tag == Reg_Enum) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_TNC,
            .data.tnc = {
                .range = class_name_r,
                .type_name = class_name,
                .actual_kind = string(class_entry->tag == Reg_Struct ? "struct" : "enum"),
            }
        });
        return;
    }

    if (class_entry->tag != Reg_Class) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_TNC,
            .data.tnc = { .range = class_name_r, .type_name = class_name, .actual_kind = string("unknown") }
        });
        return;
    }

    FunctionMethod* matched = NULL;
    for (size_t i = 0; i < class_entry->data.class.methods_count; i++) {
        FunctionMethod* m = &class_entry->data.class.methods[i];
        if (m->operation.function.start != NULL && m->operation.op == op_tag) { matched = m; break; }
    }

    if (!matched) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_OUD,
            .data.oud = {
                .range = class_name_r,
                .class_name = class_name,
                .op = string(op_tag_to_str(op_tag)),
            }
        });
        return;
    }

    if (matched->params_count == 0) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_OMP,
            .data.omp = {
                .range = matched->range,
                .class_name = class_name,
                .method_name = string_range(matched->name),
                .op = string(op_tag_to_str(op_tag)),
            }
        });
        return;
    }

    Type rhs_type = infer_expr_type(rhs, reg);
    Type expected_type = resolve_type(matched->params[0].c_type, reg);

    if (rhs_type.tag != expected_type.tag) {
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_OMM,
            .data.omm = {
                .range = matched->range,
                .class_name = class_name,
                .method_name = string_range(matched->name),
                .op = string(op_tag_to_str(op_tag)),
                .expected_type = type_tag_to_view(expected_type),
                .actual_type = type_tag_to_view(rhs_type),
            }
        });
    }
}

void resolve_generic_call(Exprs* call, Register* reg, GenericRegistry* greg, CheckerErrList* errors) {
    SourceRange func_name = call->data.function_call.name;
    StringView func_name_sv = string_range(func_name);
    size_t params_count = call->data.function_call.param_count;
    GenericArg* args = malloc(params_count * sizeof(GenericArg));

    for (size_t i = 0; i < params_count; i++) {
        SourceRange param_name = call->data.function_call.param[i].name;
        RegisterEntry* entry = register_get(reg, string_range(param_name));

        if (!entry) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_VSF,
                .data.vsf = { .range = param_name, .var_name = string_range(param_name) }
            });
            free(args);
            return;
        }

        args[i] = (GenericArg){
            .type_name = type_tag_to_view(entry->type),
            .type = entry->type
        };
    }

    khint_t existing_it = generic_instance_table_get(greg->table, func_name_sv);
    bool has_matching = existing_it != kh_end(greg->table) && kh_val(greg->table, existing_it).args_count > 0 && kh_val(greg->table, existing_it).args[0].type.tag == args[0].type.tag;

    if (has_matching) { free(args); return; }

    GenericInstance inst = {
        .func_name = func_name_sv,
        .args = args,
        .args_count = params_count,
        .return_type = args[0].type,
        .params = NULL,
        .params_count = 0,
    };

    int absent = 0;
    khint_t it = generic_instance_table_put(greg->table, func_name_sv, &absent);
    kh_val(greg->table, it) = inst;
}

void check_extern_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    ExternBlock* block = &stmt->data.externs.block;
    SourceRange range = block->range;

    for (size_t i = 0; i < block->funcs_count; i++) {
        ExternFunction* fn = &block->funcs[i];

        for (size_t j = 0; j < fn->params_count; j++) {
            Param* p = &fn->params[j];

            if (range_eq(p->c_type, "void")) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_VFT,
                    .data.vft = {
                        .range      = range,
                        .field_name = string_range(p->name),
                        .type_name  = string_range(fn->name),
                    }
                });
            }

            if (!is_builtin_type(p->c_type) && !register_get(reg, string_range(p->c_type))) {
                checker_err_push(errors, (CheckerErr){
                    .tag = Err_Tag_UFT,
                    .data.uft = {
                        .range = range,
                        .field_name = string_range(p->name),
                        .type_name  = string_range(p->c_type),
                    }
                });
            }
        }

        if (!is_valid_return_type(fn->return_type) && !register_get(reg, string_range(fn->return_type))) {
            checker_err_push(errors, (CheckerErr){
                .tag = Err_Tag_UFT,
                .data.uft = {
                    .range = range,
                    .field_name = string_range(fn->name),
                    .type_name  = string_range(fn->return_type),
                }
            });
        }
    }
}

