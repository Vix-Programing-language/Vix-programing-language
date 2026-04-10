#include "ast.h"
#include "import.h"

bool range_eq(SourceRange r, const char* str);
LexerToken parser_current(Parser* self);
LexerToken parser_advance(Parser* self);
LexerToken parser_peek(Parser* self);
bool is_operation(LexerTokenTag tag);
int parser_precedence(LexerTokenTag tag);
Stmts parser_stmt(Parser* self);
Exprs parser_expr(Parser* self);
Exprs parser_expr_bp(Parser* self, int min_prec);
Exprs parser_expr_primary(Parser* self);
Type  parser_type(Parser* self);
Exprs parser_function_call(Parser* self, SourceRange fn);
Exprs parser_method_calls(Parser* self, SourceRange cls);
Exprs parser_struct_call(Parser* self, SourceRange str);
Exprs parser_enums_call(Parser* self, SourceRange en);
Stmts parser_functions(Parser* self, bool is_const, bool is_unsafe, bool is_pub);
Stmts parser_class(Parser* self, bool is_pub);
Stmts parser_structer(Parser* self, bool is_pub, bool is_unsafe);
Stmts parser_enums(Parser* self, bool is_pub, bool is_unsafe);
Stmts parser_traits(Parser* self, bool is_pub, bool is_unsafe);
Stmts parser_return(Parser* self);
Stmts parser_vars(Parser* self);
Stmts parser_lets(Parser* self);
Stmts parser_const(Parser* self);
Stmts parser_globle(Parser* self, bool is_pub, bool is_const);
Stmts parser_operation(Parser* self);
Operation operation_op(Parser* self);
Stmts operation_atom(Parser* self);
Stmts parser_variable(Parser* self, StmtsTag tag, bool is_pub);

void parse_error(Parser* self, ParseErrKind kind, const char* msg, LexerTokenTag expected);
void parse_error_eof(Parser* self);
bool parser_expect(Parser* self, LexerTokenTag tag);
void parser_sync(Parser* self);

Stmts parser_vars(Parser* self) { return parser_variable(self, Stmt_Vars, false);  }
Stmts parser_lets(Parser* self) { return parser_variable(self, Stmt_Lets, false);  }
Stmts parser_const(Parser* self) { return parser_variable(self, Stmt_Consts, false); }
Exprs parser_expr(Parser* self) { return parser_expr_bp(self, 0); }


Parser parser_new(LexerToken* tokens) {
    return (Parser){ .cur = tokens };
}


Type parser_type(Parser* self) {
    switch (parser_current(self).tag) {
        case Ints: {
            SourceRange r = parser_current(self).range;
            int bits = range_eq(r,"int8") ? 8 : range_eq(r,"int16") ? 16 : range_eq(r,"int64") ? 64 : 32; parser_advance(self);
            
            return (Type){ 
                .tag = Type_Int, 
                .data.int_t.bits = bits 
            };
        }

        case Floats: {
            SourceRange r = parser_current(self).range;
            int bits = range_eq(r,"float64") ? 64 : 32;

            parser_advance(self);
            return (Type){ 
                .tag = Type_Float, 
                .data.float_t.bits = bits 
            };
        }

        case Chars:   { parser_advance(self); return (Type){ .tag = Type_Char }; }
        case Strings: { parser_advance(self); return (Type){ .tag = Type_Str  }; }
        case Trues:   { parser_advance(self); return (Type){ .tag = Type_Bool }; }
        case Falses:  { parser_advance(self); return (Type){ .tag = Type_Bool }; }
        case Identifier: {
            SourceRange r = parser_current(self).range;
            parser_advance(self);

            if (range_eq(r,"bool")) return (Type){ .tag = Type_Bool };
            if (range_eq(r,"void")) return (Type){ .tag = Type_Void };
            if (range_eq(r,"str"))  return (Type){ .tag = Type_Str  };
            if (range_eq(r,"char")) return (Type){ .tag = Type_Char };

            return (Type){ 
                .tag = Type_Custom, 
                .data.custom.name = r 
            };
        }
        default:
            parse_error(self, ParseErr_UnexpectedToken, "expected type", 0); parser_advance(self);
            return (Type){ 
                .tag = Type_Void 
            };
    }
}

