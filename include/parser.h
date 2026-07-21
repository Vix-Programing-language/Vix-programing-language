#ifndef VIX_TOKEN_PARSER_H
#define VIX_TOKEN_PARSER_H

#include "ast.h"
#include "lexer.h"
#include "helper.h"

typedef struct Parser {
    Lexer lexer;
    bool atomic_imported;
    bool has_peek;
    LexerToken peek_tok;
} Parser;



Exprs parser_expr(Parser* self);
Exprs parser_index(Parser* self, SourceRange idx);
Exprs parser_function_call(Parser* self, SourceRange fn);
Exprs parser_method_calls(Parser* self, SourceRange class);
Exprs parser_struct_call(Parser* self, SourceRange str);
Exprs parser_enums_call(Parser* self, SourceRange en);
Exprs parser_expr_primary(Parser* self);

Type parser_type_base(Parser* self);
Type parser_type(Parser* self);

Stmts parser_functions(Parser* self, bool is_const, bool is_unsafe, bool is_pub);
Stmts parser_class(Parser* self, bool is_pub);
Stmts parser_structer(Parser* self, bool is_pub, bool is_unsafe);
Stmts parser_enums(Parser* self, bool is_pub, bool is_unsafe);
Stmts parser_traits(Parser* self, bool is_pub, bool is_unsafe);
Stmts parser_if(Parser* self);
Stmts parser_elif(Parser* self);
Stmts parser_else(Parser* self);
Stmts parser_return(Parser* self);
Stmts parser_stmt(Parser* self);
Stmts parser_operation(Parser* self);

LexerToken parser_peek_offset(Parser* self, size_t offset);
ExternBlock parser_extern(Parser* self);

void parser_class_method(Parser* self, bool is_pub, bool is_unsafe, MethodArr* methods);
void parse_error(Parser* self, ParseErrKind kind, const char* msg, LexerTokenTag expected);
void parse_error_eof(Parser* self);
void parser_sync(Parser* self);
void lexer_advance(Lexer* self);
void parser_set_error_list(CheckerErrList* list);
bool parser_expect(Parser* self, LexerTokenTag tag);

Lexer lexer_new(FileId file_id, const char* source);

LexerToken lexer_peek(Lexer* self);
LexerToken parser_current(Parser* self);
LexerToken parser_advance(Parser* self);
LexerToken parser_peek_next(Parser* self, int distance);

Parser parser_new(Lexer lex);
uint64_t pack_hash_source(const char* src, size_t len);
void pack_write_register(Register* reg, CheckerErrList* errors, LineStarts* ls, const char* source, size_t source_len, const char* source_file, const char* project_name);
char* read_file_to_string(const char* path);

Operation operation_op(Parser* self);

#define MATCH(t) (parser_current(self).tag == (t))
#define PEEK_MATCH(t) (parser_peek_next(self, 1).tag == (t))
#define PEEK_TAG() (parser_peek_next(self, 1).tag)
#define ADVANCE() parser_advance(self)

#define EXPECT(t, err_msg) \
    do { \
        if (!parser_expect(self, (t))) { \
            parse_error(self, ParseErr_ExpectedToken, (err_msg), (t)); \
            parser_sync(self); \
        } \
    } while(0)

Parser parser_new(Lexer lex) { return (Parser){.lexer = lex}; }

LexerToken parser_current(Parser* self) { return lexer_peek(&self->lexer); }

LexerToken parser_peek_next(Parser* self, int distance) {
    if (distance <= 0) return parser_current(self);

    Lexer temp_lexer = self->lexer;
    LexerToken tok = {0};

    for (int i = 0; i < distance; i++) {
        lexer_advance(&temp_lexer);
        tok = lexer_peek(&temp_lexer);
    }

    return tok;
}

LexerToken parser_advance(Parser* self) {
    LexerToken tok = lexer_peek(&self->lexer);
    lexer_advance(&self->lexer);
    self->has_peek = false;
    return tok;
}

