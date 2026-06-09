#include "lexer.h"
#include "ast.h"



static char lexer_advance_char(Lexer* self);
static void skip_whitespace(Lexer* self);
static void compue_top(Lexer* self);
static void lexer_chars(Lexer* self);
static char* lexer_read_word(Lexer* self);
static void lexer_words(Lexer* self);
static void lexer_numbers(Lexer* self);
static void lexer_strings(Lexer* self);
static void lexer_char_literal(Lexer* self);

Lexer lexer_new(FileId file_id, const char* source) {
    Lexer ans = (Lexer){
        .source = source,
        .cur = source,
        .file_id = file_id,
    };

    ARR_PUSH(ans.line_starts, ans.cur);

    return ans;
}

LexerToken lexer_peek(Lexer* self){
    if(!self->top.tag){
        compue_top(self);
    }

    return self->top;
}


void lexer_advance(Lexer* self) {
    self->top = (LexerToken){0};
}

static char lexer_advance_char(Lexer* self) {
    char ans = self->cur[0];
    if (ans == '\n') {
        ARR_PUSH(self->line_starts, self->cur + 1);
    }
    if (ans != '\0') self->cur++;
    return ans;
}

static void skip_whitespace(Lexer* self){
    while(isspace(self->cur[0])){
        lexer_advance_char(self);
    }
}

void skip_comments(Lexer* self){
    skip_whitespace(self);
    if(self->cur[0]=='/'&&self->cur[1]=='/'){
        while('\n'!=lexer_advance_char(self));
        skip_comments(self);
    }
}


static void compue_top(Lexer* self) {
    self->top = (LexerToken){0};
    skip_comments(self);

    self->top.range.start = self->cur;
    self->top.range.file_id=self->file_id;

    char c = self->cur[0];
    if (c == '\0') { self->top.tag = EOFs; } 
    else if (isalpha(c) || c == '_') { lexer_words(self); }
    else if (isdigit(c))             { lexer_numbers(self); }
    else if (c == '"')               { lexer_strings(self); }
    else if (c == '\'')              { lexer_char_literal(self); }
    else                             { lexer_chars(self); }

    self->top.range.end = self->cur;
}

