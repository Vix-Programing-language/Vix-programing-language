#include "import.h"
#include "register.h"
#include "ir.h"
#include "import.h"
#include "third-party/khashl.h"
#include "helper.h"
#include "register_tables.h"



Type type_from_literal(SourceRange r) {
    if (!r.start || r.start == r.end) return (Type){ .tag = Type_Void };

    size_t len = r.end - r.start;

    if (len >= 2 && r.start[0] == '"') return (Type){ .tag = Type_Str };
    if (len >= 2 && r.start[0] == '\'') return (Type){ .tag = Type_Char };
    if (len == 4 && memcmp(r.start, "true", 4) == 0)  return (Type){ .tag = Type_Bool };
    if (len == 5 && memcmp(r.start, "false", 5) == 0) return (Type){ .tag = Type_Bool };

    bool has_dot = false;
    for (size_t i = 0; i < len; i++) {
        if (r.start[i] == '.') { has_dot = true; break; }
    }

    if (has_dot) return (Type){ .tag = Type_Float, .data.float_t = { .bits = 32 } };
    return (Type){ .tag = Type_Int, .data.int_t = { .bits = 32, .is_unsigned = false } };
}


Type infer_expr_type(Register* reg, Exprs* e) {
    switch (e->tag) {
        case Expr_Literals:
            return type_from_literal(e->data.literals.range);

        case Expr_Identifiers: {
            StringView sv = sv_from_range(e->data.identifiers.name);
            RegisterEntry* found = register_get(reg, sv);
            if (found && (found->tag == Reg_Var || found->tag == Reg_Let || found->tag == Reg_Param)) {
                return found->data.var.type;
            }
            return (Type){0};
        }

        case Expr_Function: {
            StringView sv = sv_from_range(e->data.function_call.name);
            RegisterEntry* found = register_get(reg, sv);
            if (found && found->tag == Reg_Function) return found->data.function.return_type;
            if (found && found->tag == Reg_ExternFunc) return found->data.extern_func.return_type;
            return (Type){0};
        }

        case Expr_Tuple: {
            size_t n = e->data.tuple.elems_count;
            Type* elems = n ? malloc(sizeof(Type) * n) : NULL;
            for (size_t i = 0; i < n; i++) {
                elems[i] = infer_expr_type(reg, &e->data.tuple.elems[i]);
            }
            return (Type){ .tag = Type_Tuple, .data.tuple = { .elems = elems, .elems_count = n } };
        }
        default:
            return (Type){0};
    }
}

Type type_from_range(SourceRange r) {
    if (!r.start || r.start == r.end) {
        return (Type){ .tag = Type_Void };
    }

    size_t len = r.end - r.start;

    if (len == 4 && memcmp(r.start, "bool", 4) == 0) return (Type){ .tag = Type_Bool };
    if (len == 3 && memcmp(r.start, "str", 3)  == 0)  return (Type){ .tag = Type_Str }; 
    if (len == 4 && memcmp(r.start, "char", 4) == 0)  return (Type){ .tag = Type_Char };
    if (len == 4 && memcmp(r.start, "void", 4) == 0)  return (Type){ .tag = Type_Void };

    if (len == 3 && memcmp(r.start, "int", 3)   == 0)  return (Type){ .tag = Type_Int, .data.int_t = { .bits = 32, .is_unsigned = false } };
    if (len == 4 && memcmp(r.start, "int8", 4)  == 0)  return (Type){ .tag = Type_Int, .data.int_t = { .bits = 8,  .is_unsigned = false } };
    if (len == 5 && memcmp(r.start, "int16", 5) == 0)  return (Type){ .tag = Type_Int, .data.int_t = { .bits = 16, .is_unsigned = false } };
    if (len == 5 && memcmp(r.start, "int32", 5) == 0)  return (Type){ .tag = Type_Int, .data.int_t = { .bits = 32, .is_unsigned = false } };
    if (len == 5 && memcmp(r.start, "int64", 5) == 0)  return (Type){ .tag = Type_Int, .data.int_t = { .bits = 64, .is_unsigned = false } };

    if (len == 4 && memcmp(r.start, "uint", 4)   == 0)  return (Type){ .tag = Type_Int, .data.int_t = { .bits = 32, .is_unsigned = true } };
    if (len == 5 && memcmp(r.start, "uint8", 5)  == 0)  return (Type){ .tag = Type_Int, .data.int_t = { .bits = 8,  .is_unsigned = true } };
    if (len == 6 && memcmp(r.start, "uint16", 6) == 0)  return (Type){ .tag = Type_Int, .data.int_t = { .bits = 16, .is_unsigned = true } };
    if (len == 6 && memcmp(r.start, "uint32", 6) == 0)  return (Type){ .tag = Type_Int, .data.int_t = { .bits = 32, .is_unsigned = true } };
    if (len == 6 && memcmp(r.start, "uint64", 6) == 0)  return (Type){ .tag = Type_Int, .data.int_t = { .bits = 64, .is_unsigned = true } };

    if (len == 5 && memcmp(r.start, "float", 5)   == 0) return (Type){ .tag = Type_Float, .data.float_t = { .bits = 32 } };
    if (len == 7 && memcmp(r.start, "float32", 7) == 0) return (Type){ .tag = Type_Float, .data.float_t = { .bits = 32 } };
    if (len == 7 && memcmp(r.start, "float64", 7) == 0) return (Type){ .tag = Type_Float, .data.float_t = { .bits = 64 } };

    return (Type){ .tag = Type_Custom, .data.custom.name = r };
}


