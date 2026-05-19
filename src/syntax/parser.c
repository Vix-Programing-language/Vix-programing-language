#include "parser.h"
#include "lexer.h"


bool range_eq(SourceRange r, const char* str) {
    size_t len = r.end - r.start;
    return strlen(str) == len && memcmp(r.start, str, len) == 0;
}


static inline bool is_operation(LexerTokenTag tag) {
    switch (tag) {
        case Plus:
        case Minuss:
        case Stars:
        case Slashs:
        case Percents:
        case Lesses:
        case Greaters:
        case NEqs:
        case Equalss:
        case Ampersands:
        case Pipes:
        case Carets:
        case Tildes:
        case Bangs:
        case PlusEqualss:
        case MinusEqualss:
        case StarEqualss:
        case SlashEqualss:
        case PercentEqualss:
        case PipeEqualss:
        case AmpersandEqualss:
        case CaretEqualss:
        case LeftShiftEqualss:
        case RightShiftEqualss:
        case LeftShifts:
        case RightShifts:
        case LessEqualss:
        case GreaterEqualss:
        case NotEqualss:
        case DoubleEqualss:
        case Ands:
        case Ors:
        case Nots:
            return true;
        default:
            return false;
    }
}

bool is_type(SourceRange tok) {
    return range_eq(tok, "int")     ||
           range_eq(tok, "int8")    ||
           range_eq(tok, "int16")   ||
           range_eq(tok, "int32")   ||
           range_eq(tok, "int64")   ||
           range_eq(tok, "float")   ||
           range_eq(tok, "float32") ||
           range_eq(tok, "float64") ||
           range_eq(tok, "char")    ||
           range_eq(tok, "string");
}

Stmts parser_stmt(Parser* self);
Exprs parser_expr(Parser* self);
Type parser_type(Parser* self);
Exprs parser_function_call(Parser* self, SourceRange fn);
Exprs parser_method_calls(Parser* self, SourceRange class);
Exprs parser_struct_call(Parser* self, SourceRange str);
Exprs parser_enums_call(Parser* self, SourceRange en);
Stmts parser_functions(Parser* self, bool is_const, bool is_unsafe, bool is_pub);
Stmts parser_class(Parser* self, bool is_pub);
Stmts parser_structer(Parser* self, bool is_pub, bool is_unsafe);
Stmts parser_enums(Parser* self, bool is_pub, bool is_unsafe);
Stmts parser_traits(Parser* self, bool is_pub, bool is_unsafe);
Stmts parser_if(Parser* self);
Stmts parser_elif(Parser* self);
Stmts parser_else(Parser* self);
Stmts parser_return(Parser* self);
ExternBlock parser_extern(Parser* self);

void parse_error(Parser* self, ParseErrKind kind, const char* msg, LexerTokenTag expected);
void parse_error_eof(Parser* self);
bool parser_expect(Parser* self, LexerTokenTag tag);
void parser_sync(Parser* self);

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

Operation operation_op(Parser* self);
Stmts parser_operation(Parser* self);
Exprs parser_expr_primary(Parser* self);


Exprs parser_expr_bp(Parser* self, int min_prec) {
    Exprs left = parser_expr_primary(self);

    while (1) {
        LexerToken tok = parser_current(self);
        int prec = parser_precedence(tok.tag);
        if (prec == -1 || prec < min_prec) break;

        LexerTokenTag op = tok.tag;
        parser_advance(self);

        Exprs* l = malloc(sizeof(Exprs));
        Exprs* r = malloc(sizeof(Exprs));
        *l = left;
        *r = parser_expr_bp(self, prec + 1);

        left = (Exprs){
            .tag = Expr_BinaryOps,
            .data = { .binary_ops = { .left = l, .op = op, .right = r }}
        };
    }

    return left;
}

Exprs parser_expr(Parser* self) {
    return parser_expr_bp(self, 0);
}

Exprs parser_expr_primary(Parser* self) {
    LexerToken tok = parser_current(self);

    if (tok.tag == Minuss) {
        parser_advance(self);
        Exprs* operand = malloc(sizeof(Exprs));
        *operand = parser_expr_primary(self);

        return (Exprs){
            .tag = Expr_Unary,
            .data = { .unary = { .op = Minuss, .operand = operand }}
        };
    }

    if (tok.tag == Nots) {
        parser_advance(self);
        Exprs* operand = malloc(sizeof(Exprs));
        *operand = parser_expr_primary(self);

        return (Exprs){
            .tag = Expr_Unary,
            .data = { 
                .unary = { 
                    .op = Nots, 
                    .operand = operand 
                }
            }
        };
    }

    if (tok.tag == Tildes) {
        parser_advance(self);
        Exprs* operand = malloc(sizeof(Exprs));
        *operand = parser_expr_primary(self);
        return (Exprs){
            .tag = Expr_Unary,
            .data = { 
                .unary = { 
                    .op = Tildes, 
                    .operand = operand 
                }
            }
        };
    }

    if (tok.tag == LeftParens) { parser_advance(self);
        Exprs inner = parser_expr(self);

        if (!parser_expect(self, RightParens)) parser_sync(self);
        return inner;
    }

    if (tok.tag == Strings || tok.tag == Ints  ||
        tok.tag == Floats  || tok.tag == Chars ||
        tok.tag == Trues   || tok.tag == Falses) {
        parser_advance(self);
        return (Exprs){
            .tag = Expr_Literals,
            .data = { .literals = { .range = tok.range }}
        };
    }

    if (tok.tag == Identifier) {
        SourceRange name = tok.range;
        parser_advance(self);

        LexerToken next = parser_current(self);
        if (next.tag == LeftParens || next.tag == LeftBrackets) return parser_function_call(self, name);
        if (next.tag == Dots) return parser_method_calls(self, name);

        return (Exprs){
            .tag = Expr_Identifiers,
            .data = { .identifiers = { .name = name }}
        };
    }

    if (tok.tag == EOFs) {
        parse_error_eof(self);
    } else {
        parse_error(self, ParseErr_UnexpectedToken, "unexpected token in expression", tok.tag);
        parser_advance(self);
    }

    return (Exprs){0};
}

Exprs parser_function_call(Parser* self, SourceRange fn) {
    parser_advance(self);
    RangeArr generic_params = {0};
    ParamArr params = {0};

    if (parser_current(self).tag == LeftBrackets) {
        parser_advance(self);
        while (parser_current(self).tag != RightBrackets && parser_current(self).tag != EOFs) {
            if (parser_current(self).tag == Identifier) {
                ARR_PUSH(generic_params, parser_current(self).range);
                parser_advance(self);
            } else if (parser_current(self).tag == Commas) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_UnexpectedToken, "expected identifier or ',' in generic params", parser_current(self).tag);
                parser_advance(self);
            }
        }
        if (!parser_expect(self, RightBrackets)) parser_sync(self);
    }

    if (parser_current(self).tag == LeftParens) {
        parser_advance(self);
        while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
            Param p = {0};
            if (parser_current(self).tag == Identifier) {
                p.name = parser_current(self).range;
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_ExpectedToken, "expected parameter name", Identifier);
                parser_sync(self);
                break;
            }
            
            if (parser_current(self).tag == Colons) {
                parser_advance(self);
                p.value = parser_expr(self);
            } else {
                p.value = (Exprs){
                    .tag = Expr_Identifiers,
                    .data = { .identifiers = { .name = p.name }}
                };
                p.name = (SourceRange){0};
            }

            p.value = parser_expr(self);
            ARR_PUSH(params, p);
            if (parser_current(self).tag == Commas) parser_advance(self);
        }
        if (!parser_expect(self, RightParens)) parser_sync(self);
    }

    return (Exprs){
        .tag = Expr_Function,
        .data = { .function_call = {
            .name = fn,
            .param = params.data,
            .param_count = params.len,
            .generic_params = generic_params.data,
            .generic_params_count = generic_params.len
        }}
    };
}