static void lexer_chars(Lexer* self) {
    char c = lexer_advance_char(self);

    switch (c) {
        case '+': if (self->cur[0] == '=') { lexer_advance_char(self); self->top.tag = PlusEqualss; }    else { self->top.tag = Plus; }    break;
        case '-': if (self->cur[0] == '=') { lexer_advance_char(self); self->top.tag = MinusEqualss; }   else { self->top.tag = Minuss; }   break;
        case '*': if (self->cur[0] == '=') { lexer_advance_char(self); self->top.tag = StarEqualss; }    else { self->top.tag = Stars; }    break;
        case '%': if (self->cur[0] == '=') { lexer_advance_char(self); self->top.tag = PercentEqualss; } else { self->top.tag = Percents; } break;
        case '=': if (self->cur[0] == '=') { lexer_advance_char(self); self->top.tag = DoubleEqualss; }  else { self->top.tag = Equalss; }  break;
        case '!': if (self->cur[0] == '=') { lexer_advance_char(self); self->top.tag = NotEqualss; }     else { self->top.tag = Bangs; }     break;
        case '^': if (self->cur[0] == '=') { lexer_advance_char(self); self->top.tag = CaretEqualss; }   else { self->top.tag = Carets; }   break;

        case '&':
            if (self->cur[0] == '&') { lexer_advance_char(self);  self->top.tag = Ands; }
            else if (self->cur[0] == '=') { lexer_advance_char(self); self->top.tag = AmpersandEqualss; }
            else { self->top.tag = Ampersands; } break;

        case '|':
            if (self->cur[0] == '|') { lexer_advance_char(self); self->top.tag = Ors; }
            else if (self->cur[0] == '=') { lexer_advance_char(self); self->top.tag = PipeEqualss; }
            else  { self->top.tag = Pipes; } break;

        case '<':
            if (self->cur[0] == '=' ) { lexer_advance_char(self); self->top.tag = LessEqualss; }
            else if (self->cur[0] == '<' ) { lexer_advance_char(self); 
            if (self->cur[0] == '=') { lexer_advance_char(self); self->top.tag = LeftShiftEqualss; } 
            else { self->top.tag = LeftShifts; } }
            else { self->top.tag = Lesses; }
            break;

        case '>': 
            if (self->cur[0] == '=') { lexer_advance_char(self); self->top.tag = GreaterEqualss; }
            else if (self->cur[0] == '>') { lexer_advance_char(self); 
            if (self->cur[0] == '=') { lexer_advance_char(self); self->top.tag = RightShiftEqualss; } 
            else { self->top.tag = RightShifts; } }
            else { self->top.tag = Greaters; } break;

        case '.':
            if (self->cur[0] == '.') { lexer_advance_char(self); 
            if (self->cur[0] == '.') { lexer_advance_char(self); self->top.tag = TrippleDots; } 
            else { self->top.tag = DoubleDots; } }
            else { self->top.tag = Dots; }
            break;

    case '/':
        if (self->cur[0] == '/') {
            while (self->cur[0] != '\n' && self->cur[0] != '\0')
                lexer_advance_char(self);
                self->top.tag = EOFs;

                compue_top(self);
                return;
            }
        else if (self->cur[0] == '*') {
            lexer_advance_char(self);
            while (!(self->cur[0] == '*' && self->cur[1] == '/'))
            lexer_advance_char(self);
            lexer_advance_char(self);
            lexer_advance_char(self);

            compue_top(self);
            return;
        }
        else if (self->cur[0] == '=') { lexer_advance_char(self); self->top.tag = SlashEqualss; }
        else { self->top.tag = Slashs; }
        break;
        
        case '~':  self->top.tag = Tildes;        break;
        case '(':  self->top.tag = LeftParens;    break;
        case ')':  self->top.tag = RightParens;   break;
        case '[':  self->top.tag = LeftBrackets;  break;
        case ']':  self->top.tag = RightBrackets; break;
        case '{':  self->top.tag = LeftBraces;    break;
        case '}':  self->top.tag = RightBraces;   break;
        case ',':  self->top.tag = Commas;        break;
        case ':':  self->top.tag = Colons;        break;
        case ';':  self->top.tag = Semicolons;    break;
        case '@':  self->top.tag = Ats;           break;
        case '#':  self->top.tag = Hashs;         break;
        case '?':  self->top.tag = Questions;     break;
        case '\\': self->top.tag = Backslashs;    break;
        case '`':  self->top.tag = Backticks;     break;
        case '_':  self->top.tag = Underscores;   break;
        case '\0': self->top.tag = EOFs;          break;

        case ' ':
        case '\t':
        case '\r':
        case '\n': break;

        default: printf("Unknown char: %c\n", c); break;
    }
}

static char* lexer_read_word(Lexer* self) {
    const char* start = self->cur;

    while (self->cur[0] != '\0' &&
           (isalpha(self->cur[0]) ||
            isdigit(self->cur[0]) ||
            self->cur[0] == '_')) {
        self->cur++;
    }

    size_t len = self->cur - start;
    char* word = checked_malloc(len + 1);
    memcpy(word, start, len);
    word[len] = '\0';

    return word;
}

