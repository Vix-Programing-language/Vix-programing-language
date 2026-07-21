#include "parser.h"
#include "lexer.h"
#include "helper.h"

LexerToken parser_current(Parser* self);


SourceRange expr_get_range(Exprs expr) {
    switch (expr.tag) {
        case Expr_Identifiers: return expr.data.identifiers.name;
        case Expr_Field:       return expr.data.field_access.field;
        case Expr_Literals:    return expr.data.literals.range;
        case Expr_Idx:         return expr.data.idx.range;
        case Expr_Class_Calls: return expr_get_range(*expr.data.class_calls.object);
        default:               return (SourceRange){0};
    }
}

int parser_precedence(LexerTokenTag tag) {
    switch (tag) {
        case Equalss: 
        case PlusEqualss: 
        case MinusEqualss: 
        case StarEqualss: 
        case SlashEqualss:
        case PercentEqualss:
        case PipeEqualss:
        case AmpersandEqualss:
        case CaretEqualss:
        case LeftShiftEqualss:
        case RightShiftEqualss:                          return 1; 
        
        case Ors:                                        return 2;
        case Ands:                                       return 3;
        case Pipes:                                      return 4;
        case Carets:                                     return 5;
        case Ampersands:                                 return 6;
        case DoubleEqualss: case NotEqualss:             return 7;
        case Lesses: 
        case Greaters:
        case LessEqualss: case GreaterEqualss:           return 8;
        case LeftShifts: case RightShifts:               return 9;
        case Plus: case Minuss:                          return 10;
        case Stars: case Slashs: case Percents:          return 11;
        case LeftBrackets:                               return 12;
        case Dots:                                       return 13;
        default:                                         return -1;
    }
}

Exprs parser_literal(Parser* self, SourceRange name) {
    parser_advance(self);

    ParamArr fields = {0};

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

        if (parser_current(self).tag == Equalss) parser_advance(self); else parse_error(self, ParseErr_ExpectedToken, "expected '=' after field name", Equalss);
    

        p.value = parser_expr(self);

        if (p.value.tag == Expr_Cast && p.value.data.cast.ty) {
            p.type = *p.value.data.cast.ty;
        } else if (p.value.tag == Expr_Identifiers) {
            p.type = (Type){ .tag = Type_Custom, .data.custom.name = p.value.data.identifiers.name };
        }

        ARR_PUSH(fields, p);

        if (parser_current(self).tag == Commas) parser_advance(self);
    }

    EXPECT(RightBraces, "expected closing '}' after structural initializer");

    return (Exprs){
        .tag = Expr_Struct_Calls,
        .data = { .struct_calls = {
            .name = name,
            .function = (SourceRange){0},
            .param = fields.data,
            .param_count = fields.len,
            .generic_params = NULL,
            .generic_params_count = 0,
        }}
    };
}

static bool parser_is_generic_invocation(Parser* self) {
    if (parser_current(self).tag != Lesses) {
        return false;
    }

    Lexer lookahead = self->lexer;
    lexer_advance(&lookahead);

    int depth = 1;

    while (depth > 0) {
        LexerToken tok = lexer_peek(&lookahead);
        if (tok.tag == EOFs) return false;
        switch (tok.tag) {
            case Ands: 
            case Ors: case Thens: case Dos: case Semicolons:
            case Equalss: 
            case Plus: case Minuss: case Stars: case Slashs:
            case Ifs: 
            case Elses: case Whiles: case Returns: case Commas:
                if (tok.tag != Commas) return false;
                break;
            default:
                break;
        }

        if (tok.tag == Lesses) {
            depth++;
        } else if (tok.tag == Greaters) {
            depth--;
            if (depth == 0) {
                lexer_advance(&lookahead);
                LexerToken follow = lexer_peek(&lookahead);
                return (follow.tag == LeftParens || follow.tag == LeftBraces);
            }
        } else if (tok.tag != Identifier && !is_type_token(tok.tag) && tok.tag != Colons && tok.tag != Commas) {
            return false;
        }

        lexer_advance(&lookahead);
    }

    return false;
}