Exprs parser_expr_bp(Parser* self, int min_prec) {
    Exprs left = parser_expr_primary(self);

    while (1) {
        LexerToken tok = parser_current(self);
        int prec = parser_precedence(tok.tag);
        if (prec < min_prec) break;

        LexerTokenTag op = tok.tag; parser_advance(self);
        Exprs* l = checked_malloc(sizeof(Exprs));
        Exprs* r = checked_malloc(sizeof(Exprs));

        *l = left;
        *r = parser_expr_bp(self, prec + 1);
        left = (Exprs){
            .tag  = Expr_BinaryOps,
            .data.binary_ops = { .left = l, .op = op, .right = r }
        };
    }

    return left;
}


Exprs parser_expr_primary(Parser* self) {
    LexerToken tok = parser_current(self);

    if (tok.tag == Bangs || tok.tag == Minuss || tok.tag == Tildes || tok.tag == Stars || tok.tag == Ampersands) {
        LexerTokenTag op = tok.tag; parser_advance(self);
        Exprs* operand = checked_malloc(sizeof(Exprs));
       
        *operand = parser_expr_primary(self);

        return (Exprs){
            .tag  = Expr_UnaryOps,
            .data.unary_ops = { .op = op, .operand = operand }
        };
    }

    if (tok.tag == LeftParens) {
        parser_advance(self);
        Exprs inner = parser_expr(self);

        parser_expect(self, RightParens);
        return inner;
    }

    if (tok.tag == Strings || tok.tag == Ints  || tok.tag == Floats  || tok.tag == Chars || tok.tag == Trues   || tok.tag == Falses) {
        parser_advance(self);
        return (Exprs){
            .tag  = Expr_Literals,
            .data.literals.range = tok.range
        };
    }

    if (tok.tag == Identifier) {
        SourceRange name = tok.range; parser_advance(self);
        LexerToken next = parser_current(self);

        if (next.tag == LeftParens || next.tag == LeftBrackets) return parser_function_call(self, name);

        if (next.tag == Dots) {
            parser_advance(self);
            LexerToken after_dot = parser_current(self);

            if (after_dot.tag == Identifier) {
                SourceRange method = after_dot.range;
                parser_advance(self);
                if (parser_current(self).tag == LeftParens || parser_current(self).tag == LeftBrackets) {
                    RangeArr gen_params = {0};
                    ParamArr params = {0};

                    if (parser_current(self).tag == LeftBrackets) {
                        parser_advance(self);
                        while (parser_current(self).tag != RightBrackets && parser_current(self).tag != EOFs) {
                            if (parser_current(self).tag == Identifier) ARR_PUSH(gen_params, parser_advance(self).range);
                            if (parser_current(self).tag == Commas) parser_advance(self);
                        }
                        parser_expect(self, RightBrackets);
                    }

                    if (parser_current(self).tag == LeftParens) {
                        parser_advance(self);
                        while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
                            Param p = {0};
                            if (parser_current(self).tag == Identifier) { p.name = parser_advance(self).range; }
                            if (parser_current(self).tag == Colons) parser_advance(self);

                            p.c_type = parser_type(self).data.custom.name; 
                            ARR_PUSH(params, p);
                            if (parser_current(self).tag == Commas) parser_advance(self);
                        }

                        parser_expect(self, RightParens);
                    }

                    SourceRange* gp_heap = NULL;
                    if (gen_params.len) {
                        gp_heap = checked_malloc(gen_params.len * sizeof(SourceRange));
                        memcpy(gp_heap, gen_params.data, gen_params.len * sizeof(SourceRange));
                    }

                    Param* p_heap = NULL;

                    if (params.len) {
                        p_heap = checked_malloc(params.len * sizeof(Param));
                        memcpy(p_heap, params.data, params.len * sizeof(Param));
                    }

                    free(gen_params.data);
                    free(params.data);

                    return (Exprs){
                        .tag = Expr_Class_Calls,
                        .data.class_calls = {
                            .name = name,
                            .function = method,
                            .generic_params = gp_heap,
                            .generic_params_count = gen_params.len,
                            .param = p_heap,
                            .param_count = params.len,
                        }
                    };
                }

                return (Exprs){
                    .tag = Expr_Class_Calls,
                    .data.class_calls = {
                        .name = name,
                        .function = method,
                    }
                };
            }
   
            parse_error(self, ParseErr_UnexpectedToken, "expected identifier after '.'", Identifier);
        }

        return (Exprs){
            .tag  = Expr_Identifiers,
            .data.identifiers.name = name
        };
    }

    if (tok.tag == EOFs) { parse_error_eof(self); return (Exprs){0}; }

    parse_error(self, ParseErr_UnexpectedToken, "unexpected token in expression", 0);
    parser_advance(self);
    return (Exprs){0};
}


