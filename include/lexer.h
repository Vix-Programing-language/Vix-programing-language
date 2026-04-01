#ifndef VIX_TOKEN_LEXER_H
#define VIX_TOKEN_LEXER_H

#include "ast/ast.h"

Lexer lexer_new(const char* source);
LexerToken lexer_peek(Lexer* self);
void lexer_advance(Lexer* self);

#endif