FieldOwnerKind get_kind(Register* reg, SourceRange type_name) {
    RegisterEntry* type_entry = register_get(reg, sv_from_range(type_name));
    if (!type_entry) return FieldOwner_Unknown;
    switch (type_entry->tag) {
        case Reg_Struct: return FieldOwner_Struct;
        case Reg_Class:  return FieldOwner_Class;
        case Reg_Enum:   return FieldOwner_Enum;
        default:         return FieldOwner_Unknown;
    }
}


Type get_function_type(RegisterEntry* cond_entry) {
    if (!cond_entry) {
        return (Type){0};
    }

    int tags[] = { Reg_Function, Reg_ExternFunc };
    RegisterEntry* fn = register_by_target(sv_from_range(cond_entry->data.expr_function_call.name), tags, 2);
  
    if (!fn) {
        return (Type){0};
    }


    switch (fn->tag) {
        case Reg_Function: {
            Type r = fn->data.function.return_type;
            return r;
        }
        case Reg_ExternFunc: {
            Type r = fn->data.extern_func.return_type;
            return r;
        }
        default:
            return (Type){0};
    }
}


SourceRange register_generate_name(Register* reg, SourceRange inner_bind) {
    size_t bind_len = inner_bind.end - inner_bind.start;

    size_t base_len = 4 + bind_len;
    char* base = malloc(base_len + 1);
    memcpy(base, "tmp_", 4);
    memcpy(base + 4, inner_bind.start, bind_len);
    base[base_len] = '\0';

    StringView sv = { .ptr = base, .len = base_len };
    if (!register_get(reg, sv)) {
        return (SourceRange){ .start = base, .end = base + base_len, .file_id = inner_bind.file_id };
    }

    for (uint32_t n = 1; ; n++) {
        char suffix[16];
        int slen = snprintf(suffix, sizeof(suffix), "_%u", n);
        size_t total = base_len + slen;
        char* buf = malloc(total + 1);
        memcpy(buf, base, base_len);
        memcpy(buf + base_len, suffix, slen);
        buf[total] = '\0';

        StringView candidate = { .ptr = buf, .len = total };
        if (!register_get(reg, candidate)) {
            free(base);
            return (SourceRange){ .start = buf, .end = buf + total, .file_id = inner_bind.file_id };
        }
        free(buf);
    }
}

bool is_generic_type(RegisterEntry* e) {
    if (!e) return false;
    switch (e->tag) {
        case Reg_Struct: return e->data.strct.generic_params_count > 0;
        case Reg_Class:  return e->data._class.generic_params_count > 0;
        case Reg_Enum:   return e->data.enm.generic_params_count   > 0;
        case Reg_Function: return e->data.function.generic_params_count > 0;
        default: return false;
    }
}