static void parse_generic_params(Parser* self, RangeArr* out) {
    if (parser_current(self).tag != LeftBrackets) return; parser_advance(self);

    while (parser_current(self).tag != RightBrackets && parser_current(self).tag != EOFs) {
        if (parser_current(self).tag == Identifier) ARR_PUSH(*out, parser_advance(self).range);
        if (parser_current(self).tag == Commas) parser_advance(self);
    }

    parser_expect(self, RightBrackets);
}

static void parse_param_list_parens(Parser* self, ParamArr* out) {
    if (parser_current(self).tag != LeftParens) return;
    parser_advance(self);
    while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
        Param p = {0};
        if (parser_current(self).tag == Identifier) p.name = parser_advance(self).range;
        if (parser_current(self).tag == Colons) parser_advance(self);

        p.c_type = parser_type(self).data.custom.name;
        ARR_PUSH(*out, p);

        if (parser_current(self).tag == Commas) parser_advance(self);
    }

    parser_expect(self, RightParens);
}

Exprs parser_function_call(Parser* self, SourceRange fn) {
    RangeArr gen  = {0};
    ParamArr args = {0};

    parse_generic_params(self, &gen);
    parse_param_list_parens(self, &args);

    SourceRange* gp = NULL;

    if (gen.len) {
        gp = checked_malloc(gen.len * sizeof(SourceRange));
        memcpy(gp, gen.data, gen.len * sizeof(SourceRange));
    }
    Param* pp = NULL;

    if (args.len) {
        pp = checked_malloc(args.len * sizeof(Param));
        memcpy(pp, args.data, args.len * sizeof(Param));
    }

    size_t gc = gen.len, pc = args.len;

    free(gen.data); 
    free(args.data);

    return (Exprs){
        .tag = Expr_Function,
        .data.function_call = {
            .name = fn,
            .param = pp, .param_count = pc,
            .generic_params = gp, .generic_params_count = gc,
        }
    };
}

Exprs parser_method_calls(Parser* self, SourceRange cls) {
    parser_advance(self);
    SourceRange function = {0};
    if (parser_current(self).tag == Identifier) function = parser_advance(self).range;
    else parse_error(self, ParseErr_ExpectedToken, "expected method name", Identifier);

    RangeArr gen  = {0};
    ParamArr args = {0};
    SourceRange* gp = NULL;
    Param* pp = NULL;

    parse_generic_params(self, &gen);
    parse_param_list_parens(self, &args);

    if (gen.len) { gp = checked_malloc(gen.len * sizeof(SourceRange)); memcpy(gp, gen.data, gen.len * sizeof(SourceRange)); }
    if (args.len) { pp = checked_malloc(args.len * sizeof(Param)); memcpy(pp, args.data, args.len * sizeof(Param)); }

    size_t gc = gen.len, pc = args.len;
    free(gen.data); free(args.data);

    return (Exprs){
        .tag = Expr_Class_Calls,
        .data.class_calls = {
            .name = cls, .function = function,
            .generic_params = gp, .generic_params_count = gc,
            .param = pp,          .param_count = pc,
        }
    };
}

Exprs parser_struct_call(Parser* self, SourceRange str) {
    RangeArr  gen      = {0};
    SourceRange function = {0};
    ParamArr  fields   = {0};

    parse_generic_params(self, &gen);

    if (parser_current(self).tag == Dots) {
        parser_advance(self);
        if (parser_current(self).tag == Identifier) function = parser_advance(self).range;
        else parse_error(self, ParseErr_ExpectedToken, "expected field name", Identifier);
    }

    if (parser_current(self).tag == LeftBraces) {
        parser_advance(self);
        while (parser_current(self).tag != RightBraces && parser_current(self).tag != EOFs) {
            Param p = {0};

            if (parser_current(self).tag == Identifier) p.name = parser_advance(self).range;
            if (parser_current(self).tag == Colons) parser_advance(self); p.c_type = parser_type(self).data.custom.name;
            ARR_PUSH(fields, p);
            if (parser_current(self).tag == Commas) parser_advance(self);
        }

        parser_expect(self, RightBraces);
    }

    SourceRange* gp = NULL;
    Param* fp = NULL;

    if (gen.len) { gp = checked_malloc(gen.len * sizeof(SourceRange)); memcpy(gp, gen.data, gen.len * sizeof(SourceRange)); }
    if (fields.len) { fp = checked_malloc(fields.len * sizeof(Param)); memcpy(fp, fields.data, fields.len * sizeof(Param)); }

    size_t gc = gen.len, fc = fields.len;
    free(gen.data); free(fields.data);

    return (Exprs){
        .tag = Expr_Struct_Calls,
        .data.struct_calls = {
            .name = str, .function = function,
            .generic_params = gp, .generic_params_count = gc,
            .param = fp,          .param_count = fc,
        }
    };
}

