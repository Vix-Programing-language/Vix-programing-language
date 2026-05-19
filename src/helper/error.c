#include "ast.h"
#include "import.h"
#include "error.h"
#include "parser.h"

typedef struct Parser Parser;

LexerToken parser_current(Parser* self);
LexerToken parser_advance(Parser* self);


typedef struct {
    ParseErrKind    kind;
    SourceRange     range;
    LexerTokenTag   expected;
    LexerTokenTag   got;
    const char*     message;
} ParseError;

typedef ARR(ParseError) ParseErrorArr;
static ParseErrorArr g_errors = {0};
static CheckerErrList* g_err_list = NULL;

void parser_set_error_list(CheckerErrList* list) { g_err_list = list; }
void parse_error(Parser* self, ParseErrKind kind, const char* msg, LexerTokenTag expected) {
    LexerToken cur = parser_current(self);

    CheckerErr e = {
        .tag = Err_Tag_Parse,
        .range = cur.range,
        .data.parse = {
            .range    = cur.range,
            .message  = msg,
            .expected = (int)expected,
            .got      = (int)cur.tag,
            .kind     = (int)kind,
        },
    };

    if (g_err_list) {
        checker_err_push(g_err_list, e);
    }
}


void parse_error_eof(Parser* self) { parse_error(self, ParseErr_UnexpectedEOF, "unexpected end of file", EOFs); }

bool parser_expect(Parser* self, LexerTokenTag tag) {
    if (parser_current(self).tag == tag) { parser_advance(self); return true; }
    if (parser_current(self).tag == EOFs) { parse_error_eof(self); } else { parse_error(self, ParseErr_ExpectedToken, NULL, tag); }

    return false;
}

void parser_sync(Parser* self) {
    while (parser_current(self).tag != EOFs) {
        LexerTokenTag t = parser_current(self).tag;
        if (t == Ends || t == Semicolons || t == Functions || t == Classes || t == Structs   || t == Enums  || t == Traits || t == Returns) return;
        parser_advance(self);
    }
}

ParseErrorArr parser_get_errors(void) { return g_errors; }

void parser_reset_errors(void) {
    free(g_errors.data);
    g_errors = (ParseErrorArr){0};
}