Exprs parser_expr_bp(Parser* self, int min_prec) {
    Exprs left = parser_expr_primary(self);

    while (1) {
        LexerToken tok = parser_current(self);
        int prec = parser_precedence(tok.tag);
        if (prec == -1 || prec < min_prec) break;

        LexerTokenTag op = tok.tag;
        ADVANCE(); 
        if (op == LeftBrackets) {
            Exprs index_expr = parser_expr_bp(self, 0); 

            EXPECT(RightBrackets, "expected closing ']' after index expression");

            Exprs* base_heap = malloc(sizeof(Exprs));
            *base_heap = left;

            Exprs* idx_heap = malloc(sizeof(Exprs));
            *idx_heap = index_expr;

            left = (Exprs){
                .tag = Expr_Idx,
                .data.idx = {
                    .base = base_heap,
                    .index = idx_heap,
                    .range = tok.range
                }
            };
            continue;
        }


        if (op == Dots) {
            if (!MATCH(Identifier)) {
                parse_error(self, ParseErr_ExpectedToken, "expected field identifier after '.'", parser_current(self).tag);
                continue;
            }
            
            SourceRange member_name = parser_current(self).range;
            ADVANCE();

            Exprs* base_heap = malloc(sizeof(Exprs));
            *base_heap = left;

            RangeArr generic_params = parse_generic_args(self);

            if (parser_current(self).tag == LeftParens) {
                ParamArr params = parse_call_arguments(self);

                left = (Exprs){
                    .tag = Expr_Class_Calls,
                    .data = { .class_calls = {
                        .object = base_heap,
                        .function = member_name,     
                        .generic_params = generic_params.data,
                        .generic_params_count = generic_params.len,
                        .param = params.data,
                        .param_count = params.len
                    }}
                };
                continue;
            }

            left = (Exprs){
                .tag = Expr_Field,
                .data.field_access = {
                    .object = base_heap,
                    .field  = member_name   
                }
            };

            continue;
        }

        // I'll make something cleaner than this.
        bool is_assignment = (
            op == Equalss || op == PlusEqualss || op == MinusEqualss || 
            op == StarEqualss || op == SlashEqualss || op == PercentEqualss || 
            op == PipeEqualss || op == AmpersandEqualss || op == CaretEqualss || 
            op == LeftShiftEqualss || op == RightShiftEqualss
        );
        
        int next_prec = is_assignment ? prec : prec + 1;

        ExprsArr l_arr = {0}, r_arr = {0};
        ARR_PUSH(l_arr, left);
        ARR_PUSH(r_arr, parser_expr_bp(self, next_prec));

        if (op != Equalss && is_assignment) {
            LexerTokenTag math_op;
            switch (op) {
                case PlusEqualss:       math_op = Plus; break;
                case MinusEqualss:      math_op = Minuss; break;
                case StarEqualss:       math_op = Stars; break;
                case SlashEqualss:      math_op = Slashs; break;
                case PercentEqualss:    math_op = Percents; break;
                case PipeEqualss:       math_op = Pipes; break;
                case AmpersandEqualss:  math_op = Ampersands; break;
                case CaretEqualss:      math_op = Carets; break;
                case LeftShiftEqualss:  math_op = LeftShifts; break;
                case RightShiftEqualss: math_op = RightShifts; break;
                default:                math_op = op; break;
            }

            Exprs math_expr = (Exprs){
                .tag = Expr_BinaryOps,
                .data = { .binary_ops = { .left = l_arr.data, .op = math_op, .right = r_arr.data }}
            };

            ExprsArr right_transformed = {0};
            ARR_PUSH(right_transformed, math_expr);

            left = (Exprs){
                .tag = Expr_BinaryOps,
                .data = { .binary_ops = { .left = l_arr.data, .op = Equalss, .right = right_transformed.data }}
            };
        } else {
            left = (Exprs){
                .tag = Expr_BinaryOps,
                .data = { .binary_ops = { .left = l_arr.data, .op = op, .right = r_arr.data }}
            };
        }
    }

    if (MATCH(Ass)) {
        ADVANCE(); 
        Type cast_ty = parser_type(self);
        Exprs* inner = malloc(sizeof(Exprs));
        *inner = left;

        Type* ty_heap = malloc(sizeof(Type));
        *ty_heap = cast_ty;

        left = (Exprs){
            .tag = Expr_Cast,
            .data.cast = { .expr = inner, .ty = ty_heap }
        };
    }

    return left;
}

Exprs parser_expr(Parser* self) {
    return parser_expr_bp(self, 0);
}

Exprs parser_array(Parser* self) {
    SourceRange start = parser_current(self).range;
    parser_advance(self); 

    ExprsArr elems = {0};

    while (parser_current(self).tag != RightBrackets && parser_current(self).tag != EOFs) {
        Exprs e = parser_expr(self);
        ARR_PUSH(elems, e);
        if (parser_current(self).tag == Commas) {
            parser_advance(self);
        } else {
            break;
        }
    }

    EXPECT(RightBrackets, "expected closing ']' after index expression");

    return (Exprs){
        .tag = Expr_Array,
        .data.array = {
            .elems = elems.data,
            .elems_count = elems.len,
        }
    };
}