Exprs parser_enums_call(Parser* self, SourceRange en) {
    RangeArr gen = {0};
    SourceRange field = {0};
    ParamArr args = {0};
    SourceRange* gp = NULL;
    Param* pp = NULL;

    parse_generic_params(self, &gen);

    if (parser_current(self).tag == Dots) {
        parser_advance(self);

        if (parser_current(self).tag == Identifier) field = parser_advance(self).range;
        else parse_error(self, ParseErr_ExpectedToken, "expected enum variant name", Identifier);
    }

    parse_param_list_parens(self, &args);

    if (gen.len) { gp = checked_malloc(gen.len * sizeof(SourceRange)); memcpy(gp, gen.data, gen.len * sizeof(SourceRange)); }
    if (args.len) { pp = checked_malloc(args.len * sizeof(Param)); memcpy(pp, args.data, args.len * sizeof(Param)); }

    size_t gc = gen.len, pc = args.len;
    free(gen.data); free(args.data);

    return (Exprs){
        .tag = Expr_Enum_Calls,
        .data.enum_calls = {
            .name = en, .field = field,
            .generic_params = gp, .generic_params_count = gc,
            .param = pp,          .param_count = pc,
        }
    };
}

Stmts parser_functions(Parser* self, bool is_const, bool is_unsafe, bool is_pub) {
    parser_advance(self);

    SourceRange n = {0};
    RangeArr gen_params = {0};
    ParamArr params = {0};
    StmtsArr body = {0};
    Type return_type = { .tag = Type_Void };
    SourceRange* gp_heap = NULL;
    Param* p_heap = NULL;
    Stmts* b_heap = NULL;

    if (parser_current(self).tag == Identifier) n = parser_advance(self).range;
    else parse_error(self, ParseErr_ExpectedToken, "expected function name", Identifier);

    parse_generic_params(self, &gen_params);

    if (!parser_expect(self, LeftParens)) { parser_sync(self); }
    while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
        Param p = {0};

        if (parser_current(self).tag == Identifier) p.name = parser_advance(self).range;
        if (parser_current(self).tag == Colons) parser_advance(self); p.c_type = parser_type(self).data.custom.name; ARR_PUSH(params, p);
        if (parser_current(self).tag == Commas) parser_advance(self);
    }

    parser_expect(self, RightParens);

    if (parser_current(self).tag == Colons) { parser_advance(self); return_type = parser_type(self); }
    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) { ARR_PUSH(body, parser_stmt(self)); }

    parser_expect(self, Ends);


    if (gen_params.len) { gp_heap = checked_malloc(gen_params.len * sizeof(SourceRange)); memcpy(gp_heap, gen_params.data, gen_params.len * sizeof(SourceRange)); }
    if (params.len) { p_heap = checked_malloc(params.len * sizeof(Param)); memcpy(p_heap, params.data, params.len * sizeof(Param)); }
    if (body.len) { b_heap = checked_malloc(body.len * sizeof(Stmts)); memcpy(b_heap, body.data, body.len * sizeof(Stmts)); }

    size_t gpc = gen_params.len, pc = params.len, bc = body.len;
    free(gen_params.data); free(params.data); free(body.data);

    return (Stmts){
        .tag = Stmt_Functions,
        .data.functions = {
            .name = n,
            .params = p_heap,  
            .params_count = pc,
            .generic_params = gp_heap, 
            .generic_params_count = gpc,
            .return_type = return_type.data.custom.name,
            .body = b_heap,            
            .body_count = bc,
            .is_unsafe = is_unsafe,
            .is_pub = is_pub,
        }
    };
}