LexerToken parser_peek_offset(Parser* self, size_t offset) {
    if (offset == 0) return parser_current(self);

    Lexer temp_lexer = self->lexer;
    LexerToken tok = {0};
    size_t steps = offset;

    if (self->has_peek) {
        if (steps == 1) {
            return self->peek_tok;
        }
        steps--;
    }

    for (size_t i = 0; i < steps; i++) {
        tok = lexer_peek(&temp_lexer);
        lexer_advance(&temp_lexer);
        if (tok.tag == EOFs) {
            break;
        }
    }

    return tok;
}

static GenericParamArr parse_generic_params(Parser* self) {
    GenericParamArr generic_params = {0};

    if (parser_current(self).tag != Lesses) return generic_params;

    parser_advance(self); 

    while (parser_current(self).tag != Greaters && parser_current(self).tag != EOFs) {
        if (parser_current(self).tag == Identifier || is_type_token(parser_current(self).tag)) {
            GenericParam gp = {0};
            gp.name = parser_current(self).range;
            parser_advance(self);

            if (parser_current(self).tag == Colons) {
                parser_advance(self);
                if (parser_current(self).tag == Identifier) {
                    gp.bound = parser_current(self).range;
                    parser_advance(self);
                } else {
                    parse_error(self, ParseErr_ExpectedToken, "expected trait bound after ':'", Identifier);
                }
            }

            ARR_PUSH(generic_params, gp);
        } else if (parser_current(self).tag == Commas) {
            parser_advance(self);
        } else {
            parse_error(self, ParseErr_UnexpectedToken, "expected identifier or ',' in generic params", parser_current(self).tag);
            parser_advance(self);
        }
    }

    EXPECT(Greaters, "expected closing '>' after generic parameters");
    return generic_params;
}

static RangeArr parse_generic_args(Parser* self) {
    RangeArr generic_params = {0};

    if (parser_current(self).tag != Lesses) {
        return generic_params;
    }
    parser_advance(self);

    while (parser_current(self).tag != Greaters && parser_current(self).tag != EOFs) {
        if (is_type_token(parser_current(self).tag)) {
            ARR_PUSH(generic_params, parser_current(self).range);
            parser_advance(self);
        } else if (parser_current(self).tag == Commas) {
            parser_advance(self);
        } else {
            parse_error(self, ParseErr_UnexpectedToken, "expected type in generic args", parser_current(self).tag);
            parser_advance(self);
        }
    }

    EXPECT(Greaters, "expected closing '>' after generic parameters");
    return generic_params;
}

static ParamArr parse_function_params(Parser* self) {
    ParamArr params = {0};

    if (parser_current(self).tag == LeftParens) {
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected '(' in function parameters", LeftParens);
        return params;
    }

    while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
        Param p = {0};

        if (parser_current(self).tag == Identifier) {
            p.name = parser_current(self).range;
            parser_advance(self);
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected parameter name", Identifier);
            parser_sync(self);
            break;
        }

        bool is_self = range_eq(p.name, "self");

        if (parser_current(self).tag == Colons) {
            parser_advance(self);
            p.type = parser_type(self);
        } else if (is_self) {
            p.type = (Type){ .tag = Type_Void };
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected ':' after parameter name", Colons);
        }

        ARR_PUSH(params, p);

        if (parser_current(self).tag == Commas) {
            parser_advance(self);
        }
    }

    EXPECT(RightParens, "expected closing ')' after parameter list");
    return params;
}

static ParamArr parse_call_arguments(Parser* self) {
    ParamArr params = {0};

    if (parser_current(self).tag != LeftParens) {
        return params;
    }
    parser_advance(self);

    while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
        Param p = {0};

        if (parser_current(self).tag == Identifier && parser_peek_next(self, 1).tag == Colons) {
            p.name = parser_current(self).range;
            parser_advance(self);
            parser_advance(self);
            p.value = parser_expr(self);
        } else {
            p.name  = (SourceRange){0};
            p.value = parser_expr(self);
        }

        ARR_PUSH(params, p);
        if (parser_current(self).tag == Commas) parser_advance(self);
    }

    EXPECT(RightParens, "expected closing ')' after parameter list");
    return params;
}


#endif
