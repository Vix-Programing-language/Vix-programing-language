#include "ast.h"
#include "import.h"

typedef struct {
    LexerToken* tokens;
    size_t count;
    size_t pos;
} Parser;


Parser parser_new(LexerToken* tokens, size_t count) {
    return (Parser){
        .tokens = tokens,
        .count  = count,
        .pos    = 0,
    };
}

void parser_free(Parser* self) {
    self->tokens = NULL;
}

LexerToken parser_current(Parser* self) {
    return self->tokens[self->pos];
}

LexerToken parser_peek(Parser* self) {
    return self->tokens[self->pos + 1];
}

LexerToken parser_advance(Parser* self) {
    return self->tokens[self->pos++];
}

bool is_type(Parser* self) {
    if  (strcmp(self, "int") == 0) return true;
    else if (strcmp(self, "int8")    == 0) return true;
    else if (strcmp(self, "int16")   == 0) return true;
    else if (strcmp(self, "int32")   == 0) return true;
    else if (strcmp(self, "int64")   == 0) return true;
    else if (strcmp(self, "float")   == 0) return true;
    else if (strcmp(self, "float32") == 0) return true;
    else if (strcmp(self, "float64") == 0) return true;
    else if (strcmp(self, "char")    == 0) return true;
    else if (strcmp(self, "string")  == 0) return true;
    else { return false; }
}

Stmts parser_stmt(Parser* self) {
    parser_advance(self);

    if (parser_current(self).tag == Publics) {
        parser_advance(self);

        if (parser_current(self).tag == Unsafes) {
            parser_advance(self);

            switch (parser_current(self).tag) {
                case Functions: parser_functions(self, false, true, true); parser_advance(self); break;
                case Structs: parser_structer(self, true, true); parser_advance(self); break;
                case Enums: parser_enums(self, true, true); parser_advance(self); break;
                case Traits: parser_traits(self, true, true); parser_advance(self); break;

                default: break;
            }
        }

        switch (parser_current(self).tag) {
            case Functions: parser_functions(self, false, false, true); parser_advance(self); break;
            case Classes: parser_class(self, true); parser_advance(self); break;
            case Structs: parser_structer(self, true, false); parser_advance(self); break;
            case Enums: parser_enums(self, true, false); parser_advance(self); break;
            case Traits: parser_traits(self, true, false); parser_advance(self); break;
            case Locals: parser_static(self, false, true); parser_advance(self); break;

            default: break;
        }
    } else if (parser_current(self).tag == Unsafes) {
        parser_advance(self);

        switch (parser_current(self).tag) {
            case Functions: parser_functions(self, false, true, false); parser_advance(self); break;
            case Structs: parser_structer(self, true, false); parser_advance(self); break;
            case Enums: parser_enums(self, true, false); parser_advance(self); break;
            case Traits: parser_traits(self, true, false); parser_advance(self); break;

            default: break;
        }
    } else if (parser_current(self).tag == Consts) {
        parser_advance(self);

        switch (parser_current(self).tag) {
            case Functions: parser_functions(self, true, false, false); parser_advance(self); break;
            case Locals: parser_static(self, false, true); parser_advance(self); break;
            case Vars: parser_vars(self, false); parser_advance(self); break;
            case Lets: parser_lets(self, false); parser_advance(self); break;

            default: break;
        }
    } else {
        switch (parser_current(self).tag) {
            case Functions: parser_functions(self, false, false, false); parser_advance(self); break;
            case Classes: parser_class(self, false); parser_advance(self); break;
            case Structs: parser_structer(self, false, false); parser_advance(self); break;
            case Enums: parser_enums(self, false, false); parser_advance(self); break;
            case Traits: parser_traits(self, false, false); parser_advance(self); break;

            default: break;
        }
    }
}

Type parser_type(Parser* self) {
    switch (parser_current(self).tag) {
        case Ints: { uint64_t bits = parser_current(self).data.value_int; parser_advance(self); return (Type){ .tag = Type_Int,   .data = { .int_t   = { .bits = bits } } }; }
        case Floats: { uint64_t bits = parser_current(self).data.value_int; parser_advance(self); return (Type){ .tag = Type_Float, .data = { .float_t = { .bits = bits } } }; }
        case Chars: { parser_advance(self); return (Type){ .tag = Type_Char }; }
        case Strings: { parser_advance(self); return (Type){ .tag = Type_Str  }; }
        case Trues:
        case Falses:
        case Identifier: {
            char* name = parser_current(self).data.s;
            parser_advance(self);
            if (strcmp(name, "bool")  == 0) return (Type){ .tag = Type_Bool };
            if (strcmp(name, "void")  == 0) return (Type){ .tag = Type_Void };
            if (strcmp(name, "str")   == 0) return (Type){ .tag = Type_Str  };
            if (strcmp(name, "char")  == 0) return (Type){ .tag = Type_Char };
            return (Type){ .tag = Type_Custom, .data = { .custom = { .name = name } } };
        }

        default: return (Type){ .tag = Type_Void };
    }
}