Stmts parser_class(Parser* self, bool is_pub) {
    parser_advance(self);

    SourceRange n       = {0};
    SourceRange parent  = {0};
    RangeArr gen     = {0};
    RangeArr traits  = {0};
    ParamArr cparams = {0};
    StructParamArr fields  = {0};
    MethodArr methods = {0};

    if (parser_current(self).tag == Fors) {
        parser_advance(self);
        while (parser_current(self).tag == Identifier) {
            ARR_PUSH(traits, parser_advance(self).range);

            if (parser_current(self).tag == Commas) parser_advance(self);
        }
    }

    if (parser_current(self).tag == Identifier) n = parser_advance(self).range;
    else parse_error(self, ParseErr_ExpectedToken, "expected class name", Identifier);

    parse_generic_params(self, &gen);

    if (parser_current(self).tag == LeftParens) {
        parser_advance(self);
        while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
            Param p = {0};

            if (parser_current(self).tag == Identifier) p.name = parser_advance(self).range;
            if (parser_current(self).tag == Colons) parser_advance(self);

            p.c_type = parser_type(self).data.custom.name;
            ARR_PUSH(cparams, p);
            if (parser_current(self).tag == Commas) parser_advance(self);
        }

        parser_expect(self, RightParens);
    }

    if (parser_current(self).tag == Greaters) {
        parser_advance(self);
        if (parser_current(self).tag == Identifier) parent = parser_advance(self).range;
        else parse_error(self, ParseErr_ExpectedToken, "expected parent class name", Identifier);
    }

    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        if (parser_current(self).tag == Vars || parser_current(self).tag == Lets) {
            parser_advance(self);
            StructParam f = {0};

            if (parser_current(self).tag == Identifier) f.name = parser_advance(self).range;
            if (parser_current(self).tag == Colons) parser_advance(self);
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

            if (parser_current(self).tag != Identifier) { parser_advance(self); continue; }
            if (!range_eq(parser_current(self).range, "operation")) { parser_advance(self); continue; }

            Operation op = operation_op(self);
            FunctionMethod m = {0};

            if (parser_current(self).tag != Functions) continue;
            Stmts fn = parser_functions(self, false, false, false);

            m.name = fn.data.functions.name;
            m.params = fn.data.functions.params;
            m.params_count = fn.data.functions.params_count;
            m.body = fn.data.functions.body;
            m.body_count = fn.data.functions.body_count;
            m.is_pub = fn.data.functions.is_pub;
            m.is_unsafe = fn.data.functions.is_unsafe;
            m.has_operation = true;
            m.operation = op;
            ARR_PUSH(methods, m);
        } else {
            parse_error(self, ParseErr_UnexpectedToken, "unexpected token in class body", 0);
            parser_advance(self);
        }
    }
    parser_expect(self, Ends);

    SourceRange* gp = NULL; if (gen.len)        { gp = checked_malloc(gen.len*sizeof(SourceRange));       memcpy(gp, gen.data,     gen.len*sizeof(SourceRange)); }
    Param* cp = NULL; if (cparams.len)          { cp = checked_malloc(cparams.len*sizeof(Param));         memcpy(cp, cparams.data, cparams.len*sizeof(Param)); }
    StructParam* fp = NULL; if (fields.len)     { fp = checked_malloc(fields.len*sizeof(StructParam));    memcpy(fp, fields.data,  fields.len*sizeof(StructParam)); }
    FunctionMethod* mp = NULL; if (methods.len) { mp = checked_malloc(methods.len*sizeof(FunctionMethod));memcpy(mp, methods.data, methods.len*sizeof(FunctionMethod)); }
    SourceRange* tp = NULL; if (traits.len)     { tp = checked_malloc(traits.len*sizeof(SourceRange));    memcpy(tp, traits.data,  traits.len*sizeof(SourceRange)); }

    size_t gpc=gen.len, cpc=cparams.len, fpc=fields.len, mpc=methods.len;

    free(gen.data); 
    free(cparams.data); 
    free(fields.data); 
    free(methods.data); 
    free(traits.data);

    return (Stmts){
        .tag = Stmt_Classes,
        .data.classes = {
            .name = n,
            .class_params = cp,     .class_params_count = cpc,
            .fields = fp,           .fields_count = fpc,
            .methods = mp,          .methods_count = mpc,
            .parent = parent,
            .is_pub = is_pub,
            .attached_tag = ClassAttach_None,
        }
    };
}