Exprs parser_expr_primary(Parser* self) {
    LexerToken tok = parser_current(self);

    if (tok.tag == LeftBrackets) {
        return parser_array(self);
    }
    
    if (tok.tag == Ampersands) {
        parser_advance(self);
        ExprsArr op_arr = {0};
        ARR_PUSH(op_arr, parser_expr_primary(self));

        return (Exprs){
            .tag = Expr_AddrOf,
            .data.unary = { .op = Ampersands, .operand = op_arr.data }
        };
    }

    if (tok.tag == Minuss) {
        parser_advance(self);
        ExprsArr op_arr = {0};
        ARR_PUSH(op_arr, parser_expr_primary(self));

        return (Exprs){
            .tag = Expr_Unary,
            .data = { .unary = { .op = Minuss, .operand = op_arr.data }}
        };
    }

    if (tok.tag == Nots || tok.tag == Bangs) {
        parser_advance(self);
        ExprsArr op_arr = {0};
        ARR_PUSH(op_arr, parser_expr_primary(self));

        return (Exprs){
            .tag = Expr_Unary,
            .data = { .unary = { .op = tok.tag, .operand = op_arr.data }}
        };
    }

    if (tok.tag == Tildes) {
        parser_advance(self);
        ExprsArr op_arr = {0};
        ARR_PUSH(op_arr, parser_expr_primary(self));
        return (Exprs){
            .tag = Expr_Unary,
            .data = { 
                .unary = { 
                    .op = Tildes, 
                    .operand = op_arr.data 
                }
            }
        };
    }

    if (tok.tag == LeftParens) {
        parser_advance(self);

        if (parser_current(self).tag == RightParens) {
            parser_advance(self);
            return (Exprs){ .tag = Expr_Tuple, .data.tuple = { .elems = NULL, .elems_count = 0 } };
        }

        Exprs first = parser_expr(self);

        if (parser_current(self).tag == Commas) {
            ExprsArr elems = {0};
            ARR_PUSH(elems, first);

            while (parser_current(self).tag == Commas) {
                parser_advance(self);
                if (parser_current(self).tag == RightParens) break;
                ARR_PUSH(elems, parser_expr(self));
            }
            EXPECT(RightParens, "expected closing ')'");

            return (Exprs){
                .tag = Expr_Tuple,
                .data.tuple = { .elems = elems.data, .elems_count = elems.len }
            };
        }

        EXPECT(RightParens, "expected closing ')'");
        return first;
    }

    if (tok.tag == Strings || tok.tag == Ints  || tok.tag == Floats  || tok.tag == Chars || tok.tag == Trues   || tok.tag == Falses || tok.tag == Nulls) {
        parser_advance(self);
        return (Exprs){
            .tag = Expr_Literals,
            .data = { .literals = { .range = tok.range }}
        };
    }
    

    if (tok.tag == Identifier) {
        SourceRange name = tok.range;
        parser_advance(self);
        if (parser_current(self).tag == Lesses && parser_is_generic_invocation(self)) {
            parser_advance(self);

            RangeArr generic = {0};

            while (parser_current(self).tag != Greaters && parser_current(self).tag != EOFs) {
                LexerToken cur = parser_current(self);
                if (cur.tag == Identifier || is_type_token(cur.tag)) {
                    ARR_PUSH(generic, cur.range);
                }
                parser_advance(self);
            }

            if (parser_current(self).tag == Greaters) {
                parser_advance(self);
            }

            switch (parser_current(self).tag) {
                case LeftParens: {
                    ParamArr params = parse_call_arguments(self);
                    return (Exprs){
                        .tag = Expr_Function,
                        .data.function_call = {
                            .name = name,
                            .param = params.data,
                            .param_count = params.len,
                            .generic_params = generic.data,
                            .generic_params_count = generic.len,
                        }
                    };
                }
                case LeftBraces: {
                    Exprs result = parser_literal(self, name);
                    result.data.struct_calls.generic_params = generic.data;
                    result.data.struct_calls.generic_params_count = generic.len;
                    return result;
                }
                default:
                    break;
            }
        }

        if (parser_current(self).tag == LeftParens) {
            return parser_function_call(self, name);
        }

        if (parser_current(self).tag == LeftBraces) {
            return parser_literal(self, name);
        }

        return (Exprs){
            .tag = Expr_Identifiers,
            .data.identifiers = {
                .name = name,
            }
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


Exprs parser_index(Parser* self, SourceRange idx) {
    Exprs base_expr = (Exprs){
        .tag = Expr_Identifiers,
        .data = { .identifiers = { .name = idx }}
    };

    Exprs index_expr = parser_expr(self);
    EXPECT(RightBrackets, "expected closing ']' after index expression");

    Exprs* base_heap = malloc(sizeof(Exprs));
    *base_heap = base_expr;

    Exprs* idx_heap = malloc(sizeof(Exprs));
    *idx_heap = index_expr;

    return (Exprs){
        .tag = Expr_Idx,
        .data.idx = {
            .base = base_heap,
            .index = idx_heap,
            .range = idx,
        }
    };
}

Exprs parser_function_call(Parser* self, SourceRange fn) {
    printf("It is called!");
    RangeArr generic_params = parse_generic_args(self);
    ParamArr params = parse_call_arguments(self);

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
    parser_advance(self); // consume '.'
    SourceRange function = {0};

    if (parser_current(self).tag == Identifier) {
        function = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected method name after '.'", Identifier);
    }

    RangeArr generic_params = parse_generic_args(self);

    Exprs* obj_heap = checked_malloc(sizeof(Exprs));
    *obj_heap = (Exprs){
        .tag = Expr_Identifiers,
        .data = { .identifiers = { .name = class } }
    };

    if (parser_current(self).tag == LeftParens) {
        ParamArr params = parse_call_arguments(self);

        return (Exprs){
            .tag = Expr_Class_Calls,
            .data = { .class_calls = {
                .object = obj_heap,
                .function = function,
                .generic_params = generic_params.data,
                .generic_params_count = generic_params.len,
                .param = params.data,
                .param_count = params.len
            }}
        };
    }

    return (Exprs){
        .tag = Expr_Field,
        .data.field_access = {
            .object = obj_heap,
            .field  = function,
        }
    };
}

Exprs parser_struct_call(Parser* self, SourceRange str) {
    parser_advance(self);
    SourceRange function = {0};
    ParamArr params = {0};

    RangeArr generic_params = parse_generic_args(self);

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
            p.type = parser_type(self);

            ARR_PUSH(params, p);
            if (parser_current(self).tag == Commas) parser_advance(self);
        }
        EXPECT(RightBraces, "expected closing '}' after structural initializer");
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
    SourceRange field = {0};

    RangeArr generic_params = parse_generic_args(self);

    if (parser_current(self).tag == Dots) {
        parser_advance(self);
        if (parser_current(self).tag == Identifier) {
            field = parser_current(self).range;
            parser_advance(self);
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected enum variant name after '.'", Identifier);
        }
    }

    ParamArr params = parser_current(self).tag == LeftParens ? parse_call_arguments(self) : (ParamArr){0};

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

Type parser_type_base(Parser* self) {
    LexerToken tok = parser_current(self);

    if (tok.tag == LeftBrackets) {
        parser_advance(self); // consume '['

        Type current_type = parser_type(self);

        while (parser_current(self).tag == Commas) {
            parser_advance(self); // consume ','

            if (parser_current(self).tag == Ints) {
                size_t len = (size_t)parser_current(self).data.value_int;
                parser_advance(self);

                Type* inner_alloc = malloc(sizeof(Type));
                if (inner_alloc != NULL) {
                    *inner_alloc = current_type;
                }

                current_type = (Type){
                    .tag = Type_Array,
                    .data.array_t.inner = inner_alloc,
                    .data.array_t.len = len
                };
            } else {
                parse_error(self, ParseErr_ExpectedToken, "expected array length integer after ','", Ints);
                break;
            }
        }

        EXPECT(RightBrackets, "expected closing ']' after index expression");
        return current_type;
    }
    

    if (tok.tag == TypeToken) {
        Type t = {0};
        t.tag = (TypeTag)tok.data.value_int;
        parser_advance(self);
        return t;
    }

    if (tok.tag == Ints) { parser_advance(self); return (Type){ .tag = Type_Int, .data.int_t = { .bits = (int)tok.data.value_int, .is_unsigned = tok.is_unsigned } }; }
    if (tok.tag == Floats) { parser_advance(self); return (Type){ .tag = Type_Float, .data.float_t = { .bits = (int)tok.data.value_int } }; }
    if (tok.tag == Strings) { parser_advance(self); return (Type){ .tag = Type_Str }; }
    if (tok.tag == Chars) { parser_advance(self); return (Type){ .tag = Type_Char }; }
    if (tok.tag == Voids) { parser_advance(self); return (Type){ .tag = Type_Void }; }
    if (tok.tag == Bools ) { parser_advance(self); return (Type){ .tag = Type_Bool }; }

    if (parser_current(self).tag == Atomics) {
        parser_advance(self);

        if (!self->atomic_imported) {
            parse_error(self, ParseErr_UnexpectedToken, "atomic[T] requires 'import Atomic from std'", Atomics);
            parser_advance(self);
            return (Type){ .tag = Type_Void };
        }

        if (parser_current(self).tag != LeftBrackets) {
            parse_error(self, ParseErr_ExpectedToken, "expected '[' after 'atomic'", LeftBrackets);
            return (Type){ .tag = Type_Void };
        }
        parser_advance(self);
        TypeArr inner_arr = {0};
        ARR_PUSH(inner_arr, parser_type(self));

        if (parser_current(self).tag != RightBrackets) {
            parse_error(self, ParseErr_ExpectedToken, "expected ']' to close 'atomic[...]'", RightBrackets);
        } else {
            parser_advance(self);
        }

        return (Type){ .tag = Type_Atomic, .data.atomic.inner = inner_arr.data };
    }


    if (tok.tag == LeftParens) {
        parser_advance(self);
        TypeArr elems = {0};

        while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
            ARR_PUSH(elems, parser_type(self));
            if (parser_current(self).tag == Commas) parser_advance(self);
        }
        EXPECT(RightParens, "expected closing ')'");

        return (Type){ .tag = Type_Tuple, .data.tuple = { .elems = elems.data, .elems_count = elems.len } };
    }

    if (tok.tag == Stars) {
            parser_advance(self);

            bool is_const = false;
            if (parser_current(self).tag == Consts) {
                is_const = true;
                parser_advance(self);
            }

            TypeArr inner_arr = {0};
            if (parser_current(self).tag == Stars) {
                parser_advance(self);

                ARR_PUSH(inner_arr, parser_type(self));

                bool inner_const = false;
                if (parser_current(self).tag == Consts) {
                    inner_const = true;
                    parser_advance(self);
                }

                TypeArr raw_arr = {0};
                ARR_PUSH(raw_arr, ((Type){ 
                    .tag = Type_RawPtr, 
                    .data.raw_ptr = { .inner = inner_arr.data, .is_const = inner_const } 
                }));

                return (Type){ 
                    .tag = Type_Ptr, 
                    .data.ptr = { .inner = raw_arr.data, .is_const = is_const } 
                };
            }

            ARR_PUSH(inner_arr, parser_type(self));
            return (Type){
                .tag = Type_Ptr,
                .data.ptr = { .inner = inner_arr.data, .is_const = is_const } // Uses inner_arr.data directly!
            };
        }

    if (tok.tag == Functions) {
        parser_advance(self);

        TypeArr params = {0};

        if (parser_current(self).tag == LeftParens) {
            parser_advance(self);
            while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
                ARR_PUSH(params, parser_type(self));
                if (parser_current(self).tag == Commas) {
                    parser_advance(self);
                }
            }
            EXPECT(RightParens, "expected closing ')'");
        }

        Type ret = { .tag = Type_Void };
        if (parser_current(self).tag == Colons) {
            parser_advance(self);
            ret = parser_type(self);
        }

        TypeArr ret_arr = {0};
        ARR_PUSH(ret_arr, ret);

        return (Type){ 
            .tag = Type_FnPtr, 
            .data.fn_ptr = { 
                .ret = ret_arr.data, 
                .params = params.data, 
                .params_count = params.len 
            } 
        };
    }

    if (tok.tag == Identifier) {
        parser_advance(self);
        return (Type){ .tag = Type_Custom, .data.custom = { .name = tok.range } };
    }

    parse_error(self, ParseErr_InvalidType, "expected a type", tok.tag);
    parser_advance(self);
    return (Type){ .tag = Type_Void };
}

Type parser_type(Parser* self) {
    Type t = parser_type_base(self);


    if (t.tag == Type_Custom && parser_current(self).tag == LeftBrackets) {
        parser_advance(self);
        RangeArr generic_args = {0};
        while (parser_current(self).tag != RightBrackets && parser_current(self).tag != EOFs) {
            ARR_PUSH(generic_args, parser_current(self).range);
            parser_advance(self);
            if (parser_current(self).tag == Commas) parser_advance(self);
        }
        EXPECT(RightBrackets, "expected closing ']' after index expression");

        t.data.custom.generic_args = generic_args.data;
        t.data.custom.generic_args_count = generic_args.len;
        return t;
    }

    if (t.tag == TypeToken && parser_current(self).tag == LeftBrackets) {
        parser_advance(self);
        size_t len = 0;

        if (parser_current(self).tag == Ints) {
            len = (size_t)parser_current(self).data.value_int;
            parser_advance(self);
        }

        EXPECT(RightBrackets, "expected closing ']' after index expression");

        TypeArr inner_arr = {0};
        ARR_PUSH(inner_arr, t);
        return (Type){ .tag = Type_Array, .data.array_t.inner = inner_arr.data, .data.array_t.len = len };
    }

    return t;
}

Stmts parser_functions(Parser* self, bool is_const, bool is_unsafe, bool is_pub) {
    parser_advance(self);

    SourceRange n = {0};
    Type return_type = { .tag = Type_Void };
    StmtsArr body = {0};

    if (parser_current(self).tag == Identifier) {
        n = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected function name", Identifier);
    }

    GenericParamArr generic_params = parse_generic_params(self);
    ParamArr params = parse_function_params(self);

    if (parser_current(self).tag == Colons) {
        parser_advance(self);
        return_type = parser_type(self);
    }

    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        size_t pos_before = (size_t)(parser_current(self).range.start - self->lexer.source);
        Stmts s = parser_stmt(self);
        if (s.tag != 0) ARR_PUSH(body, s);
        size_t pos_after = (size_t)(parser_current(self).range.start - self->lexer.source);
        if (pos_after == pos_before) parser_advance(self);
    }

    EXPECT(Ends, "expected 'end' keyword to close statement block");

    return (Stmts){
        .tag = Stmt_Functions,
        .data = { .functions = {
            .name = n,
            .params = params.data,
            .params_count = params.len,
            .generic_params = generic_params.data,
            .generic_params_count = generic_params.len,
            .return_type = return_type,
            .body = body.data,
            .body_count = body.len,
            .is_unsafe = is_unsafe,
            .is_pub = is_pub,
        }}
    };
}

