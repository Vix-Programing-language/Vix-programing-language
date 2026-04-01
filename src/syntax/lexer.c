#include "ast.h"
#include "import.h"


Lexer lexer_new(const char* source) {
    return (Lexer){
        .source   = source,
    };
}

char lexer_current_char(Lexer* self) {
    return self->source[self->pos];
}

char lexer_peek_char(Lexer* self) {
    return self->source[self->pos + 1];
}

static char lexer_advance_char(Lexer* self) {
    char ans = lexer_current_char(self);
    if(ans=='\n'){
        ARR_PUSH(self->line_starts,&self->source[++self->pos]);
    }
    return ans;
}

static void skip_whitespace(Lexer* self){
    while(isspace(lexer_current_char(self))){
        lexer_advance_char(self);
    }
}

void skip_comments(Lexer* self){
    skip_whitespace(self);
    if(lexer_current_char(self)=='/'&&lexer_peek_char(self)=='/'){
        while('\n'!=lexer_advance_char(self));
        //recurse
        skip_comments(self);
    }
}


void Tokenize(Lexer* self) {
    skip_comments(self);

    while (lexer_current_char(self) != '\0') {
        char c = lexer_current_char(self);
        if (isalpha(c) || c == '_') lexer_words(self);
        else if (isdigit(c)) lexer_numbers(self);
        else if (c == '"') lexer_strings(self);
        else if (c == '\'') lexer_char_literal(self);
        else lexer_chars(self);
    }

    lexer_push(self, (LexerToken){ .tag = EOFs });
    return self->tokens;
}

void lexer_chars(Lexer* self) {
    char c = lexer_advance_char(self);

    switch (c) {
        case '+': if (lexer_current_char(self) == '=') { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = PlusEqualss }); }    else { lexer_push(self, (LexerToken){ .tag = Plus }); }    break;
        case '-': if (lexer_current_char(self) == '=') { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = MinusEqualss }); }   else { lexer_push(self, (LexerToken){ .tag = Minuss }); }   break;
        case '*': if (lexer_current_char(self) == '=') { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = StarEqualss }); }    else { lexer_push(self, (LexerToken){ .tag = Stars }); }    break;
        case '%': if (lexer_current_char(self) == '=') { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = PercentEqualss }); } else { lexer_push(self, (LexerToken){ .tag = Percents }); } break;
        case '=': if (lexer_current_char(self) == '=') { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = DoubleEqualss }); }  else { lexer_push(self, (LexerToken){ .tag = Equalss }); }  break;
        case '!': if (lexer_current_char(self) == '=') { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = NotEqualss }); }     else { lexer_push(self, (LexerToken){ .tag = Bangs }); }     break;
        case '^': if (lexer_current_char(self) == '=') { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = CaretEqualss }); }   else { lexer_push(self, (LexerToken){ .tag = Carets }); }   break;

        case '&':
            if (lexer_current_char(self) == '&') { lexer_advance_char(self);  lexer_push(self, (LexerToken){ .tag = Ands }); }
            else if (lexer_current_char(self) == '=') { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = AmpersandEqualss }); }
            else { lexer_push(self, (LexerToken){ .tag = Ampersands }); } break;

        case '|':
            if (lexer_current_char(self) == '|') { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = Ors }); }
            else if (lexer_current_char(self) == '=') { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = PipeEqualss }); }
            else  { lexer_push(self, (LexerToken){ .tag = Pipes }); } break;

        case '<':
            if (lexer_current_char(self) == '=' ) { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = LessEqualss }); }
            else if (lexer_current_char(self) == '<' ) { lexer_advance_char(self); 
            if (lexer_current_char(self) == '=') { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = LeftShiftEqualss }); } 
            else { lexer_push(self, (LexerToken){ .tag = LeftShifts }); } }
            else { lexer_push(self, (LexerToken){ .tag = Lesses }); }
            break;

        case '>': 
            if (lexer_current_char(self) == '=') { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = GreaterEqualss }); }
            else if (lexer_current_char(self) == '>') { lexer_advance_char(self); 
            if (lexer_current_char(self) == '=') { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = RightShiftEqualss }); } 
            else { lexer_push(self, (LexerToken){ .tag = RightShifts }); } }
            else { lexer_push(self, (LexerToken){ .tag = Greaters }); } break;

        case '.':
            if (lexer_current_char(self) == '.') { lexer_advance_char(self); 
            if (lexer_current_char(self) == '.') { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = TrippleDots }); } 
            else { lexer_push(self, (LexerToken){ .tag = DoubleDots }); } }
            else { lexer_push(self, (LexerToken){ .tag = Dots }); }
            break;

        case '/':
            if (lexer_current_char(self) == '/') { 
                while (lexer_current_char(self) != '\n' && lexer_current_char(self) != '\0') 
                lexer_advance_char(self); 
            }
        
            else if (lexer_current_char(self) == '*') { lexer_advance_char(self); 
                while (!(lexer_current_char(self) == '*' && lexer_peek_char(self) == '/')) 
                    lexer_advance_char(self); 
                    lexer_advance_char(self); 
                    lexer_advance_char(self); 
                }

            else if (lexer_current_char(self) == '=') { lexer_advance_char(self); lexer_push(self, (LexerToken){ .tag = SlashEqualss }); }
            else { lexer_push(self, (LexerToken){ .tag = Slashs }); } break;

        case '~':  lexer_push(self, (LexerToken){ .tag = Tildes });        break;
        case '(':  lexer_push(self, (LexerToken){ .tag = LeftParens });    break;
        case ')':  lexer_push(self, (LexerToken){ .tag = RightParens });   break;
        case '[':  lexer_push(self, (LexerToken){ .tag = LeftBrackets });  break;
        case ']':  lexer_push(self, (LexerToken){ .tag = RightBrackets }); break;
        case '{':  lexer_push(self, (LexerToken){ .tag = LeftBraces });    break;
        case '}':  lexer_push(self, (LexerToken){ .tag = RightBraces });   break;
        case ',':  lexer_push(self, (LexerToken){ .tag = Commas });        break;
        case ':':  lexer_push(self, (LexerToken){ .tag = Colons });        break;
        case ';':  lexer_push(self, (LexerToken){ .tag = Semicolons });    break;
        case '@':  lexer_push(self, (LexerToken){ .tag = Ats });           break;
        case '#':  lexer_push(self, (LexerToken){ .tag = Hashs });         break;
        case '?':  lexer_push(self, (LexerToken){ .tag = Questions });     break;
        case '\\': lexer_push(self, (LexerToken){ .tag = Backslashs });    break;
        case '`':  lexer_push(self, (LexerToken){ .tag = Backticks });     break;
        case '_':  lexer_push(self, (LexerToken){ .tag = Underscores });   break;
        case '\0': lexer_push(self, (LexerToken){ .tag = EOFs });          break;

        case ' ':
        case '\t':
        case '\r':
        case '\n': break;

        default: printf("Unknown char: %c\n", c); break;
    }
}