Exprs parser_method_calls(Parser* self, SourceRange class) {
    parser_advance(self);
    SourceRange function = {0};
    RangeArr generic_params = {0};
    ParamArr params = {0};

    if (parser_current(self).tag == Identifier) {
        function = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected method name after '.'", Identifier);
    }

    if (parser_current(self).tag == LeftBrackets) {
        parser_advance(self);
        while (parser_current(self).tag != RightBrackets && parser_current(self).tag != EOFs) {
            if (parser_current(self).tag == Identifier) {
                ARR_PUSH(generic_params, parser_current(self).range);
                parser_advance(self);
            } else if (parser_current(self).tag == Commas) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_UnexpectedToken, "expected identifier or ',' in generic params", parser_current(self).tag);
                parser_advance(self);
            }
        }
        if (!parser_expect(self, RightBrackets)) parser_sync(self);
    }

    if (parser_current(self).tag == LeftParens) {
        parser_advance(self);
        while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
            Param p = {0};
            if (parser_current(self).tag == Identifier) {
                p.name = parser_current(self).range;
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_ExpectedToken, "expected parameter name", Identifier);
                parser_sync(self);
                break;
            }

            if (parser_current(self).tag == Colons) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_ExpectedToken, "expected ':' after parameter name", Colons);
            }

            p.value = parser_expr(self);
            ARR_PUSH(params, p);

            if (parser_current(self).tag == Commas) parser_advance(self);
        }
        if (!parser_expect(self, RightParens)) parser_sync(self);
    }

    return (Exprs){
        .tag = Expr_Class_Calls,
        .data = { .class_calls = {
            .name = class,
            .function = function,
            .generic_params = generic_params.data,
            .generic_params_count = generic_params.len,
            .param = params.data,
            .param_count = params.len
        }}
    };
}

Exprs parser_struct_call(Parser* self, SourceRange str) {
    parser_advance(self);
    RangeArr generic_params = {0};
    SourceRange function = {0};
    ParamArr params = {0};

    if (parser_current(self).tag == LeftBrackets) {
        parser_advance(self);
        while (parser_current(self).tag != RightBrackets && parser_current(self).tag != EOFs) {
            if (parser_current(self).tag == Identifier) {
                ARR_PUSH(generic_params, parser_current(self).range);
                parser_advance(self);
            } else if (parser_current(self).tag == Commas) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_UnexpectedToken, "expected identifier or ',' in generic params", parser_current(self).tag);
                parser_advance(self);
            }
        }
        if (!parser_expect(self, RightBrackets)) parser_sync(self);
    }

    if (parser_current(self).tag == Dots) {
        parser_advance(self);
        if (parser_current(self).tag == Identifier) {
            function = parser_current(self).range;
            parser_advance(self);
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected field name after '.'", Identifier);
        }
    }

    if (parser_current(self).tag == LeftBraces) {
        parser_advance(self);
        while (parser_current(self).tag != RightBraces && parser_current(self).tag != EOFs) {
            Param p = {0};
            if (parser_current(self).tag == Identifier) {
                p.name = parser_current(self).range;
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_ExpectedToken, "expected field name in struct literal", Identifier);
                parser_sync(self);
                break;
            }
            if (parser_current(self).tag == Colons) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_ExpectedToken, "expected ':' after field name", Colons);
            }
            p.c_type = parser_type(self).data.custom.name;
            ARR_PUSH(params, p);
            if (parser_current(self).tag == Commas) parser_advance(self);
        }
        if (!parser_expect(self, RightBraces)) parser_sync(self);
    }

    return (Exprs){
        .tag = Expr_Struct_Calls,
        .data = { .struct_calls = {
            .name = str,
            .function = function,
            .param = params.data,
            .param_count = params.len,
            .generic_params = generic_params.data,
            .generic_params_count = generic_params.len
        }}
    };
}

Exprs parser_enums_call(Parser* self, SourceRange en) {
    parser_advance(self);
    RangeArr generic_params = {0};
    ParamArr params = {0};
    SourceRange field = {0};

    if (parser_current(self).tag == LeftBrackets) {
        parser_advance(self);
        while (parser_current(self).tag != RightBrackets && parser_current(self).tag != EOFs) {
            if (parser_current(self).tag == Identifier) {
                ARR_PUSH(generic_params, parser_current(self).range);
                parser_advance(self);
            } else if (parser_current(self).tag == Commas) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_UnexpectedToken, "expected identifier or ',' in generic params", parser_current(self).tag);
                parser_advance(self);
            }
        }
        if (!parser_expect(self, RightBrackets)) parser_sync(self);
    }

    if (parser_current(self).tag == Dots) {
        parser_advance(self);
        if (parser_current(self).tag == Identifier) {
            field = parser_current(self).range;
            parser_advance(self);
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected enum variant name after '.'", Identifier);
        }
    }

    if (parser_current(self).tag == LeftParens) {
        parser_advance(self);
        while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
            Param p = {0};
            if (parser_current(self).tag == Identifier) {
                p.name = parser_current(self).range;
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_ExpectedToken, "expected parameter name in enum call", Identifier);
                parser_sync(self);
                break;
            }
            if (parser_current(self).tag == Colons) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_ExpectedToken, "expected ':' after parameter name", Colons);
            }
            p.c_type = parser_type(self).data.custom.name;
            ARR_PUSH(params, p);
            if (parser_current(self).tag == Commas) parser_advance(self);
        }
        if (!parser_expect(self, RightParens)) parser_sync(self);
    }

    return (Exprs){
        .tag = Expr_Enum_Calls,
        .data = { .enum_calls = {
            .name = en,
            .field = field,
            .param = params.data,
            .param_count = params.len,
            .generic_params = generic_params.data,
            .generic_params_count = generic_params.len
        }}
    };
}