Stmts parser_trait_functions(Parser* self) {
    parser_advance(self);
    SourceRange n = {0};
    Type return_type = { .tag = Type_Void };

    if (parser_current(self).tag == Identifier) {
        n = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected function name", Identifier);
    }

    GenericParamArr generic_params = parse_generic_params(self);
    ParamArr params = parse_function_params(self);

    if (parser_current(self).tag == Colons) {
        parser_advance(self);
        return_type = parser_type(self);
    }

    return (Stmts){
        .tag = Stmt_Functions,
        .data = { .functions = {
            .name = n,
            .params = params.data,
            .params_count = params.len,
            .generic_params = generic_params.data,
            .generic_params_count = generic_params.len,
            .return_type = return_type,
            .body = NULL,
            .body_count = 0,
            .is_unsafe = false,
            .is_pub = false,
        }}
    };
}
Stmts parser_class(Parser* self, bool is_pub) {
    parser_advance(self);
    SourceRange n = {0};
    SourceRange parent = {0};
    StructParamArr fields = {0};
    MethodArr methods = {0};
    RangeArr traits = {0};

    if (parser_current(self).tag == Identifier) {
        n = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected class name", Identifier);
    }

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

    GenericParamArr generic_params = parse_generic_params(self);
    ParamArr class_params = parser_current(self).tag == LeftParens ? parse_function_params(self) : (ParamArr){0};

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
            f.type = parser_type(self);
            ARR_PUSH(fields, f);
        } else if (parser_current(self).tag == Publics) {
            parser_advance(self);
            bool method_unsafe = false;
            if (parser_current(self).tag == Unsafes) {
                parser_advance(self);
                method_unsafe = true;
            }
            if (parser_current(self).tag != Functions) {
                parse_error(self, ParseErr_ExpectedToken, "expected 'fn' after 'public' in class body", Functions);
                parser_advance(self);
                continue;
            }
            parser_class_method(self, true, method_unsafe, &methods);
        } else if (parser_current(self).tag == Functions) {
            parser_class_method(self, false, false, &methods);
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

    EXPECT(Ends, "expected 'end' keyword to close statement block");

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
            .generic_param_nodes = generic_params.data,
            .trait_bounds = traits.data,
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

    GenericParamArr generic_params = {0};
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
                GenericParam gp = {0};
                gp.name = parser_current(self).range;
                parser_advance(self);

                if (parser_current(self).tag == Colons) {
                    parser_advance(self);
                    if (parser_current(self).tag == Identifier) {
                        gp.bound = parser_current(self).range;
                        parser_advance(self);
                    } else {
                        parse_error(self, ParseErr_ExpectedToken, "expected trait bound after ':'", Identifier);
                    }
                }

                ARR_PUSH(generic_params, gp);
            } else if (parser_current(self).tag == Commas) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_UnexpectedToken, "expected identifier or ',' in generic params", parser_current(self).tag);
                parser_advance(self);
            }
        }
        EXPECT(RightBrackets, "expected closing ']' after index expression");
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
                Type parsed = parser_type(self);
                f.type = parsed;

                ARR_PUSH(fields, f);
                break;
            }
            case Identifier: {
                f.mode = (VarMode){ .tag = VarMode_Value, .mutability = Mutability_Immutable };
                f.name = parser_current(self).range;
                parser_advance(self);

                if (parser_current(self).tag == Colons) {
                    parser_advance(self);
                } else {
                    parse_error(self, ParseErr_ExpectedToken, "expected ':' after field name", Equalss);
                }
                Type parsed = parser_type(self);
                f.type = parsed;


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
    EXPECT(Ends, "expected 'end' keyword to close statement block");

    return (Stmts){
        .tag = Stmt_Structs,
        .data = { .structs = {
            .name = n,
            .generic_params = generic_params.data,
            .generic_params_count = generic_params.len,
            .generic_param_nodes = generic_params.data,
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
    GenericParamArr generic_params = {0};
    GenericParam gp = {0};

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
                gp.name = parser_current(self).range;
                parser_advance(self);
                if (parser_current(self).tag == Colons) {
                    parser_advance(self);
                    if (parser_current(self).tag == Identifier) {
                        gp.bound = parser_current(self).range;
                        parser_advance(self);
                    } else {
                        parse_error(self, ParseErr_ExpectedToken, "expected trait bound after ':'", Identifier);
                    }
                }
                ARR_PUSH(generic_params, gp);

            } else if (parser_current(self).tag == Commas) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_UnexpectedToken, "expected identifier or ',' in generic params", parser_current(self).tag);
                parser_advance(self);
            }
        }
        EXPECT(RightBrackets, "expected closing ']' after index expression");
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

                    bool has_name = (parser_current(self).tag == Identifier || is_type_token(parser_current(self).tag)) && parser_peek_next(self, 1).tag == Colons;

                    if (has_name) {
                        fname = parser_current(self).range;
                        parser_advance(self);
                        parser_advance(self);
                    }

                    Type ft = parser_type(self);
                    ARR_PUSH(fields, ((EnumField){ .name = fname, .type = ft }));
                    if (parser_current(self).tag == Commas) parser_advance(self);
                }
                EXPECT(RightParens, "expected closing ')'");

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
    EXPECT(Ends, "expected 'end' keyword to close statement block");

    return (Stmts){
        .tag = Stmt_Enums,
        .data = { .enums = {
            .name = n,
            .variants = variants.data,
            .variants_count = variants.len,
            .generic_params = generic_params.data,
            .generic_params_count = generic_params.len,
            .generic_param_nodes = generic_params.data,
            .is_pub = is_pub,
        }}
    };
}