SourceRange mangle_name(SourceRange class_name, SourceRange method_name) {
    size_t clen = class_name.end - class_name.start;
    size_t mlen = method_name.end - method_name.start;
    size_t total = mlen + 1 + clen;
    char* buf = malloc(total + 1);
    memcpy(buf, method_name.start, mlen);
    buf[mlen] = '_';
    memcpy(buf + mlen + 1, class_name.start, clen);
    buf[total] = '\0';
    return (SourceRange){ .start = buf, .end = buf + total, .file_id = method_name.file_id };
}

SourceRange mangle_name_unique(Register* reg, SourceRange class_name, SourceRange method_name) {
    SourceRange base = mangle_name(class_name, method_name);
    
    StringView sv = sv_from_range(base);
    if (!register_get(reg, sv)) return base;

    size_t base_len = base.end - base.start;
    for (int suffix = 1; ; suffix++) {
        char num[16];
        int nlen = snprintf(num, sizeof(num), "_%d", suffix);
        char* buf = malloc(base_len + nlen + 1);

        memcpy(buf, base.start, base_len);
        memcpy(buf + base_len, num, nlen);
        buf[base_len + nlen] = '\0';

        StringView candidate = { .ptr = buf, .len = base_len + nlen };
        if (!register_get(reg, candidate)) {
            return (SourceRange){
                .start = buf,
                .end = buf + base_len + nlen,
                .file_id = method_name.file_id
            };
        }
        free(buf);
    }
}



bool is_variable(RegisterEntry* e) {
    return e && (e->tag == Reg_Var || e->tag == Reg_Let || e->tag == Reg_Param);
}

bool is_user_defined_type(RegisterEntry* e) {
    return e && (e->tag == Reg_Enum || e->tag == Reg_Struct || e->tag == Reg_Class);
}

void range_to_span(SourceRange r, LineStarts* ls, uint32_t* line_start, uint16_t* col_start, uint32_t* line_end, uint16_t* col_end) {
    size_t ls_idx = get_line_num(ls, (uintptr_t)r.start);
    size_t le_idx = get_line_num(ls, (uintptr_t)r.end);

    *line_start = (ls_idx != (size_t)-1) ? (uint32_t)ls_idx : 0;
    *line_end = (le_idx != (size_t)-1) ? (uint32_t)le_idx : 0;

    *col_start = (ls_idx != (size_t)-1) ? (uint16_t)(r.start - ls->data[ls_idx]) : 0;
    *col_end = (le_idx != (size_t)-1) ? (uint16_t)(r.end - ls->data[le_idx])   : 0;
}


bool range_eq(SourceRange r, const char* str) {
    size_t len = r.end - r.start;
    return strlen(str) == len && memcmp(r.start, str, len) == 0;
}

Type* type_heap(Type t) {
    switch (t.tag) {
        case Type_Void:
        case Type_Custom:
            return NULL;
        default: {
            Type* p = malloc(sizeof(Type));
            *p = t;
            return p;
        }
    }
}


bool is_type_token(LexerTokenTag tag) {
    return tag == Identifier || tag == TypeToken || tag == Ints || tag == Floats || tag == Chars || tag == Strings || tag == Bools || tag == Voids;
}

bool is_operation(LexerTokenTag tag) {
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

bool expr_is_empty(Exprs* expr) {
    if (expr == NULL) return true;
    if (expr->tag == Expr_Null) return true;
    if (expr->tag == Expr_Literals && expr->data.literals.range.start == expr->data.literals.range.end) return true;
    if (expr->tag == Expr_Function && expr->data.function_call.name.start == NULL) return true;

    return false;
}

bool source_range_eq(SourceRange a, SourceRange b) {
    size_t len_a = a.end - a.start;
    size_t len_b = b.end - b.start;
    return (len_a == len_b) && (memcmp(a.start, b.start, len_a) == 0);
}

bool is_enum_variant(const RegisterEntry* entry, SourceRange name) {
    for (size_t i = 0; i < entry->data.enm.variants_count; i++) {
        if (source_range_eq(entry->data.enm.variants[i].name, name)) {
            return true;
        }
    }
    return false;
}