Type parser_type(Parser* self) {
    if (parser_current(self).tag == Stars) {
        parser_advance(self);

        if (parser_current(self).tag == Stars) {
            parser_advance(self);

            Type* inner = malloc(sizeof(Type));
            *inner = parser_type(self);

            return (Type){ .tag = Type_RawPtr, .data.raw_ptr.inner = inner };
        }

        Type* inner = malloc(sizeof(Type));
        *inner = parser_type(self);

        return (Type){ .tag = Type_Ptr, .data.ptr.inner = inner };
    }

    if (parser_current(self).tag == Functions) {
        parser_advance(self);

        Type* params = NULL;
        size_t params_count = 0;

        if (parser_current(self).tag == LeftParens) {
            parser_advance(self);

            size_t cap = 0;
            while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
                if (params_count == cap) { cap = cap ? cap * 2 : 4; params = realloc(params, cap * sizeof(Type)); }
                params[params_count++] = parser_type(self);
                if (parser_current(self).tag == Commas) parser_advance(self);
            }

            if (!parser_expect(self, RightParens)) parser_sync(self);
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected '(' after 'fn' in function type", LeftParens);
        }

        Type ret = { .tag = Type_Void };
        if (parser_current(self).tag == Colons) { parser_advance(self); ret = parser_type(self); }

        Type* ret_heap = malloc(sizeof(Type));
        *ret_heap = ret;

        return (Type){
            .tag = Type_FnPtr,
            .data.fn_ptr = {
                .ret = ret_heap,
                .params = params,
                .params_count = params_count,
            }
        };
    }

    switch (parser_current(self).tag) {
        case Ints: {
            SourceRange r = parser_current(self).range;
            int bits = range_eq(r, "int8")  ? 8  : range_eq(r, "int16") ? 16 : range_eq(r, "int64") ? 64 : 32;
            parser_advance(self);

            Type base = (Type){
                .tag = Type_Int,
                .data.int_t.bits = bits
            };

            if (parser_current(self).tag == LeftBrackets) {
                parser_advance(self);
                size_t len = 0;
                if (parser_current(self).tag == Ints) {
                    len = (size_t)parser_current(self).data.value_int;
                    parser_advance(self);
                } else {
                    parse_error(self, ParseErr_ExpectedToken, "expected array length", Ints);
                }
                if (!parser_expect(self, RightBrackets)) parser_sync(self);

                Type* inner = malloc(sizeof(Type));
                *inner = base;

                return (Type){ .tag = Type_Array, .data.array_t.inner = inner, .data.array_t.len = len };
            }

            return base;
        }

        case Floats: {
            SourceRange r = parser_current(self).range;
            int bits = range_eq(r, "float64") ? 64 : 32;
            parser_advance(self);

            Type base = (Type){
                .tag = Type_Float,
                .data.float_t.bits = bits
            };

            if (parser_current(self).tag == LeftBrackets) {
                parser_advance(self);
                if (parser_current(self).tag == Ints) {
                    parser_advance(self);
                } else {
                    parse_error(self, ParseErr_ExpectedToken, "expected array length", Ints);
                }
                if (!parser_expect(self, RightBrackets)) parser_sync(self);

                Type* inner = malloc(sizeof(Type));
                *inner = base;

                return (Type){ .tag = Type_Array, .data.array_t.inner = inner };
            }
            return base;
        }

        case Chars:   { parser_advance(self); return (Type){ .tag = Type_Char }; }
        case Strings: { parser_advance(self); return (Type){ .tag = Type_Str  }; }
        case Trues:
        case Falses:  { parser_advance(self); return (Type){ .tag = Type_Bool }; }

        case Identifier: {
            SourceRange r = parser_current(self).range;
            parser_advance(self);

            if (range_eq(r, "bool")) return (Type){ .tag = Type_Bool };
            if (range_eq(r, "void")) return (Type){ .tag = Type_Void };
            if (range_eq(r, "str"))  return (Type){ .tag = Type_Str  };
            if (range_eq(r, "char")) return (Type){ .tag = Type_Char };

            Type base = (Type){ .tag = Type_Custom, .data.custom.name = r };

            if (parser_current(self).tag == LeftBrackets) {
                parser_advance(self);
                if (parser_current(self).tag == Ints) {
                    parser_advance(self);
                } else {
                    parse_error(self, ParseErr_ExpectedToken, "expected array length", Ints);
                }
                if (!parser_expect(self, RightBrackets)) parser_sync(self);

                Type* inner = malloc(sizeof(Type));
                *inner = base;
                return (Type){ .tag = Type_Array, .data.array_t.inner = inner };
            }

            return base;
        }

        default: {
            if (parser_current(self).tag == EOFs) {
                parse_error_eof(self);
            } else {
                parse_error(self, ParseErr_InvalidType, "expected a type", parser_current(self).tag);
                parser_advance(self);
            }
            return (Type){ .tag = Type_Void };
        }
    }
}

Stmts parser_functions(Parser* self, bool is_const, bool is_unsafe, bool is_pub) {
    parser_advance(self);

    SourceRange n = {0};
    Type return_type = { .tag = Type_Void };
    ParamArr params = {0};
    StmtsArr body = {0};
    RangeArr generic_params = {0};

    if (parser_current(self).tag == Identifier) {
        n = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected function name", Identifier);
    }

    if (parser_current(self).tag == LeftBrackets) {
        parser_advance(self);
        while (parser_current(self).tag != RightBrackets && parser_current(self).tag != EOFs) {
            if (parser_current(self).tag == Identifier) {
                ARR_PUSH(generic_params, parser_current(self).range);
                parser_advance(self);
            } else if (parser_current(self).tag == Commas) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_UnexpectedToken, "expected identifier or ',' in generic params", parser_current(self).tag);
                parser_advance(self);
            }
        }
        if (!parser_expect(self, RightBrackets)) parser_sync(self);
    }

    if (parser_current(self).tag == LeftParens) {
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected '(' in function parameters", LeftParens);
    }

    while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
        Param p = {0};
        if (parser_current(self).tag == Identifier) {
            p.name = parser_current(self).range;
            parser_advance(self);
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected parameter name", Identifier);
            parser_sync(self);
            break;
        }
        if (parser_current(self).tag == Colons) {
            parser_advance(self);
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected ':' after parameter name", Colons);
        }
        
        SourceRange type_start = parser_current(self).range;
        Type param_type = parser_type(self);
        p.c_type = param_type.tag == Type_Custom ? param_type.data.custom.name : type_start;
        ARR_PUSH(params, p);
        if (parser_current(self).tag == Commas) parser_advance(self);
    }

    if (!parser_expect(self, RightParens)) parser_sync(self);
    if (parser_current(self).tag == Colons) {
        parser_advance(self);
        return_type = parser_type(self);
    }

    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        Stmts s = parser_stmt(self);
        if (s.tag != 0) ARR_PUSH(body, s);
    }
    if (!parser_expect(self, Ends)) parser_sync(self);

    SourceRange ret_range = return_type.tag == Type_Custom ? return_type.data.custom.name : (SourceRange){0};

    return (Stmts){
        .tag = Stmt_Functions,
        .data = { .functions = {
            .name = n,
            .params = params.data,
            .params_count = params.len,
            .generic_params = generic_params.data,
            .generic_params_count = generic_params.len,
            .return_type      = ret_range,
            .return_type_tag  = return_type.tag,
            .return_type_bits = return_type.tag == Type_Int ? return_type.data.int_t.bits : return_type.tag == Type_Float ? return_type.data.float_t.bits : 0,
            .body = body.data,
            .body_count = body.len,
            .is_unsafe = is_unsafe,
            .is_pub = is_pub,
        }}
    };
}