Stmts parser_traits(Parser* self, bool is_pub, bool is_unsafe) {
    parser_advance(self);

    TraitMethodArr methods = {0};
    RangeArr types = {0};
    SourceRange n = {0};
    GenericParamArr generic_params = {0};

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
                GenericParam gp = {0};
                gp.name = parser_current(self).range;
                parser_advance(self);

                if (parser_current(self).tag == Colons) {
                    parser_advance(self);
                    if (parser_current(self).tag == Identifier) {
                        gp.bound = parser_current(self).range;
                        parser_advance(self);
                    } else {
                        parse_error(self, ParseErr_ExpectedToken, "expected trait bound after ':'", Identifier);
                    }
                }

                ARR_PUSH(generic_params, gp);
            } else if (parser_current(self).tag == Commas) {
                parser_advance(self);
            } else {
                parse_error(self, ParseErr_UnexpectedToken, "expected identifier or ',' in generic params", parser_current(self).tag);
                parser_advance(self);
            }
        }
        EXPECT(RightBrackets, "expected closing ']' after index expression");
    }

    if (parser_current(self).tag == Colons) {
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected ':' after trait name", Colons);
    }

    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        size_t pos_before = (size_t)(parser_current(self).range.start - self->lexer.source);

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
                Stmts fn = parser_trait_functions(self);
                TraitMethod m = {0};
                m.name             = fn.data.functions.name;
                m.params           = fn.data.functions.params;
                m.params_count     = fn.data.functions.params_count;
                m.body             = fn.data.functions.body;
                m.body_count       = fn.data.functions.body_count;
                m.is_pub           = fn.data.functions.is_pub;
                m.return_type      = fn.data.functions.return_type;
                ARR_PUSH(methods, m);
                break;
            }
            default: {
                parse_error(self, ParseErr_UnexpectedToken, "unexpected token in trait body; expected 'fn' or 'type'", parser_current(self).tag);
                parser_advance(self);
                break;
            }
        }

        size_t pos_after = (size_t)(parser_current(self).range.start - self->lexer.source);
        if (pos_after == pos_before) parser_advance(self);
    }
    EXPECT(Ends, "expected 'end' keyword to close statement block");
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
    SourceRange var_type_start = (SourceRange){0};
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
            .type = var_type,
            .value = var_value,
            .range = { .start = start.start, .end = n.end, .file_id = start.file_id },
        }
    };
}