static void lexer_words(Lexer* self) {
    char* word = lexer_read_word(self);

    if (strcmp(word, "func")     == 0) self->top.tag = Functions; 
    else if (strcmp(word, "return")   == 0) self->top.tag = Returns;
    else if (strcmp(word, "if")       == 0) self->top.tag = Ifs;
    else if (strcmp(word, "elif")     == 0) self->top.tag = Elifs;
    else if (strcmp(word, "else")     == 0) self->top.tag = Elses;
    else if (strcmp(word, "while")    == 0) self->top.tag = Whiles;
    else if (strcmp(word, "for")      == 0) self->top.tag = Fors;
    else if (strcmp(word, "in")       == 0) self->top.tag = Ins;
    else if (strcmp(word, "do")       == 0) self->top.tag = Dos;
    else if (strcmp(word, "match")    == 0) self->top.tag = Matchs;
    else if (strcmp(word, "case")     == 0) self->top.tag = Cases;
    else if (strcmp(word, "default")  == 0) self->top.tag = Defaults;
    else if (strcmp(word, "class")    == 0) self->top.tag = Classes;
    else if (strcmp(word, "trait")    == 0) self->top.tag = Traits;
    else if (strcmp(word, "enum")     == 0) self->top.tag = Enums;
    else if (strcmp(word, "struct")   == 0) self->top.tag = Structs;
    else if (strcmp(word, "type")     == 0) self->top.tag = Types;
    else if (strcmp(word, "let")      == 0) self->top.tag = Lets;
    else if (strcmp(word, "var")      == 0) self->top.tag = Vars;
    else if (strcmp(word, "const")    == 0) self->top.tag = Consts;
    else if (strcmp(word, "global")    == 0) self->top.tag = Locals;
    else if (strcmp(word, "public")      == 0) self->top.tag = Publics;
    else if (strcmp(word, "unsafe")   == 0) self->top.tag = Unsafes;
    else if (strcmp(word, "self")     == 0) self->top.tag = Selfs;
    else if (strcmp(word, "as")       == 0) self->top.tag = Ass;
    else if (strcmp(word, "then")     == 0) self->top.tag = Thens;
    else if (strcmp(word, "end")      == 0) self->top.tag = Ends;
    else if (strcmp(word, "not")      == 0) self->top.tag = Nots;
    else if (strcmp(word, "and")      == 0) self->top.tag = Ands;
    else if (strcmp(word, "or")       == 0) self->top.tag = Ors;
    else if (strcmp(word, "is")       == 0) self->top.tag = Iss;
    else if (strcmp(word, "true")     == 0) self->top.tag = Trues;
    else if (strcmp(word, "false")    == 0) self->top.tag = Falses;
    else if (strcmp(word, "continue") == 0) self->top.tag = Continues;
    else if (strcmp(word, "break")    == 0) self->top.tag = Breaks;
    else if (strcmp(word, "extern")   == 0) self->top.tag = Externs;
    else if (strcmp(word, "lambda")   == 0) self->top.tag = Lambdas;
    else if (strcmp(word, "Atomic") == 0) self->top.tag =   Atomics;
    else if (strcmp(word, "module") == 0) self->top.tag =   Modules;
    else if (strcmp(word, "import") == 0) self->top.tag =   Imports;
    else if (strcmp(word, "from") == 0) self->top.tag   =   Froms;
    else if (strcmp(word, "int")    == 0) { self->top.tag = Ints; self->top.data.value_int = 32; self->top.is_unsigned = false; }
    else if (strcmp(word, "int8")   == 0) { self->top.tag = Ints; self->top.data.value_int = 8;  self->top.is_unsigned = false; }
    else if (strcmp(word, "int16")  == 0) { self->top.tag = Ints; self->top.data.value_int = 16; self->top.is_unsigned = false; }
    else if (strcmp(word, "int32")  == 0) { self->top.tag = Ints; self->top.data.value_int = 32; self->top.is_unsigned = false; }
    else if (strcmp(word, "int64")  == 0) { self->top.tag = Ints; self->top.data.value_int = 64; self->top.is_unsigned = false; }
    else if (strcmp(word, "uint")   == 0) { self->top.tag = Ints; self->top.data.value_int = 32; self->top.is_unsigned = true;  }
    else if (strcmp(word, "uint8")  == 0) { self->top.tag = Ints; self->top.data.value_int = 8;  self->top.is_unsigned = true;  }
    else if (strcmp(word, "uint16") == 0) { self->top.tag = Ints; self->top.data.value_int = 16; self->top.is_unsigned = true;  }
    else if (strcmp(word, "uint32") == 0) { self->top.tag = Ints; self->top.data.value_int = 32; self->top.is_unsigned = true;  }
    else if (strcmp(word, "uint64") == 0) { self->top.tag = Ints; self->top.data.value_int = 64; self->top.is_unsigned = true;  }
    else if (strcmp(word, "float")    == 0) { self->top.tag = Floats; self->top.data.value_int = 32; }
    else if (strcmp(word, "float32")  == 0) { self->top.tag = Floats; self->top.data.value_int = 32; }
    else if (strcmp(word, "float64")  == 0) { self->top.tag = Floats; self->top.data.value_int = 64; }
    else if (strcmp(word, "char")     == 0) { self->top.tag = Chars; self->top.data.value_int = 8; }
    else if (strcmp(word, "str")   == 0) { self->top.tag = Strings; self->top.data.value_int = 0; }
    else if (strcmp(word, "bool")   == 0) self->top.tag = Bools; 
    else if (strcmp(word, "null") == 0) self->top.tag = Nulls;
    else if (strcmp(word, "void") == 0) self->top.tag = Voids;
    else if (strcmp(word, "...") == 0) self->top.tag = Ellipsis;
    else if (strcmp(word, "Relaxed") == 0) { self->top.tag = Orderings; self->top.data.value_int = Ordering_Relaxed; }
    else if (strcmp(word, "Acquire") == 0) { self->top.tag = Orderings; self->top.data.value_int = Ordering_Acquire; }
    else if (strcmp(word, "Release") == 0) { self->top.tag = Orderings; self->top.data.value_int = Ordering_Release; }
    else if (strcmp(word, "AcqRel")  == 0) { self->top.tag = Orderings; self->top.data.value_int = Ordering_AcqRel;  }
    else if (strcmp(word, "SeqCst")  == 0) { self->top.tag = Orderings; self->top.data.value_int = Ordering_SeqCst;  }

    // build in functions:
    else if (strcmp(word, "size_of") == 0) self->top.tag = Fn_Sizes;
    else if (strcmp(word, "type_of") == 0) self->top.tag = Fn_Types;
    else if (strcmp(word, "align_of") == 0) self->top.tag = Fn_Align;

    else if (strcmp(word, "load")             == 0) { self->top.tag = Orderings; self->top.data.value_int = AtomicOp_Load;            }
    else if (strcmp(word, "store")            == 0) { self->top.tag = Orderings; self->top.data.value_int = AtomicOp_Store;           }
    else if (strcmp(word, "swap")             == 0) { self->top.tag = Orderings; self->top.data.value_int = AtomicOp_Swap;            }
    else if (strcmp(word, "compare_exchange") == 0) { self->top.tag = Orderings; self->top.data.value_int = AtomicOp_CompareExchange; }
    else if (strcmp(word, "fetch_add")        == 0) { self->top.tag = Orderings; self->top.data.value_int = AtomicOp_FetchAdd;        }
    else if (strcmp(word, "fetch_sub")        == 0) { self->top.tag = Orderings; self->top.data.value_int = AtomicOp_FetchSub;        }
    else if (strcmp(word, "fetch_and")        == 0) { self->top.tag = Orderings; self->top.data.value_int = AtomicOp_FetchAnd;        }
    else if (strcmp(word, "fetch_or")         == 0) { self->top.tag = Orderings; self->top.data.value_int = AtomicOp_FetchOr;         }
    else if (strcmp(word, "fetch_xor")        == 0) { self->top.tag = Orderings; self->top.data.value_int = AtomicOp_FetchXor;        }
    else if (strcmp(word, "fetch_nand")       == 0) { self->top.tag = Orderings; self->top.data.value_int = AtomicOp_FetchNand;       }
    else if (strcmp(word, "fetch_max")        == 0) { self->top.tag = Orderings; self->top.data.value_int = AtomicOp_FetchMax;        }
    else if (strcmp(word, "fetch_min")        == 0) { self->top.tag = Orderings; self->top.data.value_int = AtomicOp_FetchMin;        }
    else { self->top.tag = Identifier; self->top.data.s = word; return; }

    free(word);
}