Stmts parser_class(Parser* self, bool is_pub) {
    parser_advance(self);

    SourceRange n = {0};
    SourceRange parent = {0};
    ParamArr class_params = {0};
    StructParamArr fields = {0};
    MethodArr methods = {0};
    RangeArr generic_params = {0};
    RangeArr traits = {0};

    if (parser_current(self).tag == Fors) {
        parser_advance(self);
        if (parser_current(self).tag != Identifier) {
            parse_error(self, ParseErr_ExpectedToken, "expected trait name after 'for'", Identifier);
        }
        while (parser_current(self).tag == Identifier) {
            ARR_PUSH(traits, parser_current(self).range);
            parser_advance(self);
            if (parser_current(self).tag == Commas) parser_advance(self);
        }
    }

    if (parser_current(self).tag == Identifier) {
        n = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected class name", Identifier);
    }

    if (parser_current(self).tag == LeftBrackets) {
        parser_advance(self);
        while (parser_current(self).tag != RightBrackets && parser_current(self).tag != EOFs) {
            if (parser_current(self).tag == Identifier) {
                ARR_PUSH(generic_params, parser_current(self).range);
                parser_advance(self);
            } else if (parser_current(self).tag == Commas) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_UnexpectedToken, "expected identifier or ',' in generic params", parser_current(self).tag);
                parser_advance(self);
            }
        }
        if (!parser_expect(self, RightBrackets)) parser_sync(self);
    }

    if (parser_current(self).tag == LeftParens) {
        parser_advance(self);
        while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
            Param p = {0};
            if (parser_current(self).tag == Identifier) {
                p.name = parser_current(self).range;
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_ExpectedToken, "expected parameter name in class constructor", Identifier);
                parser_sync(self);
                break;
            }
            if (parser_current(self).tag == Colons) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_ExpectedToken, "expected ':' after parameter name", Colons);
            }
            p.c_type = parser_type(self).data.custom.name;
            ARR_PUSH(class_params, p);
            if (parser_current(self).tag == Commas) parser_advance(self);
        }
        if (!parser_expect(self, RightParens)) parser_sync(self);
    }

    if (parser_current(self).tag == Greaters) {
        parser_advance(self);
        if (parser_current(self).tag == Identifier) {
            parent = parser_current(self).range;
            parser_advance(self);
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected parent class name after '>'", Identifier);
        }
    }

    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        if (parser_current(self).tag == Vars || parser_current(self).tag == Lets) {
            StructParam f = {0};
            parser_advance(self);

            if (parser_current(self).tag == Identifier) {
                f.name = parser_current(self).range;
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_ExpectedToken, "expected field name", Identifier);
            }
            if (parser_current(self).tag == Colons) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_ExpectedToken, "expected ':' after field name", Colons);
            }
            f.c_type = parser_type(self).data.custom.name;
            ARR_PUSH(fields, f);

        } else if (parser_current(self).tag == Functions) {
            Stmts fn = parser_functions(self, false, false, false);
            FunctionMethod m = {0};
            m.name = fn.data.functions.name;
            m.params = fn.data.functions.params;
            m.params_count = fn.data.functions.params_count;
            m.body = fn.data.functions.body;
            m.body_count = fn.data.functions.body_count;
            m.is_pub = fn.data.functions.is_pub;
            m.is_unsafe = fn.data.functions.is_unsafe;
            ARR_PUSH(methods, m);

        } else if (parser_current(self).tag == Ats) {
            parser_advance(self);
            FunctionMethod m = {0};

            if (parser_current(self).tag != Identifier) {
                parse_error(self, ParseErr_ExpectedToken, "expected annotation name after '@'", Identifier);
                parser_advance(self);
                continue;
            }
            if (!range_eq(parser_current(self).range, "operation")) {
                parse_error(self, ParseErr_UnexpectedToken, "unknown annotation; expected 'operation'", parser_current(self).tag);
                parser_advance(self);
                continue;
            }

            Operation op = operation_op(self);

            if (parser_current(self).tag != Functions) {
                parse_error(self, ParseErr_ExpectedToken, "expected 'fn' after @operation(...)", Functions);
                continue;
            }
            Stmts fn = parser_functions(self, false, false, false);

            m.name = fn.data.functions.name;
            m.params = fn.data.functions.params;
            m.params_count = fn.data.functions.params_count;
            m.body = fn.data.functions.body;
            m.body_count = fn.data.functions.body_count;
            m.is_pub = fn.data.functions.is_pub;
            m.is_unsafe = fn.data.functions.is_unsafe;
            m.operation = op;
            m.operation.function = fn.data.functions.name;
            ARR_PUSH(methods, m);

        } else {
            parse_error(self, ParseErr_UnexpectedToken, "unexpected token in class body; expected 'var', 'let', 'fn', or '@'", parser_current(self).tag);
            parser_advance(self);
        }
    }
    if (!parser_expect(self, Ends)) parser_sync(self);

    return (Stmts){
        .tag = Stmt_Classes,
        .data = { .classes = {
            .name = n,
            .class_params = class_params.data,
            .class_params_count = class_params.len,
            .fields = fields.data,
            .fields_count = fields.len,
            .methods = methods.data,
            .methods_count = methods.len,
            .parent = parent,
            .generic_params = generic_params.data,
            .generic_params_count = generic_params.len,
            .traits = traits.data,
            .traits_count = traits.len,
            .is_pub = is_pub,
            .attached_tag = ClassAttach_None,
            .attached_fields = NULL,
            .attached_fields_count = 0,
        }}
    };
}

Stmts parser_structer(Parser* self, bool is_pub, bool is_unsafe) {
    parser_advance(self);

    RangeArr generic_params = {0};
    SourceRange n = {0};
    StructParamArr fields = {0};

    if (parser_current(self).tag == Identifier) {
        n = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected struct name", Identifier);
    }

    if (parser_current(self).tag == LeftBrackets) {
        parser_advance(self);
        while (parser_current(self).tag != RightBrackets && parser_current(self).tag != EOFs) {
            if (parser_current(self).tag == Identifier) {
                ARR_PUSH(generic_params, parser_current(self).range);
                parser_advance(self);
            } else if (parser_current(self).tag == Commas) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_UnexpectedToken, "expected identifier or ',' in generic params", parser_current(self).tag);
                parser_advance(self);
            }
        }
        if (!parser_expect(self, RightBrackets)) parser_sync(self);
    }

    if (parser_current(self).tag == Colons) {
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected ':' after struct name", Colons);
    }

    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        StructParam f = {0};
        switch (parser_current(self).tag) {
            case Vars: {
                f.mode = (VarMode){ .tag = VarMode_Value, .mutability = Mutability_Mutable };
                parser_advance(self);

                if (parser_current(self).tag == Identifier) {
                    f.name = parser_current(self).range;
                    parser_advance(self);
                } else {
                    parse_error(self, ParseErr_ExpectedToken, "expected field name after 'var'", Identifier);
                }
                if (parser_current(self).tag == Equalss) {
                    parser_advance(self);
                } else {
                    parse_error(self, ParseErr_ExpectedToken, "expected '=' after field name", Equalss);
                }
                f.c_type = parser_type(self).data.custom.name;
                ARR_PUSH(fields, f);
                break;
            }
            case Identifier: {
                f.mode = (VarMode){ .tag = VarMode_Value, .mutability = Mutability_Immutable };
                f.name = parser_current(self).range;
                parser_advance(self);

                if (parser_current(self).tag == Equalss) {
                    parser_advance(self);
                } else {
                    parse_error(self, ParseErr_ExpectedToken, "expected '=' after field name", Equalss);
                }
                f.c_type = parser_type(self).data.custom.name;
                ARR_PUSH(fields, f);
                break;
            }
            default: {
                parse_error(self, ParseErr_UnexpectedToken, "unexpected token in struct body; expected field declaration", parser_current(self).tag);
                parser_advance(self);
                break;
            }
        }
    }
    if (!parser_expect(self, Ends)) parser_sync(self);

    return (Stmts){
        .tag = Stmt_Structs,
        .data = { .structs = {
            .name = n,
            .generic_params = generic_params.data,
            .generic_params_count = generic_params.len,
            .fields = fields.data,
            .fields_count = fields.len,
            .is_pub = is_pub,
        }}
    };
}