Stmts parser_lets(Parser* self) {
    SourceRange start = parser_current(self).range;
    parser_advance(self);

    SourceRange n = {0};
    Type let_type = {0};
    SourceRange let_type_start = (SourceRange){0};
    Exprs var_value = {0};

    if (parser_current(self).tag == Identifier) {
        n = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected binding name after 'let'", Identifier);
    }

    if (parser_current(self).tag == Colons) {
        parser_advance(self);
        let_type = parser_type(self);
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
            .name      = n,
            .type = let_type,
            .value     = var_value,
            .range     = { .start = start.start, .end = n.end, .file_id = start.file_id },
        }
    };
}

Stmts parser_const(Parser* self) {
    SourceRange start = parser_current(self).range;
    SourceRange n = {0};
    Type const_type = {0};
    SourceRange const_type_start = (SourceRange){0};
    Exprs var_value = {0};

    if (parser_current(self).tag == Identifier) {
        n = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected constant name after 'const'", Identifier);
    }

    if (parser_current(self).tag == Colons) {
        parser_advance(self);
        
        const_type = parser_type(self);
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
            .name      = n,
            .type = const_type,
            .value     = var_value,
            .range     = { .start = start.start, .end = n.end, .file_id = start.file_id },
        }
    };
}

Stmts parser_globle(Parser* self, bool is_pub, bool is_const) {
    parser_advance(self);

    SourceRange n = {0};
    Type g_type = {0};
    SourceRange g_type_start = (SourceRange){0};
    Exprs var_value = {0};

    if (parser_current(self).tag == Identifier) {
        n = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected global variable name", Identifier);
    }

    if (parser_current(self).tag == Colons) {
        parser_advance(self);
        g_type = parser_type(self);
    }

    if (is_const) {
        if (parser_current(self).tag == Equalss) {
            parser_advance(self);
            var_value = parser_expr(self);
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected '=' in global const declaration", Equalss);
        }

        return (Stmts){
            .tag = Stmt_Consts,
            .data.consts = {
                .name      = n,
                .type = g_type,
                .value     = var_value,
                .is_pub    = is_pub,
                .range     = n,
            }
        };
    }

    return (Stmts){
        .tag = Stmt_Locals,
        .data.locals = {
            .name = n,
            .type = g_type,
            .is_pub = is_pub,
            .range = n,
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

    if (parser_current(self).tag != Strings) {
        parse_error(self, ParseErr_ExpectedToken, "expected identifier in @ffi(...)", Identifier);
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
        .data.extern_ = {
            .abi = ex.abi,
            .ffi = result,
            .funcs = ex.funcs,
            .funcs_count = ex.funcs_count,
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
                SourceRange type_start = parser_current(self).range;
                Type param_type = parser_type(self);

                p.type = param_type;
                ARR_PUSH(params, p);
                if (parser_current(self).tag == Commas) parser_advance(self);
            }

            EXPECT(RightParens, "expected closing ')'");
            fn.params = params.data;
            fn.params_count = params.len;
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected '(' in extern function signature", LeftParens);
        }

        if (parser_current(self).tag == Colons) {
            parser_advance(self);
            SourceRange ret_start = parser_current(self).range;
            Type ret_type = parser_type(self);

            fn.return_type = ret_type;
        }

        fn.ffi_type = abi;
        ARR_PUSH(funcs, fn);
    }

    EXPECT(Ends, "expected 'end' keyword to close statement block");

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

    EXPECT(Ends, "expected 'end' keyword to close statement block");

    return (Stmts){
        .tag = Stmt_Matchs,
        .data.matchs = {
            .expr = condition,
            .cases = arms.data,
            .cases_count = arms.len,
        }
    };
}
Stmts parser_while(Parser* self) {
    parser_advance(self); // consume 'while'

    Exprs condition = parser_expr(self);

    if (parser_current(self).tag == Dos) {
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected 'do' after while condition", Dos);
    }

    StmtsArr body = {0};
    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        Stmts s = parser_stmt(self);
        if (s.tag != 0) ARR_PUSH(body, s);
    }

    EXPECT(Ends, "expected 'end' keyword to close statement block");

    return (Stmts){
        .tag = Stmt_Whiles,
        .data.whiles = {
            .cond = condition,
            .body = body.data,
            .body_count = body.len,
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

    EXPECT(Ends, "expected 'end' keyword to close statement block");

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

static IfPat parser_if_pat(Parser* self) {
    IfPat pat = {0};
    LexerToken cur = parser_current(self);

    if (cur.tag == Lets) {
        pat.kind = IfPat_Let;
        parser_advance(self);

        LexerToken name = parser_current(self);
        if (name.tag != Identifier) {
            parse_error(self, ParseErr_ExpectedToken,
                        "expected identifier after 'let'", Identifier);
            return pat;
        }
        pat.bind_name = name.range;
        parser_advance(self);

    } else if (cur.tag == Vars) {
        pat.kind = IfPat_Var;
        parser_advance(self);

        LexerToken name = parser_current(self);
        if (name.tag != Identifier) {
            parse_error(self, ParseErr_ExpectedToken,
                        "expected identifier after 'var'", Identifier);
            return pat;
        }
        pat.bind_name = name.range;
        parser_advance(self);

    } else if (cur.tag == Identifier && isupper((unsigned char)cur.range.start[0])) {
        pat.variant = cur.range;
        parser_advance(self);

        if (parser_current(self).tag != LeftParens) {
            parse_error(self, ParseErr_ExpectedToken,
                        "expected '(' after enum variant in if-pattern", LeftParens);
            return pat;
        }
        parser_advance(self);

        LexerToken inner = parser_current(self);
        if (inner.tag == Underscores) {
            pat.kind              = IfPat_Wildcard;
            pat.inner_is_wildcard = true;
        } else if (inner.tag == Identifier) {
            pat.kind       = IfPat_Enum;
            pat.inner_bind = inner.range;
        } else {
            parse_error(self, ParseErr_ExpectedToken,
                        "expected identifier or '_' inside enum pattern", Identifier);
            return pat;
        }
        parser_advance(self);

        if (parser_current(self).tag != RightParens) {
            parse_error(self, ParseErr_ExpectedToken,
                        "expected ')' after enum pattern binding", RightParens);
            return pat;
        }
        parser_advance(self);

    } else {
        pat.kind = IfPat_None;
        return pat;
    }

    if (parser_current(self).tag != Equalss) {
        parse_error(self, ParseErr_ExpectedToken,
                    "expected '=' after if-pattern", Equalss);
        return pat;
    }
    parser_advance(self);

    return pat;
}

Stmts parser_if(Parser* self) {
    parser_advance(self);

    /* ── pattern (optional) ── */
    IfPat pat = parser_if_pat(self);

    /* ── condition / rhs expression ── */
    Exprs condition = parser_expr(self);

    /* attach the pattern's init_expr pointer to the condition we just parsed */
    if (pat.kind != IfPat_None) {
        pat.init_expr = &condition;   /* or store a heap copy if your AST owns it */
    }

    if (parser_current(self).tag == Thens) {
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken,
                    "expected 'then' after if condition", Thens);
    }

    StmtsArr body = {0};
    while (parser_current(self).tag != Ends  &&
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
            .pat = pat,
            .cond = condition,
            .body = body.data,
            .body_count = body.len,
            .else_body = else_body.data,
            .else_body_count = else_body.len,
        }
    };
}

Stmts parser_elif(Parser* self) {
    parser_advance(self);

    IfPat pat = parser_if_pat(self);
    Exprs condition = parser_expr(self);

    if (pat.kind != IfPat_None) {
        pat.init_expr = &condition;
    }

    if (parser_current(self).tag == Thens) {
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken,
                    "expected 'then' after elif condition", Thens);
    }

    StmtsArr body = {0};
    while (parser_current(self).tag != Ends  &&
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
        .tag      = Stmt_Elifs,
        .data.ifs = {
            .pat             = pat,
            .cond            = condition,
            .body            = body.data,
            .body_count      = body.len,
            .else_body       = else_body.data,
            .else_body_count = else_body.len,
        }
    };
}