Stmts parser_functions(Parser* self, bool is_const, bool is_unsafe, bool is_pub) {
    parser_advance(self);

    char* n;
    Type return_type = { .tag = Type_Void };
    Param params[64];
    size_t params_count = 0;
    Stmts body[256];
    size_t body_count = 0;
    char* generic_params[16];
    size_t generic_params_count = 0;

    if (parser_current(self).tag == Identifier) { n = parser_current(self).data.s; parser_advance(self); } else { /* Error */ }
    if (parser_current(self).tag == LeftBrackets) {
        parser_advance(self);

        while (parser_current(self).tag != RightBrackets && parser_current(self).tag != EOFs) {
            if (parser_current(self).tag == Identifier) { generic_params[generic_params_count++] = parser_current(self).data.s; parser_advance(self); }
            if (parser_current(self).tag == Commas) parser_advance(self);
        }

        if (parser_current(self).tag == RightBrackets) parser_advance(self);
    }

    if (parser_current(self).tag == LeftParens) { parser_advance(self); }

    while (parser_current(self).tag != RightParens) {
        Param p;

        if (parser_current(self).tag == Identifier) { p.name = parser_current(self).data.s; parser_advance(self); }
        if (parser_current(self).tag == Colons) { parser_advance(self); }

        p.c_type = parser_type(self).data.custom.name;
        params[params_count++] = p;

        if (parser_current(self).tag == Commas) parser_advance(self);
    }

    if (parser_current(self).tag == RightParens) parser_advance(self);
    if (parser_current(self).tag == Colons) { parser_advance(self); return_type = parser_type(self); }

    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) { body[body_count++] = parser_stmt(self); }

    if (parser_current(self).tag == Ends) parser_advance(self);

    return (Stmts){
        .tag = Stmt_Functions,
        .data = { .functions = {
            .name = n,
            .params = params,
            .params_count = params_count,
            .generic_params = generic_params,
            .generic_params_count = generic_params_count,
            .return_type = return_type.data.custom.name,
            .body = body,
            .body_count = body_count,
            .is_unsafe = is_unsafe,
            .is_pub = is_pub,
        }}
    };
}

Stmts parser_class(Parser* self, bool is_pub) {
    parser_advance(self);

    char* n;
    char* parent = NULL;
    Param class_params[64];
    size_t class_params_count = 0;
    StructParam fields[64];
    size_t fields_count = 0;
    FunctionMethod methods[64];
    size_t methods_count = 0;
    char* generic_params[16];
    size_t generic_params_count = 0;
    char* traits[16];
    size_t traits_count = 0;

    if (parser_current(self).tag == Fors) {
        parser_advance(self);

        while (parser_current(self).tag == Identifier) {
            traits[traits_count++] = parser_current(self).data.s;
            parser_advance(self);
            if (parser_current(self).tag == Commas) parser_advance(self);
        }
    }

    if (parser_current(self).tag == Identifier) { n = parser_current(self).data.s; parser_advance(self); } else { /* Error */ }

    if (parser_current(self).tag == LeftBrackets) {
        parser_advance(self);
        while (parser_current(self).tag != RightBrackets && parser_current(self).tag != EOFs) {
            if (parser_current(self).tag == Identifier) { generic_params[generic_params_count++] = parser_current(self).data.s; parser_advance(self); }
            if (parser_current(self).tag == Commas) parser_advance(self);
        }

        if (parser_current(self).tag == RightBrackets) parser_advance(self);
    }

    if (parser_current(self).tag == LeftParens) {
        parser_advance(self);

        while (parser_current(self).tag != RightParens && parser_current(self).tag != EOFs) {
            Param p;
            if (parser_current(self).tag == Identifier) { p.name = parser_current(self).data.s; parser_advance(self); }
            if (parser_current(self).tag == Colons) { parser_advance(self); }

            p.c_type = parser_type(self).data.custom.name;
            class_params[class_params_count++] = p;

            if (parser_current(self).tag == Commas) parser_advance(self);
        }

        if (parser_current(self).tag == RightParens) parser_advance(self);
    }

    if (parser_current(self).tag == Greaters) {
        parser_advance(self);
        if (parser_current(self).tag == Identifier) { parent = parser_current(self).data.s; parser_advance(self); }
    }
    
    while (parser_current(self).tag != Ends && parser_current(self).tag != EOFs) {
        if (parser_current(self).tag == Vars || parser_current(self).tag == Lets) {
            StructParam f;
            parser_advance(self);
            if (parser_current(self).tag == Identifier) { f.name = parser_current(self).data.s; parser_advance(self); }
            if (parser_current(self).tag == Colons)     { parser_advance(self); }
            f.c_type = parser_type(self).data.custom.name;
            fields[fields_count++] = f;
        } else if (parser_current(self).tag == Functions) {
            Stmts fn = parser_functions(self, false, false, false);
            FunctionMethod m;

            m.name = fn.data.functions.name;
            m.params = fn.data.functions.params;
            m.params_count = fn.data.functions.params_count;
            m.body = fn.data.functions.body;
            m.body_count = fn.data.functions.body_count;
            m.is_pub = fn.data.functions.is_pub;
            m.is_unsafe = fn.data.functions.is_unsafe;
            methods[methods_count++] = m;
        } else {
            parser_advance(self);
        }
    }

    if (parser_current(self).tag == Ends) parser_advance(self);

    return (Stmts){
        .tag = Stmt_Classes,
        .data = { .classes = {
            .name = n,
            .class_params = class_params,
            .class_params_count = class_params_count,
            .fields = fields,
            .fields_count = fields_count,
            .methods = methods,
            .methods_count = methods_count,
            .parent = parent,
            .is_pub = is_pub,
        }}
    };
}
