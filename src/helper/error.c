#include "ast.h"
#include "import.h"
#include "parser.h"

typedef struct Parser Parser;

LexerToken parser_current(Parser* self);
LexerToken parser_advance(Parser* self);

typedef enum {
    ParseErr_UnexpectedEOF,
    ParseErr_ExpectedToken,
    ParseErr_UnexpectedToken,
    ParseErr_InvalidType,
    ParseErr_InvalidOperation,
} ParseErrKind;

typedef struct {
    ParseErrKind    kind;
    SourceRange     range;
    LexerTokenTag   expected;
    LexerTokenTag   got;
    const char*     message;
} ParseError;

typedef ARR(ParseError) ParseErrorArr;
static ParseErrorArr g_errors = {0};

void parse_error(Parser* self, ParseErrKind kind, const char* msg, LexerTokenTag expected) {
    ParseError e = {
        .kind = kind,
        .range = parser_current(self).range,
        .expected = expected,
        .got = parser_current(self).tag,
        .message = msg,
    };

    ARR_PUSH(g_errors, e);
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