Stmts parser_import(Parser* self) {
    SourceRange range = parser_current(self).range;
    parser_advance(self);

    if (parser_current(self).tag == Stars) {
        parser_advance(self);

        if (parser_current(self).tag != Froms) {
            parse_error(self, ParseErr_ExpectedToken, "expected 'from' after 'import *'", Froms);
        }
        parser_advance(self);

        SourceRange path = {0};
        if (parser_current(self).tag == Strings) {
            path = parser_current(self).range;
            parser_advance(self);
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected module path string after 'from'", Strings);
        }

        return (Stmts){
            .tag = Stmt_Imports,
            .data.imports = {
                .kind = Import_Star,
                .path = path,
                .is_star = true,
                .range = range,
            }
        };
    }

    if (parser_current(self).tag == Strings) {
        SourceRange path = parser_current(self).range;
        parser_advance(self);
        return (Stmts){
            .tag = Stmt_Imports,
            .data.imports = {
                .kind = Import_Plain,
                .path = path,
                .range = range,
            }
        };
    }

    SourceRange name = {0};
    if (parser_current(self).tag == Identifier) {
        name = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected name or '*' after 'import'", Identifier);
    }

    SourceRange path = {0};
    if (parser_current(self).tag == Froms) {
        parser_advance(self);
        if (parser_current(self).tag == Strings) {
            path = parser_current(self).range;
            parser_advance(self);
        } else {
            parse_error(self, ParseErr_ExpectedToken, "expected module path after 'from'", Strings);
        }
    }

    if (range_eq(name, "Atomic") && range_eq(path, "std")) {
        self->atomic_imported = true;
    }

    return (Stmts){
        .tag = Stmt_Imports,
        .data.imports = {
            .kind = Import_Named,
            .name = name,
            .path = path,
            .range = range,
        }
    };
}