Stmts parser_enums(Parser* self, bool is_pub, bool is_unsafe) {
    parser_advance(self);

    VariantArr variants = {0};
    SourceRange n = {0};
    RangeArr generic_params = {0};

    if (parser_current(self).tag == Identifier) {
        n = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected enum name", Identifier);
    }

    if (parser_current(self).tag == LeftBrackets) {
        parser_advance(self);
        while (parser_current(self).tag != RightBrackets && parser_current(self).tag != EOFs) {
            if (parser_current(self).tag == Identifier) {
                ARR_PUSH(generic_params, parser_current(self).range);
                parser_advance(self);
            } else if (parser_current(self).tag == Commas) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_UnexpectedToken, "expected identifier or ',' in generic params", parser_current(self).tag);
                parser_advance(self);
            }
        }
        if (!parser_expect(self, RightBrackets)) parser_sync(self);
    }

    if (parser_current(self).tag == Colons) {
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected ':' after enum name", Colons);
    }

    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        EnumVariant v = {0};
        v.fields = NULL;
        v.fields_count = 0;

        if (parser_current(self).tag == Identifier) {
            v.name = parser_current(self).range;
            parser_advance(self);

            if (parser_current(self).tag == LeftParens) {
                parser_advance(self);
                EnumFieldArr fields = {0};

                while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
                    SourceRange fname = {0};
                    SourceRange ftype = {0};
                    if (parser_current(self).tag == Identifier) {
                        fname = parser_current(self).range;
                        parser_advance(self);
                    } else {
                        parse_error(self, ParseErr_ExpectedToken, "expected field name in enum variant", Identifier);
                        parser_sync(self);
                        break;
                    }
                    if (parser_current(self).tag == Colons) {
                        parser_advance(self);
                    } else {
                        parse_error(self, ParseErr_ExpectedToken, "expected ':' after enum field name", Colons);
                    }
                    ftype = parser_type(self).data.custom.name;
                    ARR_PUSH(fields, ((EnumField){ fname, ftype }));
                    if (parser_current(self).tag == Commas) parser_advance(self);
                }
                if (!parser_expect(self, RightParens)) parser_sync(self);

                v.fields = fields.data;
                v.fields_count = fields.len;
            }
            ARR_PUSH(variants, v);
        } else if (parser_current(self).tag == Commas) {
            parser_advance(self);
        } else {
            parse_error(self, ParseErr_UnexpectedToken, "unexpected token in enum body; expected variant name", parser_current(self).tag);
            parser_advance(self);
        }
    }
    if (!parser_expect(self, Ends)) parser_sync(self);

    return (Stmts){
        .tag = Stmt_Enums,
        .data = { .enums = {
            .name = n,
            .variants = variants.data,
            .generic_params = generic_params.data,
            .generic_params_count = generic_params.len,
            .variants_count = variants.len,
            .is_pub = is_pub,
        }}
    };
}

Stmts parser_traits(Parser* self, bool is_pub, bool is_unsafe) {
    parser_advance(self);

    TraitMethodArr methods = {0};
    RangeArr types = {0};
    SourceRange n = {0};
    RangeArr generic_params = {0};

    if (parser_current(self).tag == Identifier) {
        n = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected trait name", Identifier);
    }

    if (parser_current(self).tag == LeftBrackets) {
        parser_advance(self);
        while (parser_current(self).tag != RightBrackets && parser_current(self).tag != EOFs) {
            if (parser_current(self).tag == Identifier) {
                ARR_PUSH(generic_params, parser_current(self).range);
                parser_advance(self);
            } else if (parser_current(self).tag == Commas) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_UnexpectedToken, "expected identifier or ',' in generic params", parser_current(self).tag);
                parser_advance(self);
            }
        }
        if (!parser_expect(self, RightBrackets)) parser_sync(self);
    }

    if (parser_current(self).tag == Colons) {
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected ':' after trait name", Colons);
    }

    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        switch (parser_current(self).tag) {
            case Types: {
                parser_advance(self);
                if (parser_current(self).tag == Identifier) {
                    ARR_PUSH(types, parser_current(self).range);
                    parser_advance(self);
                } else {
                    parse_error(self, ParseErr_ExpectedToken, "expected associated type name after 'type'", Identifier);
                }
                break;
            }
            case Functions: {
                Stmts fn = parser_functions(self, false, false, false);
                TraitMethod m = {0};
                m.name = fn.data.functions.name;
                m.params = fn.data.functions.params;
                m.params_count = fn.data.functions.params_count;
                m.body = fn.data.functions.body;
                m.body_count = fn.data.functions.body_count;
                m.is_pub = fn.data.functions.is_pub;
                m.return_type = fn.data.functions.return_type;
                ARR_PUSH(methods, m);
                break;
            }
            default: {
                parse_error(self, ParseErr_UnexpectedToken, "unexpected token in trait body; expected 'fn' or 'type'", parser_current(self).tag);
                parser_advance(self);
                break;
            }
        }
    }
    if (!parser_expect(self, Ends)) parser_sync(self);
    free(types.data);

    return (Stmts){
        .tag = Stmt_Traits,
        .data = { .traits = {
            .name = n,
            .methods = methods.data,
            .methods_count = methods.len,
            .is_pub = is_pub,
        }}
    };
}

