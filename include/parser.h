#ifndef VIX_TOKEN_PARSER_H
#define VIX_TOKEN_PARSER_H

#include "ast.h"

typedef struct Parser {
    LexerToken* cur;
} Parser;

Parser parser_new(LexerToken* tokens);
LexerToken parser_current(Parser* self);
LexerToken parser_advance(Parser* self);
LexerToken parser_peek(Parser* self);
Stmts parser_stmt(Parser* self);
Exprs parser_expr(Parser* self);

#endif
