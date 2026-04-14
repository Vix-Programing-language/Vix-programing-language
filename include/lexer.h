#ifndef VIX_TOKEN_LEXER_H
#define VIX_TOKEN_LEXER_H

#include "ast.h"
#include "file_manager.h"

Lexer lexer_new(FileId file_id, const char* source);
LexerToken lexer_peek(Lexer* self);
void lexer_advance(Lexer* self);

#endif