Stmts parser_return(Parser* self) {
    SourceRange range = parser_current(self).range;
    parser_advance(self);
    Exprs value = {0};

    if (parser_current(self).tag != Ends &&
        parser_current(self).tag != Semicolons &&
        parser_current(self).tag != EOFs) {
        value = parser_expr(self);
    }

    return (Stmts){
        .tag = Stmt_Returns,
        .data = { .returns = {
            .expr = value,
            .range = range
        }}
    };
}
Stmts parser_vars(Parser* self) {
    parser_advance(self);

    SourceRange start = parser_current(self).range;
    SourceRange n = {0};
    Type var_type = {0};
    Exprs var_value = {0};

    if (parser_current(self).tag == Identifier) {
        n = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected variable name after 'var'", Identifier);
    }

    if (parser_current(self).tag == Colons) {
        parser_advance(self);
        var_type = parser_type(self);
    }

    if (parser_current(self).tag == Equalss) {
        parser_advance(self);
        var_value = parser_expr(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected '=' in variable declaration", Equalss);
    }

    return (Stmts) {
        .tag = Stmt_Vars,
        .data.vars = {
            .name = n,
            .c_type = var_type.tag == Type_Custom ? var_type.data.custom.name : (SourceRange){0},
            .value = var_value,
            .range = { .start = start.start, .end = n.end, .file_id = start.file_id },
        }
    };
}

Stmts parser_lets(Parser* self) {
    SourceRange start = parser_current(self).range;
    parser_advance(self);

    SourceRange t = {0};
    SourceRange n = {0};
    Exprs var_value = {0};

    if (parser_current(self).tag == Identifier) {
        n = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected binding name after 'let'", Identifier);
    }

    if (parser_current(self).tag == Colons) {
        parser_advance(self);
        t = parser_current(self).range;
        parser_advance(self);
    }

    if (parser_current(self).tag == Equalss) {
        parser_advance(self);
        var_value = parser_expr(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected '=' in let declaration", Equalss);
    }

    return (Stmts) {
        .tag = Stmt_Lets,
        .data.lets = {
            .name = n,
            .c_type = t,
            .value = var_value,
            .range = {
                .start = start.start,
                .end = n.end,
                .file_id = start.file_id
            },
        }
    };
}

Stmts parser_const(Parser* self) {
    parser_advance(self);

    SourceRange start = parser_current(self).range;
    SourceRange t = {0};
    SourceRange n = {0};
    Exprs var_value = {0};

    if (parser_current(self).tag == Identifier) {
        n = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected constant name after 'const'", Identifier);
    }

    if (parser_current(self).tag == Colons) {
        parser_advance(self);
        t = parser_current(self).range;
        parser_advance(self);
    }

    if (parser_current(self).tag == Equalss) {
        parser_advance(self);
        var_value = parser_expr(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected '=' in const declaration", Equalss);
    }

    return (Stmts) {
        .tag = Stmt_Consts,
        .data.consts = {
            .name = n,
            .c_type = t,
            .value = var_value,
            .range = {
                .start = start.start,
                .end = n.end,
                .file_id = start.file_id
            },
        }
    };
}

Stmts parser_globle(Parser* self, bool is_pub, bool is_const) {
    parser_advance(self);

    SourceRange t = {0};
    SourceRange n = {0};
    Exprs var_value = {0};

    if (parser_current(self).tag == Identifier) {
        n = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected global variable name", Identifier);
    }

    if (parser_current(self).tag == Colons) {
        parser_advance(self);
        SourceRange type_start = parser_current(self).range;
        Type g_type = parser_type(self);

        t = g_type.tag == Type_Custom ? g_type.data.custom.name : type_start;
    }


    if (parser_current(self).tag == Equalss) {
        parser_advance(self);
        var_value = parser_expr(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected '=' in global declaration", Equalss);
    }

    return (Stmts) {
        .tag = Stmt_Locals,
        .data.vars = {
            .name = n,
            .c_type = t,
            .value = var_value,
            .mode = is_pub,
        }
    };
}

Stmts operation_atom(Parser* self) {
    parser_advance(self);
    uint64_t val = 0;
    bool _signed = false;

    if (parser_current(self).tag != LeftParens) {
        parse_error(self, ParseErr_ExpectedToken, "expected '(' after @atom", LeftParens);
        return (Stmts){0};
    }
    parser_advance(self);

    if (parser_current(self).tag != Identifier || !range_eq(parser_current(self).range, "bits")) {
        parse_error(self, ParseErr_ExpectedToken, "expected 'bits' in @atom(...)", Identifier);
        return (Stmts){0};
    }
    parser_advance(self);

    if (parser_current(self).tag != Equalss) {
        parse_error(self, ParseErr_ExpectedToken, "expected '=' after 'bits'", Equalss);
        return (Stmts){0};
    }
    parser_advance(self);

    if (parser_current(self).tag != Ints) {
        parse_error(self, ParseErr_ExpectedToken, "expected integer for 'bits' value", Ints);
        return (Stmts){0};
    }
    val = parser_current(self).data.value_int;
    if (val != 2 && val != 4 && val != 8 && val != 16 && val != 32 && val != 64 && val != 128) {
        parse_error(self, ParseErr_InvalidOperation, "bits must be one of: 2, 4, 8, 16, 32, 64, 128", Ints);
        return (Stmts){0};
    }
    parser_advance(self);

    if (parser_current(self).tag != Commas) {
        parse_error(self, ParseErr_ExpectedToken, "expected ',' in @atom(...)", Commas);
        return (Stmts){0};
    }
    parser_advance(self);

    if (parser_current(self).tag != Identifier || !range_eq(parser_current(self).range, "signed")) {
        parse_error(self, ParseErr_ExpectedToken, "expected 'signed' in @atom(...)", Identifier);
        return (Stmts){0};
    }
    parser_advance(self);

    if (parser_current(self).tag != Equalss) {
        parse_error(self, ParseErr_ExpectedToken, "expected '=' after 'signed'", Equalss);
        return (Stmts){0};
    }
    parser_advance(self);

    if (parser_current(self).tag != Trues && parser_current(self).tag != Falses) {
        parse_error(self, ParseErr_ExpectedToken, "expected 'true' or 'false' for 'signed'", Trues);
        return (Stmts){0};
    }
    _signed = parser_current(self).tag == Trues;
    parser_advance(self);

    if (parser_current(self).tag != RightParens) {
        parse_error(self, ParseErr_ExpectedToken, "expected ')' to close @atom(...)", RightParens);
        return (Stmts){0};
    }
    parser_advance(self);

    return (Stmts){0};
}

Operation operation_op(Parser* self) {
    parser_advance(self);
    LexerTokenTag op = 0;

    if (parser_current(self).tag != LeftParens) {
        parse_error(self, ParseErr_ExpectedToken, "expected '(' after @operation", LeftParens);
        return (Operation){0};
    }
    parser_advance(self);

    if (!is_operation(parser_current(self).tag)) {
        parse_error(self, ParseErr_InvalidOperation, "expected a valid operator token inside @operation(...)", parser_current(self).tag);
        return (Operation){0};
    }
    op = parser_current(self).tag;
    parser_advance(self);

    if (parser_current(self).tag != RightParens) {
        parse_error(self, ParseErr_ExpectedToken, "expected ')' to close @operation(...)", RightParens);
        return (Operation){0};
    }
    parser_advance(self);

    return (Operation){
        .op = op,
        .function = {0}
    };
}

Stmts parser_ffi(Parser* self) {
    parser_advance(self);

    SourceRange result = {0};

    if (parser_current(self).tag != LeftParens) {
        parse_error(self, ParseErr_ExpectedToken, "expected '(' after @ffi", LeftParens);
        return (Stmts){0};
    }
    parser_advance(self);

    if (parser_current(self).tag != Strings) {
        parse_error(self, ParseErr_ExpectedToken, "expected string literal in @ffi(...)", Strings);
        return (Stmts){0};
    }
    result = parser_current(self).range;
    parser_advance(self);

    if (parser_current(self).tag != RightParens) {
        parse_error(self, ParseErr_ExpectedToken, "expected ')' to close @ffi(...)", RightParens);
        return (Stmts){0};
    }
    parser_advance(self);

    ExternBlock ex = parser_extern(self);

    return (Stmts){
        .tag = Stmt_Externs,
        .data.externs = {
            .block = ex,
            .ffi = result,
        }
    };
}

Stmts parser_operation(Parser* self) {
    parser_advance(self);

    if (parser_current(self).tag != Identifier) {
        parse_error(self, ParseErr_ExpectedToken, "expected annotation name after '@'", Identifier);
        return (Stmts){0};
    }

    SourceRange range = parser_current(self).range;

    if (range_eq(range, "operation")) { parser_advance(self); operation_op(self);  return (Stmts){0}; }
    if (range_eq(range, "atom"))      { parser_advance(self); operation_atom(self); return (Stmts){0}; }
    if (range_eq(range, "ffi"))       { parser_advance(self); return parser_ffi(self); }

    parse_error(self, ParseErr_UnexpectedToken, "unknown annotation; expected 'operation', 'atom', or 'ffi'", parser_current(self).tag);
    parser_advance(self);
    return (Stmts){0};
}

ExternBlock parser_extern(Parser* self) {
    parser_advance(self);

    SourceRange abi = {0};
    ExternFuncArr funcs = {0};

    if (parser_current(self).tag == Strings) {
        abi = parser_current(self).range;
        parser_advance(self);
    }

    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        if (parser_current(self).tag != Functions) {
            parse_error(self, ParseErr_ExpectedToken, "expected 'fn' in extern block", Functions);
            parser_advance(self);
            continue;
        }
        parser_advance(self);

        ExternFunction fn = {0};

        if (parser_current(self).tag == Identifier) {
            fn.name = parser_current(self).range;
            parser_advance(self);
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected function name in extern declaration", Identifier);
        }

        if (parser_current(self).tag == LeftParens) {
            parser_advance(self);
            ParamArr params = {0};

            while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
                Param p = {0};
                if (parser_current(self).tag == Identifier) {
                    p.name = parser_current(self).range;
                    parser_advance(self);
                } else {
                    parse_error(self, ParseErr_ExpectedToken, "expected parameter name in extern fn", Identifier);
                    parser_sync(self);
                    break;
                }
                if (parser_current(self).tag == Colons) {
                    parser_advance(self);
                } else {
                    parse_error(self, ParseErr_ExpectedToken, "expected ':' after parameter name", Colons);
                }
                p.c_type = parser_type(self).data.custom.name;
                ARR_PUSH(params, p);
                if (parser_current(self).tag == Commas) parser_advance(self);
            }

            if (!parser_expect(self, RightParens)) parser_sync(self);
            fn.params = params.data;
            fn.params_count = params.len;
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected '(' in extern function signature", LeftParens);
        }

        if (parser_current(self).tag == Colons) {
            parser_advance(self);
            fn.return_type = parser_type(self).data.custom.name;
        }

        ARR_PUSH(funcs, fn);
    }

    if (!parser_expect(self, Ends)) parser_sync(self);

    return (ExternBlock){
        .abi = abi,
        .funcs = funcs.data,
        .funcs_count = funcs.len,
    };
}

Stmts parser_match(Parser* self) {
    parser_advance(self);

    Exprs condition = parser_expr(self);
    MatchArmArr arms = {0};

    if (parser_current(self).tag != Colons) {
        parse_error(self, ParseErr_ExpectedToken, "expected ':' after match expression", Colons);
    } else {
        parser_advance(self);
    }

    while (parser_current(self).tag == Cases) {
        parser_advance(self);

        MatchArm arm = {0};
        arm.pattern.tag = Pattern_Wildcard;
        arm.body = NULL;
        arm.body_count = 0;

        Exprs case_expr = parser_expr(self);

        if (parser_current(self).tag == Dos) {
            parser_advance(self);
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected 'do' after case expression", Dos);
        }

        StmtsArr body = {0};
        while (parser_current(self).tag != Cases &&
               parser_current(self).tag != Ends  &&
               parser_current(self).tag != EOFs) {
            Stmts s = parser_stmt(self);
            if (s.tag != 0) ARR_PUSH(body, s);
        }

        arm.body = body.data;
        arm.body_count = body.len;
        ARR_PUSH(arms, arm);
    }

    if (!parser_expect(self, Ends)) parser_sync(self);

    return (Stmts){
        .tag = Stmt_Matchs,
        .data.matchs = {
            .expr = condition,
            .cases = arms.data,
            .cases_count = arms.len,
        }
    };
}

Stmts parser_for(Parser* self) {
    parser_advance(self);

    SourceRange var = {0};
    Exprs condition = {0};
    StmtsArr body = {0};

    if (parser_current(self).tag == Identifier) {
        var = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected loop variable name after 'for'", Identifier);
    }

    if (parser_current(self).tag == Ins) {
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected 'in' after loop variable", Ins);
    }

    condition = parser_expr(self);

    if (parser_current(self).tag == Dos) {
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected 'do' after for-in expression", Dos);
    }

    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        ARR_PUSH(body, parser_stmt(self));
    }

    if (!parser_expect(self, Ends)) parser_sync(self);

    return (Stmts){
        .tag = Stmt_Fors,
        .data.fors = {
            ._var = var,
            .iter = condition,
            .body = body.data,
            .body_count = body.len,
        }
    };
}


Stmts parser_if(Parser* self) {
    parser_advance(self);
    Exprs condition = parser_expr(self);

    if (parser_current(self).tag == Thens) {
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected 'then' after if condition", Thens);
    }

    StmtsArr body = {0};
    while (parser_current(self).tag != Ends &&
           parser_current(self).tag != Elifs &&
           parser_current(self).tag != Elses &&
           parser_current(self).tag != EOFs) {
        Stmts s = parser_stmt(self);
        if (s.tag != 0) ARR_PUSH(body, s);
    }

    StmtsArr else_body = {0};
    if (parser_current(self).tag == Elifs) {
        ARR_PUSH(else_body, parser_elif(self));
    } else if (parser_current(self).tag == Elses) {
        ARR_PUSH(else_body, parser_else(self));
    }

    if (parser_current(self).tag == Ends) parser_advance(self);

    return (Stmts) {
        .tag = Stmt_Ifs,
        .data.ifs = {
            .cond = condition,
            .body = body.data,
            .body_count = body.len,
            .else_body = else_body.data,
            .else_body_count = else_body.len
        }
    };
}

Stmts parser_elif(Parser* self) {
    parser_advance(self);
    Exprs condition = parser_expr(self);

    if (parser_current(self).tag == Thens) {
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected 'then' after elif condition", Thens);
    }

    StmtsArr body = {0};
    while (parser_current(self).tag != Ends &&
           parser_current(self).tag != Elifs &&
           parser_current(self).tag != Elses &&
           parser_current(self).tag != EOFs) {
        Stmts s = parser_stmt(self);
        if (s.tag != 0) ARR_PUSH(body, s);
    }

    StmtsArr else_body = {0};
    if (parser_current(self).tag == Elifs) {
        ARR_PUSH(else_body, parser_elif(self));
    } else if (parser_current(self).tag == Elses) {
        ARR_PUSH(else_body, parser_else(self));
    }

    return (Stmts) {
        .tag = Stmt_Elifs,
        .data.ifs = {
            .cond = condition,
            .body = body.data,
            .body_count = body.len,
            .else_body = else_body.data,
            .else_body_count = else_body.len
        }
    };
}

Stmts parser_else(Parser* self) {
    parser_advance(self);

    if (parser_current(self).tag == Thens) parser_advance(self);

    StmtsArr body = {0};
    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        Stmts s = parser_stmt(self);
        if (s.tag != 0) ARR_PUSH(body, s);
    }

    return (Stmts) {
        .tag = Stmt_Elses,
        .data.elses = {
            .body = body.data,
            .body_count = body.len,
            .range = {0},
        },
    };
}

