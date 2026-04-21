#ifndef VIX_TOKEN_PARSER_H
#define VIX_TOKEN_PARSER_H

#include "ast/ast.h"
#include "lexer.h"

typedef struct Parser {
    Lexer lexer;
} Parser;

static inline Parser parser_new(Lexer lex) {
    return (Parser){ .lexer = lex };
}

static inline LexerToken parser_current(Parser* self) {
    return lexer_peek(&self->lexer);
}

static inline LexerToken parser_advance(Parser* self) {
    LexerToken tok = lexer_peek(&self->lexer);
    lexer_advance(&self->lexer);
    return tok;
}
Stmts parser_stmt(Parser* self);
Exprs parser_expr(Parser* self);

#endif