Stmts parser_structer(Parser* self, bool is_pub, bool is_unsafe) {
    parser_advance(self);

    SourceRange n  = {0};
    RangeArr gen = {0};
    StructParamArr fields = {0};

    if (parser_current(self).tag == Identifier) n = parser_advance(self).range;
    else parse_error(self, ParseErr_ExpectedToken, "expected struct name", Identifier); parse_generic_params(self, &gen);
    if (parser_current(self).tag == Colons) parser_advance(self);

    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        StructParam f = {0};
        switch (parser_current(self).tag) {
            case Vars: {
                f.mode = (VarMode){ .tag = VarMode_Value, .mutability = Mutability_Mutable }; parser_advance(self);

                if (parser_current(self).tag == Identifier) f.name = parser_advance(self).range;
                if (parser_current(self).tag == Colons || parser_current(self).tag == Equalss) parser_advance(self); f.c_type = parser_type(self).data.custom.name; ARR_PUSH(fields, f);
                break;
            }

            case Identifier: {
                f.mode = (VarMode){ .tag = VarMode_Value, .mutability = Mutability_Immutable };
                f.name = parser_advance(self).range;
                
                if (parser_current(self).tag == Colons || parser_current(self).tag == Equalss) parser_advance(self); f.c_type = parser_type(self).data.custom.name; ARR_PUSH(fields, f);
                break;
            }
            default: parse_error(self, ParseErr_UnexpectedToken, "unexpected token in struct body", 0); parser_advance(self); break;
        }
    }
    parser_expect(self, Ends);

    SourceRange* gp = NULL; if (gen.len)    { gp = checked_malloc(gen.len*sizeof(SourceRange));    memcpy(gp, gen.data,    gen.len*sizeof(SourceRange)); }
    StructParam* fp = NULL; if (fields.len) { fp = checked_malloc(fields.len*sizeof(StructParam)); memcpy(fp, fields.data, fields.len*sizeof(StructParam)); }
    size_t gpc=gen.len, fpc=fields.len;

    free(gen.data); 
    free(fields.data);

    return (Stmts){
        .tag = Stmt_Structs,
        .data.structs = {
            .name = n,
            .generic_params = gp, .generic_params_count = gpc,
            .fields = fp,         .fields_count = fpc,
            .is_pub = is_pub,
        }
    };
}

Stmts parser_enums(Parser* self, bool is_pub, bool is_unsafe) {
    parser_advance(self);

    SourceRange n = {0};
    RangeArr gen = {0};
    VariantArr variants = {0};

    if (parser_current(self).tag == Identifier) n = parser_advance(self).range;
    else parse_error(self, ParseErr_ExpectedToken, "expected enum name", Identifier); parse_generic_params(self, &gen);
    if (parser_current(self).tag == Colons) parser_advance(self);

    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        if (parser_current(self).tag != Identifier) { parse_error(self, ParseErr_UnexpectedToken, "expected enum variant name", Identifier); parser_advance(self); continue; }
        
        EnumVariant v = { .name = parser_advance(self).range };

        if (parser_current(self).tag == LeftParens) {
            parser_advance(self);
            EnumFieldArr ef = {0};
            while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
                SourceRange fname = {0}, ftype_r = {0};
                
                if (parser_current(self).tag == Identifier) fname = parser_advance(self).range;
                if (parser_current(self).tag == Colons) parser_advance(self);

                ftype_r = parser_type(self).data.custom.name;
                EnumField ef_item = { fname, ftype_r };
                ARR_PUSH(ef, ef_item);

                if (parser_current(self).tag == Commas) parser_advance(self);
            }

            parser_expect(self, RightParens);

            if (ef.len) {
                v.fields = checked_malloc(ef.len * sizeof(EnumField));
                memcpy(v.fields, ef.data, ef.len * sizeof(EnumField));
                v.fields_count = ef.len;
            }
            free(ef.data);
        }

        ARR_PUSH(variants, v);
        if (parser_current(self).tag == Commas) parser_advance(self);
    }
    parser_expect(self, Ends);

    SourceRange* gp = NULL; if (gen.len)      { gp = checked_malloc(gen.len*sizeof(SourceRange));      memcpy(gp, gen.data,      gen.len*sizeof(SourceRange)); }
    EnumVariant* vp = NULL; if (variants.len) { vp = checked_malloc(variants.len*sizeof(EnumVariant)); memcpy(vp, variants.data, variants.len*sizeof(EnumVariant)); }
    size_t gpc=gen.len, vc=variants.len;

    free(gen.data); 
    free(variants.data);

    return (Stmts){
        .tag = Stmt_Enums,
        .data.enums = {
            .name = n,
            .generic_params = gp, .generic_params_count = gpc,
            .variants = vp,       .variants_count = vc,
            .is_pub = is_pub,
        }
    };
}