static void lexer_numbers(Lexer* self) {
    const char* start = self->cur;

if (self->cur[0] == '0' && (self->cur[1] == 'x' || self->cur[1] == 'X')) {
        lexer_advance_char(self);
        lexer_advance_char(self);
        while (isxdigit(self->cur[0])) lexer_advance_char(self);

        size_t len = (size_t)(self->cur - start);
        char* value = checked_malloc(len + 1);
        memcpy(value, start, len);
        value[len] = '\0';
        self->top.tag = Ints;
        self->top.data.value_int = strtoull(value, NULL, 16);
        free(value);
        return;
    }

    if (self->cur[0] == '0' && (self->cur[1] == 'b' || self->cur[1] == 'B')) {
        lexer_advance_char(self);
        lexer_advance_char(self);
        while (self->cur[0] == '0' || self->cur[0] == '1') lexer_advance_char(self);

        size_t len = (size_t)(self->cur - start);
        char* value = checked_malloc(len + 1);
        memcpy(value, start, len);
        value[len] = '\0';
        self->top.tag = Ints;
        self->top.data.value_int = strtoull(value + 2, NULL, 2);
        free(value);
        return;
    }

    if (self->cur[0] == '0' && (self->cur[1] == 'o' || self->cur[1] == 'O')) {
        lexer_advance_char(self);
        lexer_advance_char(self);
        while (self->cur[0] >= '0' && self->cur[0] <= '7') lexer_advance_char(self);

        size_t len = (size_t)(self->cur - start);
        char* value = checked_malloc(len + 1);
        memcpy(value, start, len);
        value[len] = '\0';
        self->top.tag = Ints;
        self->top.data.value_int = strtoull(value + 2, NULL, 8);
        free(value);
        return;
    }


    while (isdigit(self->cur[0])) {
        lexer_advance_char(self);
    }

    if (self->cur[0] == '.' && isdigit(self->cur[1])) {
        lexer_advance_char(self);
        while (isdigit(self->cur[0])) {
            lexer_advance_char(self);
        }

        size_t len = (size_t)(self->cur - start);
        char* value = checked_malloc(len + 1);
        memcpy(value, start, len);
        value[len] = '\0';
        self->top.tag = Floats;
        self->top.data.value_float = strtof(value, NULL);
        free(value);
        return;
    }

    size_t len = (size_t)(self->cur - start);
    char* value = checked_malloc(len + 1);
    memcpy(value, start, len);
    value[len] = '\0';
    self->top.tag = Ints;
    self->top.data.value_int = strtoull(value, NULL, 10);
    free(value);
}

static void lexer_strings(Lexer* self) {
    lexer_advance_char(self);
    const char* start = self->cur;

    while (self->cur[0] != '"' && self->cur[0] != '\0') {
        if (self->cur[0] == '\\' && self->cur[1] != '\0') {
            lexer_advance_char(self);
        }
        lexer_advance_char(self);
    }

    size_t len = (size_t)(self->cur - start);
    char* value = checked_malloc(len + 1);
    memcpy(value, start, len);
    value[len] = '\0';
    self->top.tag = Strings;
    self->top.data.s = value;

    if (self->cur[0] == '"') {
        lexer_advance_char(self);
    }
}

static void lexer_char_literal(Lexer* self) {
    lexer_advance_char(self);

    char value = self->cur[0];
    if (value == '\\') {
        lexer_advance_char(self);
        value = self->cur[0];
    }

    self->top.tag = Chars;
    self->top.data.value_char = value;

    if (self->cur[0] != '\0') {
        lexer_advance_char(self);
    }
    if (self->cur[0] == '\'') {
        lexer_advance_char(self);
    }
}