Stmts parser_stmt(Parser* self) {
    LexerToken tok = parser_current(self);

    if (tok.tag == Publics) {
        parser_advance(self);
        if (parser_current(self).tag == Unsafes) {
            parser_advance(self);
            switch (parser_current(self).tag) {
                case Functions: return parser_functions(self, false, true, true);
                case Structs:   return parser_structer(self, true, true);
                case Enums:     return parser_enums(self, true, true);
                case Traits:    return parser_traits(self, true, true);
                default: {
                    parse_error(self, ParseErr_UnexpectedToken,
                        "expected 'fn', 'struct', 'enum', or 'trait' after 'pub unsafe'",
                        parser_current(self).tag);
                    parser_sync(self);
                    break;
                }
            }
        } else {
            switch (parser_current(self).tag) {
                case Functions: return parser_functions(self, false, false, true);
                case Classes:   return parser_class(self, true);
                case Structs:   return parser_structer(self, true, false);
                case Enums:     return parser_enums(self, true, false);
                case Traits:    return parser_traits(self, true, false);
                default: {
                    parse_error(self, ParseErr_UnexpectedToken,
                        "expected 'fn', 'class', 'struct', 'enum', or 'trait' after 'pub'",
                        parser_current(self).tag);
                    parser_sync(self);
                    break;
                }
            }
        }
    } else if (tok.tag == Unsafes) {
        parser_advance(self);
        switch (parser_current(self).tag) {
            case Functions: return parser_functions(self, false, true, false);
            case Structs:   return parser_structer(self, true, false);
            case Enums:     return parser_enums(self, true, false);
            case Traits:    return parser_traits(self, true, false);
            default: {
                parse_error(self, ParseErr_UnexpectedToken,
                    "expected 'fn', 'struct', 'enum', or 'trait' after 'unsafe'",
                    parser_current(self).tag);
                parser_sync(self);
                break;
            }
        }
    } else if (tok.tag == Consts) {
        parser_advance(self);
        switch (parser_current(self).tag) {
            case Functions: return parser_functions(self, true, false, false);
            case Locals:    return parser_globle(self, false, true);
            default: {
                parse_error(self, ParseErr_UnexpectedToken,
                    "expected 'fn' or 'global' after 'const'",
                    parser_current(self).tag);
                parser_sync(self);
                break;
            }
        }
    } else {
        switch (tok.tag) {
            case Functions: return parser_functions(self, false, false, false);
            case Classes:   return parser_class(self, false);
            case Structs:   return parser_structer(self, false, false);
            case Enums:     return parser_enums(self, false, false);
            case Traits:    return parser_traits(self, false, false);
            case Ifs:       return parser_if(self);
            case Returns:   return parser_return(self);
            case Locals:    return parser_globle(self, false, true);
            case Vars:      return parser_vars(self);
            case Lets:      return parser_lets(self);
            case Consts:    return parser_const(self);
            case Matchs:    return parser_match(self);
            case Fors:      return parser_for(self);
            case Ats:       return parser_operation(self);
            case Externs: {
                ExternBlock ex = parser_extern(self);
                return (Stmts){
                    .tag = Stmt_Externs,
                    .data.externs = { .block = ex }
                };
            }

            case Identifier: {
                SourceRange name = parser_current(self).range;
                parser_advance(self);

                LexerToken next = parser_current(self);

                if (next.tag == Dots) {
                    Exprs obj = (Exprs){
                        .tag = Expr_Identifiers,
                        .data = { .identifiers = { .name = name }}
                    };
                    parser_advance(self);
                    SourceRange method = {0};
                    if (parser_current(self).tag == Identifier) {
                        method = parser_current(self).range;
                        parser_advance(self);
                    }
                    ParamArr args = {0};
                    if (parser_current(self).tag == LeftParens) {
                        parser_advance(self);
                        while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
                            Param p = {0};
                            if (parser_current(self).tag == Identifier) {
                                p.name = parser_current(self).range;
                                parser_advance(self);
                            }
                            if (parser_current(self).tag == Colons) {
                                parser_advance(self);
                                p.value = parser_expr(self);
                            }

                            ARR_PUSH(args, p);

                            if (parser_current(self).tag == Commas) parser_advance(self);
                        }
                        if (!parser_expect(self, RightParens)) parser_sync(self);
                    }
                    Exprs* obj_heap = malloc(sizeof(Exprs));
                    *obj_heap = obj;
                    Exprs call = (Exprs){
                        .tag = Expr_Class_Calls,
                        .data = { .class_calls = {
                            .name = name,
                            .function = method,
                            .param = args.data,
                            .param_count = args.len,
                        }}
                    };
                    return (Stmts){
                        .tag = Stmt_ExprStmt,
                        .data.expr_stmt = { .expr = call }
                    };
                }

                LexerTokenTag op = next.tag;

                switch (op) {
                    case Equalss:
                    case PlusEqualss:
                    case MinusEqualss:
                    case StarEqualss:
                    case SlashEqualss:
                    case PercentEqualss:
                    case AmpersandEqualss:
                    case PipeEqualss:
                    case CaretEqualss:
                    case LeftShiftEqualss:
                    case RightShiftEqualss: {
                        parser_advance(self);
                        Exprs value = parser_expr(self);
                        return (Stmts){
                            .tag = Stmt_Assigns,
                            .data.assigns = {
                                .target = (Exprs){
                                    .tag = Expr_Identifiers,
                                    .data = { .identifiers = { .name = name }}
                                },
                                .op = op,
                                .value = value,
                            }
                        };
                    }

                    case LeftParens:
                    case LeftBrackets: {
                        Exprs call = parser_function_call(self, name);
                        return (Stmts){
                            .tag = Stmt_ExprStmt,
                            .data.expr_stmt = { .expr = call }
                        };
                    }

                    default: {
                        Exprs ident = (Exprs){
                            .tag = Expr_Identifiers,
                            .data = { .identifiers = { .name = name }}
                        };
                        return (Stmts){
                            .tag = Stmt_ExprStmt,
                            .data.expr_stmt = { .expr = ident }
                        };
                    }
                }
            }

            case EOFs: {
                parse_error_eof(self);
                break;
            }

            default: {
                parse_error(self, ParseErr_UnexpectedToken, "unexpected token; expected a statement", tok.tag);
                parser_sync(self);
                break;
            }
        }
    }
    return (Stmts){0};
}