Stmts parser_from_import(Parser* self) {
    SourceRange range = parser_current(self).range;
    parser_advance(self);

    SourceRange path = {0};
    if (parser_current(self).tag == Strings) {
        path = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected module path after 'from'", Strings);
    }

    if (parser_current(self).tag != Imports) {
        parse_error(self, ParseErr_ExpectedToken, "expected 'import' after module path", Imports);
    }
    parser_advance(self);

    SourceRange name = {0};
    bool is_star = false;

    if (parser_current(self).tag == Stars) {
        is_star = true;
        parser_advance(self);
    } else if (parser_current(self).tag == Identifier) {
        name = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected name or '*' after 'import'", Identifier);
    }

    return (Stmts){
        .tag = Stmt_Imports,
        .data.imports = {
            .kind = is_star ? Import_Star : Import_From,
            .name = name,
            .path = path,
            .is_star = is_star,
            .range = range,
        }
    };
}

Stmts parser_module(Parser* self, bool is_pub) {
    SourceRange range = parser_current(self).range;
    parser_advance(self);

    SourceRange name = {0};
    if (parser_current(self).tag == Identifier) {
        name = parser_current(self).range;
        parser_advance(self);
    } else {
        parse_error(self, ParseErr_ExpectedToken, "expected module name", Identifier);
    }

    StmtsArr body = {0};
    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        size_t pos_before = (size_t)(parser_current(self).range.start - self->lexer.source);
        Stmts s = parser_stmt(self);
        size_t pos_after  = (size_t)(parser_current(self).range.start - self->lexer.source);

        if (s.tag != 0) ARR_PUSH(body, s);
        if (pos_after == pos_before) parser_advance(self);
    }
    EXPECT(Ends, "expected 'end' keyword to close statement block");

    return (Stmts){
        .tag = Stmt_Modules,
        .data.modules = {
            .name = name,
            .body = body.data,
            .body_count = body.len,
            .is_pub = is_pub,
            .range = range,
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

static OrderingTag parser_ordering(Parser* self) {
    if (parser_current(self).tag != Orderings) {
        parse_error(self, ParseErr_ExpectedToken, "expected Ordering (Relaxed, Acquire, Release, AcqRel, SeqCst)", Orderings);
        return Ordering_SeqCst;
    }

    OrderingTag ord = (OrderingTag)parser_current(self).data.value_int;
    parser_advance(self);
    return ord;
}


void parser_class_method(Parser* self, bool is_pub, bool is_unsafe, MethodArr* methods) {
    Stmts fn = parser_functions(self, false, is_unsafe, is_pub);
    FunctionMethod m = {0};
    m.name             = fn.data.functions.name;
    m.params           = fn.data.functions.params;
    m.params_count     = fn.data.functions.params_count;
    m.body             = fn.data.functions.body;
    m.body_count       = fn.data.functions.body_count;
    m.is_pub           = fn.data.functions.is_pub;
    m.is_unsafe        = fn.data.functions.is_unsafe;
    m.return_type      = fn.data.functions.return_type;
    m.return_type = fn.data.functions.return_type;
    ARR_PUSH(*methods, m);
}

Stmts parser_atomic_op(Parser* self, SourceRange target, AtomicOpTag op) {
    SourceRange range = parser_current(self).range;

    if (parser_current(self).tag != LeftParens) {
        parse_error(self, ParseErr_ExpectedToken,
            "expected '(' after atomic operation", LeftParens);
        return (Stmts){0};
    }
    parser_advance(self);

    Exprs args[3] = {0};
    size_t args_count = 0;
    OrderingTag ordering  = Ordering_SeqCst;
    OrderingTag ordering2 = Ordering_Relaxed;

    switch (op) {
        case AtomicOp_Load: {
            ordering = parser_ordering(self);
            break;
        }

        case AtomicOp_Store:
        case AtomicOp_Swap:
        case AtomicOp_FetchAdd:
        case AtomicOp_FetchSub:
        case AtomicOp_FetchAnd:
        case AtomicOp_FetchOr:
        case AtomicOp_FetchXor:
        case AtomicOp_FetchNand:
        case AtomicOp_FetchMax:
        case AtomicOp_FetchMin: {
            args[0] = parser_expr(self);
            args_count = 1;
            if (parser_current(self).tag == Commas) parser_advance(self);
            ordering = parser_ordering(self);
            break;
        }

        case AtomicOp_CompareExchange: {
            args[0] = parser_expr(self);
            args_count++;
            if (parser_current(self).tag == Commas) parser_advance(self);
            args[1] = parser_expr(self);
            args_count++;

            if (parser_current(self).tag == Commas) parser_advance(self); ordering  = parser_ordering(self);
            if (parser_current(self).tag == Commas) parser_advance(self); ordering2 = parser_ordering(self);
            break;
        }
    }

    if (parser_current(self).tag != RightParens) {
        parse_error(self, ParseErr_ExpectedToken,
            "expected ')' to close atomic operation", RightParens);
    } else {
        parser_advance(self);
    }

    return (Stmts){
        .tag = Stmt_AtomicOp,
        .data.atomic_op = {
            .target = target,
            .op = op,
            .args = { args[0], args[1], args[2] },
            .args_count = args_count,
            .ordering   = ordering,
            .ordering2  = ordering2,
            .range      = range,
        }
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
                case Modules:   return parser_module(self, true);
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
            case Identifier: return parser_const(self);
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
            case Imports: return parser_import(self);
            case Froms:   return parser_from_import(self);
            case Modules: return parser_module(self, false);
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
            case Whiles:    return parser_while(self);
            case Externs: {
                ExternBlock ex = parser_extern(self);
                
                return (Stmts){
                    .tag = Stmt_Externs,
                    .data.extern_ = {
                        .abi = ex.abi,
                        .funcs = ex.funcs,
                        .funcs_count = ex.funcs_count,
                    }
                };

            }

            case EOFs: {
                parse_error_eof(self);
                break;
            }

            default: {
                Exprs expr = parser_expr(self); 
                
                return (Stmts){ 
                    .tag = Stmt_ExprStmt, 
                    .data.expr_stmt = { .expr = expr } 
                };
            }
        }
    }
    return (Stmts){0};
}
