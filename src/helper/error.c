#include "ast.h"
#include "import.h"
#include "error.h"
#include "parser.h"
#include "footprint.h"

typedef struct Parser Parser;

LexerToken parser_current(Parser* self);
LexerToken parser_advance(Parser* self);

static CheckerErrList* g_err_list = NULL;

void parser_set_error_list(CheckerErrList* list) { g_err_list = list; }
void parse_error(Parser* self, ParseErrKind kind, const char* msg, LexerTokenTag expected) {
    LexerToken cur = parser_current(self);
    fprintf(stderr, "\n[!] Parse Error: %s (expected token %d, got %d)\n", msg ? msg : "syntax error", (int)expected, (int)cur.tag);

    const LineStarts* ls = &self->lexer.line_starts;

    size_t line_idx = get_line_num(ls, (uintptr_t)cur.range.start);
    if (line_idx != (size_t)-1) {
        const char* line_start = ls->data[line_idx];
        const char* line_end   = line_start;
        while (*line_end && *line_end != '\n') line_end++;

        size_t col = (size_t)(cur.range.start - line_start) + 1;

        fprintf(stderr, "  --> %zu:%zu\n", line_idx + 1, col);
        fprintf(stderr, "   | %.*s\n", (int)(line_end - line_start), line_start);
        fprintf(stderr, "   | %*s^\n", (int)(col - 1), "");
    }


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

    if (g_err_list) checker_err_push(g_err_list, e);
}


void parse_error_eof(Parser* self) {
    parse_error(self, ParseErr_UnexpectedEOF, "unexpected end of file", EOFs);
}

bool parser_expect(Parser* self, LexerTokenTag tag) {
    if (parser_current(self).tag == tag) { parser_advance(self); return true; }
    if (parser_current(self).tag == EOFs) parse_error_eof(self);
    else parse_error(self, ParseErr_ExpectedToken, NULL, tag);
    return false;
}

void parser_sync(Parser* self) {
    while (parser_current(self).tag != EOFs) {
        LexerTokenTag t = parser_current(self).tag;
        if (t == Ends       || t == Semicolons || t == Functions ||
            t == Classes    || t == Structs    || t == Enums     ||
            t == Traits     || t == Returns)
            return;
        parser_advance(self);
    }
}