Stmts parser_traits(Parser* self, bool is_pub, bool is_unsafe) {
    parser_advance(self);

    SourceRange n   = {0};
    RangeArr gen = {0};
    RangeArr types = {0};
    TraitMethodArr methods = {0};

    if (parser_current(self).tag == Identifier) n = parser_advance(self).range;
    else parse_error(self, ParseErr_ExpectedToken, "expected trait name", Identifier);

    parse_generic_params(self, &gen);
    if (parser_current(self).tag == Colons) parser_advance(self);

    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        switch (parser_current(self).tag) {
            case Types:
                parser_advance(self);
                if (parser_current(self).tag == Identifier) ARR_PUSH(types, parser_advance(self).range);
                else parse_error(self, ParseErr_ExpectedToken, "expected associated type name", Identifier);
                break;
            case Functions: {
                Stmts fn = parser_functions(self, false, false, false);
                TraitMethod m = {0};
                m.name        = fn.data.functions.name;
                m.params      = fn.data.functions.params;
                m.params_count= fn.data.functions.params_count;
                m.body        = fn.data.functions.body;
                m.body_count  = fn.data.functions.body_count;
                m.is_pub      = fn.data.functions.is_pub;
                m.return_type = fn.data.functions.return_type;
                ARR_PUSH(methods, m);
                break;
            }
            default: parse_error(self, ParseErr_UnexpectedToken, "unexpected token in trait body", 0); parser_advance(self); break;
        }
    }
    parser_expect(self, Ends);

    SourceRange*  gp = NULL; if (gen.len)     { gp = checked_malloc(gen.len*sizeof(SourceRange));       memcpy(gp, gen.data,     gen.len*sizeof(SourceRange)); }
    TraitMethod* mp  = NULL; if (methods.len) { mp = checked_malloc(methods.len*sizeof(TraitMethod));   memcpy(mp, methods.data, methods.len*sizeof(TraitMethod)); }
    size_t gpc=gen.len, mc=methods.len;
    
    free(gen.data); 
    free(types.data); 
    free(methods.data);

    return (Stmts){
        .tag = Stmt_Traits,
        .data.traits = {
            .name = n,
            .methods = mp, .methods_count = mc,
            .is_pub  = is_pub,
        }
    };
}


Stmts parser_variable(Parser* self, StmtsTag tag, bool is_pub) {
    parser_advance(self);

    SourceRange n = {0}; 
    SourceRange t = {0};
    Exprs value = {0};
    bool has_value = false;

    if (parser_current(self).tag == Identifier) n = parser_advance(self).range;
    else parse_error(self, ParseErr_ExpectedToken, "expected variable name", Identifier);

    if (parser_current(self).tag == Colons) { parser_advance(self); t = parser_type(self).data.custom.name; }
    if (parser_current(self).tag == Equalss) { parser_advance(self); value = parser_expr(self); has_value = true; }

    return (Stmts){
        .tag = tag,
        .data.vars = { 
            .name = n, 
            .c_type = t, 
            .value = value, 
            .has_value = has_value,
            .mode = is_pub
        }
    };
}

Stmts parser_globle(Parser* self, bool is_pub, bool is_const) {
    Stmts s = parser_variable(self, Stmt_Locals, is_pub);
    return s;
}

Stmts parser_return(Parser* self) {
    SourceRange range = parser_current(self).range; parser_advance(self);
    Exprs value = {0};

    LexerTokenTag t = parser_current(self).tag;
    if (t != Ends && t != Semicolons && t != EOFs && t != Elses && t != Elifs) { value = parser_expr(self); }

    return (Stmts){
        .tag = Stmt_Returns,
        .data.returns = { .expr = value, .range = range }
    };
}


Stmts operation_atom(Parser* self) {
    parser_advance(self);
    uint64_t bits    = 0;
    bool _signed = false;

    if (parser_current(self).tag != LeftParens)  goto fail; parser_advance(self);
    if (parser_current(self).tag != Identifier || !range_eq(parser_current(self).range,"bits")) goto fail; parser_advance(self);
    if (parser_current(self).tag != Equalss) goto fail; parser_advance(self);
    if (parser_current(self).tag != Ints) goto fail; bits = parser_current(self).data.value_int;
    if (bits!=2&&bits!=4&&bits!=8&&bits!=16&&bits!=32&&bits!=64&&bits!=128) goto fail; parser_advance(self);
    if (parser_current(self).tag != Commas) goto fail; parser_advance(self);
    if (parser_current(self).tag != Identifier || !range_eq(parser_current(self).range,"signed")) goto fail; parser_advance(self);
    if (parser_current(self).tag != Equalss) goto fail; parser_advance(self);
    if (parser_current(self).tag != Trues && parser_current(self).tag != Falses) goto fail; _signed = parser_current(self).tag == Trues; parser_advance(self);
    if (parser_current(self).tag != RightParens) goto fail; parser_advance(self);

    return (Stmts){ .tag = 0 };

    fail: parse_error(self, ParseErr_UnexpectedToken, "malformed @atom(bits=N, signed=B)", 0); return (Stmts){0};
}

