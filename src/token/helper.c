#include "import.h"
#include "ast.h"

Exprs parser_expr(Parser* self);

bool range_eq(SourceRange r, const char* str) { size_t len = r.end - r.start; return strlen(str) == len && memcmp(r.start, str, len) == 0; }
LexerToken parser_current(Parser* self) { return self->cur[0]; }
LexerToken parser_peek(Parser* self) { return self->cur[1]; }
Exprs parse_condition(Parser* self) { return parser_expr(self); }

LexerToken parser_advance(Parser* self) {
    LexerToken tok = self->cur[0];
    if (tok.tag != EOFs) self->cur++;

    return tok;
}

bool is_operation(LexerTokenTag tag) {
    switch (tag) {
        case Plus: case Minuss: case Stars: case Slashs: case Percents:
        case Lesses: case Greaters: case Ampersands: case Pipes: case Carets:
        case Tildes: case Bangs:
        case PlusEqualss: case MinusEqualss: case StarEqualss: case SlashEqualss:
        case PercentEqualss: case PipeEqualss: case AmpersandEqualss:
        case CaretEqualss: case LeftShiftEqualss: case RightShiftEqualss:
        case LeftShifts: case RightShifts:
        case LessEqualss: case GreaterEqualss:
        case NotEqualss: case DoubleEqualss:
        case Ands: case Ors: case Nots:
        case Equalss: return true;
        default: return false;
    }
}

int parser_precedence(LexerTokenTag tag) {
    switch (tag) {
        case Ors:                                        return 1;
        case Ands:                                       return 2;
        case Pipes:                                      return 3;
        case Carets:                                     return 4;
        case Ampersands:                                 return 5;
        case DoubleEqualss: case NotEqualss:             return 6;
        case Lesses: case Greaters:
        case LessEqualss: case GreaterEqualss:           return 7;
        case LeftShifts: case RightShifts:               return 8;
        case Plus: case Minuss:                          return 9;
        case Stars: case Slashs: case Percents:          return 10;
        default:                                         return -1;
    }
}


