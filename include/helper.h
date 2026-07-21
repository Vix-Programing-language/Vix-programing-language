#ifndef VIX_HELPER_H
#define VIX_HELPER_H

#include "import.h"
#include "register.h"



Type type_from_literal(SourceRange r);
Type infer_expr_type(Register* reg, Exprs* e);
Type type_from_range(SourceRange r);
FieldOwnerKind get_kind(Register* reg, SourceRange type_name);
Type get_function_type(RegisterEntry* cond_entry);
SourceRange register_generate_name(Register* reg, SourceRange inner_bind);

SourceRange mangle_name(SourceRange class_name, SourceRange method_name);
bool is_generic_type(RegisterEntry* e);
SourceRange mangle_name_unique(Register* reg, SourceRange class_name, SourceRange method_name);
bool is_variable(RegisterEntry* e);
bool is_user_defined_type(RegisterEntry* e);
void range_to_span(SourceRange r, LineStarts *ls, uint32_t *line_start, uint16_t *col_start, uint32_t *line_end, uint16_t *col_end);
bool range_eq(SourceRange r, const char* str);

bool is_operation(LexerTokenTag tag);
bool is_type_token(LexerTokenTag tag);
bool expr_is_empty(Exprs* expr);
bool is_type(SourceRange tok);
bool source_range_eq(SourceRange a, SourceRange b);
bool is_enum_variant(const RegisterEntry* entry, SourceRange name);
bool expr_exists(Exprs expr);

#endif /* VIX_HELPER_H */