Operation operation_op(Parser* self) {
    parser_advance(self);
    LexerTokenTag op = 0;

    if (parser_current(self).tag != LeftParens) goto fail; parser_advance(self);
    if (!is_operation(parser_current(self).tag)) { parse_error(self, ParseErr_UnexpectedToken, "expected operator in @operation(...)", 0); goto fail; } op = parser_current(self).tag; parser_advance(self);
    if (parser_current(self).tag != RightParens) goto fail; parser_advance(self);

    return (Operation){ .op = op };

    fail: parse_error(self, ParseErr_UnexpectedToken, "malformed @operation(op)", 0); return (Operation){0};
}

Stmts parser_operation(Parser* self) {
    parser_advance(self);

    if (parser_current(self).tag != Identifier) { parse_error(self, ParseErr_ExpectedToken, "expected 'operation' or 'atom' after '@'", Identifier); return (Stmts){0}; }

    SourceRange range = parser_current(self).range;

    if (range_eq(range, "operation")) { operation_op(self);   return (Stmts){0}; }
    if (range_eq(range, "atom"))      { operation_atom(self); return (Stmts){0}; }

    parse_error(self, ParseErr_UnexpectedToken, "unknown @ attribute", 0);
    parser_advance(self);
    return (Stmts){0};
}

Stmts parser_stmt(Parser* self) {
    LexerToken tok = parser_current(self);

    if (tok.tag == Publics) {
        parser_advance(self);
        bool unsafe = false;
        if (parser_current(self).tag == Unsafes) { unsafe = true; parser_advance(self); }
        switch (parser_current(self).tag) {
            case Functions: return parser_functions(self, false, unsafe, true);
            case Classes:   return unsafe ? (parser_advance(self),(Stmts){0}) : parser_class(self, true);
            case Structs:   return parser_structer(self, true, unsafe);
            case Enums:     return parser_enums(self,    true, unsafe);
            case Traits:    return parser_traits(self,   true, unsafe);
            default: parse_error(self, ParseErr_UnexpectedToken, "expected declaration after 'pub'", 0); parser_advance(self); return (Stmts){0};
        }
    }

    if (tok.tag == Unsafes) {
        parser_advance(self);
        switch (parser_current(self).tag) {
            case Functions: return parser_functions(self, false, true, false);
            case Structs:   return parser_structer(self, false, true);
            case Enums:     return parser_enums(self,    false, true);
            case Traits:    return parser_traits(self,   false, true);
            default: parse_error(self, ParseErr_UnexpectedToken, "expected declaration after 'unsafe'", 0); parser_advance(self); return (Stmts){0};
        }
    }

    if (tok.tag == Consts) {
        parser_advance(self);
        switch (parser_current(self).tag) {
            case Functions: return parser_functions(self, true, false, false);
            case Locals:    return parser_globle(self, false, true);
            default:        return parser_const(self);
        }
    }

    switch (tok.tag) {
        case Functions: return parser_functions(self, false, false, false);
        case Classes:   return parser_class(self, false);
        case Structs:   return parser_structer(self, false, false);
        case Enums:     return parser_enums(self,    false, false);
        case Traits:    return parser_traits(self,   false, false);
        case Returns:   return parser_return(self);
        case Locals:    return parser_globle(self, false, false);
        case Vars:      return parser_vars(self);
        case Lets:      return parser_lets(self);
        case Ats:       return parser_operation(self);

        case Identifier: {
            SourceRange name = parser_current(self).range; parser_advance(self);
            LexerTokenTag op = parser_current(self).tag;

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
                                .tag  = Expr_Identifiers,
                                .data.identifiers.name = name
                            },
                            .op    = op,
                            .value = value,
                        }
                    };
                }

                case LeftParens:
                case LeftBrackets: {
                    Exprs call = parser_function_call(self, name);
                    return (Stmts){
                        .tag = Stmt_ExprStmt,
                        .data.expr_stmt.expr = call
                    };
                }

                case Dots: {
                    Exprs call = parser_method_calls(self, name);
                    return (Stmts){
                        .tag = Stmt_ExprStmt,
                        .data.expr_stmt.expr = call
                    };
                }
                default: {
                    return (Stmts){
                        .tag = Stmt_ExprStmt,
                        .data.expr_stmt.expr = (Exprs){
                            .tag  = Expr_Identifiers,
                            .data.identifiers.name = name
                        }
                    };
                }
            }
        }

        case EOFs: parse_error_eof(self); return (Stmts){0};
        case Semicolons: parser_advance(self); return (Stmts){0};
        default: parse_error(self, ParseErr_UnexpectedToken, "unexpected token at statement level", 0); parser_advance(self); return (Stmts){0};
    }
}