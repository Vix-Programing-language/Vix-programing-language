#ifndef VIX_HELPER_H
#define VIX_HELPER_H

#include "import.h"
#include "register.h"


#ifdef __cplusplus
extern "C" {
#endif

StringView  string(const char *s);
StringView  string_range(SourceRange range);
bool sv_equal(StringView a, StringView b);
bool str_equal(const char *a, const char *b);
bool ranges_equal(SourceRange a, SourceRange b);
bool range_eq(SourceRange r, const char *str);
bool range_eq_sv(SourceRange r, StringView sv);

void range_to_span(SourceRange r, LineStarts *ls, uint32_t *line_start, uint16_t *col_start, uint32_t *line_end, uint16_t *col_end);

const char *op_tag_to_str(LexerTokenTag tag);
StringView  type_tag_to_view(Type t);

bool conds_equal(Exprs *a, Exprs *b);
bool is_tautolog(Exprs *cond);
bool is_always(Exprs *cond);

bool is_conditionable(Type t);
bool is_builtin_type(SourceRange name);

bool generic_param_used_in_type(SourceRange param, SourceRange type_name);
bool param_name_eq(Param *a, Param *b);

bool body_has_return(Stmts *body, size_t body_count);
bool body_has_unreachable(Stmts *body, size_t body_count);

bool expr_references_var(Exprs *expr, StringView var);
bool stmt_references_var(Stmts *stmt, StringView var);

StringView type_to_sv(Type t);
SourceRange type_to_source_range(Type t);

Type range_to_type(SourceRange r, Register *reg);
Type* type_heap(Type t);
StringView sv_of(SourceRange r);
Type get_type(SourceRange r, Type* tree, Register* reg);
#ifdef __cplusplus
}
#endif

#endif /* VIX_HELPER_H */