char* lexer_read_word(Lexer* self) {
    size_t start = self->pos;

    while (self->source[self->pos] != '\0' &&
           (isalpha(self->source[self->pos]) ||
            isdigit(self->source[self->pos]) ||
            self->source[self->pos] == '_')) {
        self->pos++;
    }

    size_t len = self->pos - start;
    char* word = checked_malloc(len + 1);
    memcpy(word, self->source + start, len);
    word[len] = '\0';

    return word;
}

void lexer_words(Lexer* self) {
    char* word = lexer_read_word(self);

    if (strcmp(word, "func")     == 0) lexer_push(self, (LexerToken){ .tag = Functions }); 
    else if (strcmp(word, "return")   == 0) lexer_push(self, (LexerToken){ .tag = Returns });
    else if (strcmp(word, "if")       == 0) lexer_push(self, (LexerToken){ .tag = Ifs });
    else if (strcmp(word, "elif")     == 0) lexer_push(self, (LexerToken){ .tag = Elifs });
    else if (strcmp(word, "else")     == 0) lexer_push(self, (LexerToken){ .tag = Elses });
    else if (strcmp(word, "while")    == 0) lexer_push(self, (LexerToken){ .tag = Whiles });
    else if (strcmp(word, "for")      == 0) lexer_push(self, (LexerToken){ .tag = Fors });
    else if (strcmp(word, "in")       == 0) lexer_push(self, (LexerToken){ .tag = Ins });
    else if (strcmp(word, "do")       == 0) lexer_push(self, (LexerToken){ .tag = Dos });
    else if (strcmp(word, "match")    == 0) lexer_push(self, (LexerToken){ .tag = Matchs });
    else if (strcmp(word, "case")     == 0) lexer_push(self, (LexerToken){ .tag = Cases });
    else if (strcmp(word, "default")  == 0) lexer_push(self, (LexerToken){ .tag = Defaults });
    else if (strcmp(word, "class")    == 0) lexer_push(self, (LexerToken){ .tag = Classes });
    else if (strcmp(word, "trait")    == 0) lexer_push(self, (LexerToken){ .tag = Traits });
    else if (strcmp(word, "enum")     == 0) lexer_push(self, (LexerToken){ .tag = Enums });
    else if (strcmp(word, "struct")   == 0) lexer_push(self, (LexerToken){ .tag = Structs });
    else if (strcmp(word, "type")     == 0) lexer_push(self, (LexerToken){ .tag = Types });
    else if (strcmp(word, "let")      == 0) lexer_push(self, (LexerToken){ .tag = Lets });
    else if (strcmp(word, "var")      == 0) lexer_push(self, (LexerToken){ .tag = Vars });
    else if (strcmp(word, "const")    == 0) lexer_push(self, (LexerToken){ .tag = Consts });
    else if (strcmp(word, "local")    == 0) lexer_push(self, (LexerToken){ .tag = Locals });
    else if (strcmp(word, "mut")      == 0) lexer_push(self, (LexerToken){ .tag = Muts });
    else if (strcmp(word, "ref")      == 0) lexer_push(self, (LexerToken){ .tag = Refs });
    else if (strcmp(word, "borrow")   == 0) lexer_push(self, (LexerToken){ .tag = Borrows });
    else if (strcmp(word, "pub")      == 0) lexer_push(self, (LexerToken){ .tag = Publics });
    else if (strcmp(word, "unsafe")   == 0) lexer_push(self, (LexerToken){ .tag = Unsafes });
    else if (strcmp(word, "self")     == 0) lexer_push(self, (LexerToken){ .tag = Selfs });
    else if (strcmp(word, "as")       == 0) lexer_push(self, (LexerToken){ .tag = Ass });
    else if (strcmp(word, "then")     == 0) lexer_push(self, (LexerToken){ .tag = Thens });
    else if (strcmp(word, "end")      == 0) lexer_push(self, (LexerToken){ .tag = Ends });
    else if (strcmp(word, "not")      == 0) lexer_push(self, (LexerToken){ .tag = Nots });
    else if (strcmp(word, "and")      == 0) lexer_push(self, (LexerToken){ .tag = Ands });
    else if (strcmp(word, "or")       == 0) lexer_push(self, (LexerToken){ .tag = Ors });
    else if (strcmp(word, "is")       == 0) lexer_push(self, (LexerToken){ .tag = Iss });
    else if (strcmp(word, "true")     == 0) lexer_push(self, (LexerToken){ .tag = Trues });
    else if (strcmp(word, "false")    == 0) lexer_push(self, (LexerToken){ .tag = Falses });
    else if (strcmp(word, "continue") == 0) lexer_push(self, (LexerToken){ .tag = Continues });
    else if (strcmp(word, "break")    == 0) lexer_push(self, (LexerToken){ .tag = Breaks });
    else { lexer_push(self, (LexerToken){ .tag = Identifier, .data = { .s = word }}); return; }

    free(word);
}

void lexer_types(Lexer* self) {
    char* word = lexer_read_word(self);

    if (strcmp(word, "int")     == 0) lexer_push(self, (LexerToken){ .tag = Ints,   .data = { .value_int = 32  }});
    else if (strcmp(word, "int8")    == 0) lexer_push(self, (LexerToken){ .tag = Ints,   .data = { .value_int = 8   }});
    else if (strcmp(word, "int16")   == 0) lexer_push(self, (LexerToken){ .tag = Ints,   .data = { .value_int = 16  }});
    else if (strcmp(word, "int32")   == 0) lexer_push(self, (LexerToken){ .tag = Ints,   .data = { .value_int = 32  }});
    else if (strcmp(word, "int64")   == 0) lexer_push(self, (LexerToken){ .tag = Ints,   .data = { .value_int = 64  }});
    else if (strcmp(word, "float")   == 0) lexer_push(self, (LexerToken){ .tag = Floats, .data = { .value_int = 32  }});
    else if (strcmp(word, "float32") == 0) lexer_push(self, (LexerToken){ .tag = Floats, .data = { .value_int = 32  }});
    else if (strcmp(word, "float64") == 0) lexer_push(self, (LexerToken){ .tag = Floats, .data = { .value_int = 64  }});
    else if (strcmp(word, "char")    == 0) lexer_push(self, (LexerToken){ .tag = Chars,  .data = { .value_int = 8   }});
    else if (strcmp(word, "string")  == 0) lexer_push(self, (LexerToken){ .tag = Strings,.data = { .value_int = 0   }});
    else {
        // Is not a type error handling...
        return;
    }

    free(word);
}

