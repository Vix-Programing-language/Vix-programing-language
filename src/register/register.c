#include "import.h"
#include "register.h"
#include "ir.h"
#include "import.h"
#include "third-party/khashl.h"
#include "helper.h"

StringView sv_from_range(SourceRange r) {
    return (StringView){ .ptr = r.start, .len = (size_t)(r.end - r.start) };
}

Register register_new(Register* parent, IDCounter* counter) {
    return (Register){
        .table = register_table_init(),
        .parent = parent,
        .pending = pending_table_init(),
        .counter = counter,
        .mono = NULL,
    };
}



EntityID register_insert(Register* reg, RegisterEntry entry) {
    entry.eid = (EntityID){ .id = reg->counter->next_id++, .kind = entry.tag };

    StringView name_key = sv_from_range(entry.decl_name_range);
    if (name_key.ptr && name_key.len > 0) {
        int absent;
        khint_t k = register_table_put(reg->table, name_key, &absent);
        if (absent > 0) kh_val(reg->table, k) = entry;
    }

    char* buf = malloc(32);
    snprintf(buf, 32, "%u", entry.eid.id);
    StringView id_key = { .ptr = buf, .len = strlen(buf) };
    int absent;
    khint_t k = register_table_put(reg->table, id_key, &absent);
    if (absent > 0) kh_val(reg->table, k) = entry;

    return entry.eid;
}

RegisterEntry* register_get_by_id(Register* reg, uint32_t id) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", id);
    StringView key = { .ptr = buf, .len = strlen(buf) };
    return register_lookup(reg, key);
}

Register* register_get_child(Register* reg, uint32_t id) {
    RegisterEntry* e = register_get_by_id(reg, id);
    if (!e) return NULL;

    switch (e->tag) {
        case Reg_Function: return e->data.function.child_reg;
        case Reg_Extern:   return e->data.extern_.child_reg;
        case Reg_If:
        case Reg_Elif:     return e->data.if_.then_child;
        case Reg_While:    return e->data.while_.body_child;
        case Reg_For:      return e->data.for_.body_child;
        case Reg_Match:    return e->data.match_.expr_child;
        default:           return NULL;
    }
}

bool register_insert_child(Register* reg, RegisterEntry entry, uint32_t parent_flat_id) {
    entry.eid = (EntityID){ .id = reg->counter->next_id++, .kind = entry.tag };

    StringView name_key = sv_from_range(entry.decl_name_range);
    if (name_key.ptr && name_key.len > 0) {
        int absent;
        khint_t k = register_table_put(reg->table, name_key, &absent);
        if (absent > 0) kh_val(reg->table, k) = entry;
    }

    char* buf = malloc(32);
    snprintf(buf, 32, "%u", entry.eid.id);
    StringView id_key = { .ptr = buf, .len = strlen(buf) };
    int absent;
    khint_t k = register_table_put(reg->table, id_key, &absent);
    if (absent > 0) kh_val(reg->table, k) = entry;

    return true;
}

RegisterEntry* register_lookup(Register* reg, StringView key) {
    khint_t k = register_table_get(reg->table, key);
    if (k == kh_end(reg->table)) return NULL;
    return &kh_val(reg->table, k);
}

static SourceRange mangle_name(SourceRange class_name, SourceRange method_name) {
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

static SourceRange mangle_name_unique(Register* reg, SourceRange class_name, SourceRange method_name) {
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

static Register* make_child(Register* parent) {
    Register* child = malloc(sizeof(Register));
    *child = (Register){
        .table = register_table_init(),
        .parent = parent,
        .pending = pending_table_init(),
        .counter = parent->counter,
        .mono = NULL,
    };
    return child;
}

RegisterEntry* register_get(Register* reg, StringView name) {
    Register* cur = reg;
    while (cur) {
        khint_t k = register_table_get(cur->table, name);
        if (k != kh_end(cur->table)) return &kh_val(cur->table, k);
        cur = cur->parent;
    }
    return NULL;
}

uint32_t register_get_id(Register* reg, StringView name) {
    RegisterEntry* e = register_get(reg, name);
    return e ? e->eid.id : 0;
}

StringView register_get_name(Register* reg, uint32_t id) {
    RegisterEntry* e = register_get_by_id(reg, id);
    if (!e) return (StringView){0};
    return sv_from_range(e->decl_name_range);
}

FuncBodyList register_body(Stmts* body, size_t count, Register* reg, CheckerErrList* errors) {
    FuncBodyList fl = {0};
    for (size_t i = 0; i < count; i++) register_stmt(reg, &body[i], (SourceRange){0});
    return fl;
}

void register_free(Register* reg) {
    register_table_destroy(reg->table);
    pending_table_destroy(reg->pending);
}

Type type_from_range(SourceRange r) {
    if (!r.start || r.start == r.end) return (Type){ .tag = Type_Void };
    size_t len = r.end - r.start;
    if (len == 4 && memcmp(r.start, "bool", 4) == 0) return (Type){ .tag = Type_Bool };
    if (len == 3 && memcmp(r.start, "str", 3)  == 0) return (Type){ .tag = Type_Str };
    if (len == 4 && memcmp(r.start, "char", 4) == 0) return (Type){ .tag = Type_Char };
    if (len == 4 && memcmp(r.start, "void", 4) == 0) return (Type){ .tag = Type_Void };
    // signed ints
    if (len == 3 && memcmp(r.start, "int", 3)   == 0) return (Type){ .tag = Type_Int, .data.int_t = { .bits = 32, .is_unsigned = false } };
    if (len == 4 && memcmp(r.start, "int8", 4)  == 0) return (Type){ .tag = Type_Int, .data.int_t = { .bits = 8,  .is_unsigned = false } };
    if (len == 5 && memcmp(r.start, "int16", 5) == 0) return (Type){ .tag = Type_Int, .data.int_t = { .bits = 16, .is_unsigned = false } };
    if (len == 5 && memcmp(r.start, "int32", 5) == 0) return (Type){ .tag = Type_Int, .data.int_t = { .bits = 32, .is_unsigned = false } };
    if (len == 5 && memcmp(r.start, "int64", 5) == 0) return (Type){ .tag = Type_Int, .data.int_t = { .bits = 64, .is_unsigned = false } };
    // unsigned ints
    if (len == 4 && memcmp(r.start, "uint", 4)   == 0) return (Type){ .tag = Type_Int, .data.int_t = { .bits = 32, .is_unsigned = true } };
    if (len == 5 && memcmp(r.start, "uint8", 5)  == 0) return (Type){ .tag = Type_Int, .data.int_t = { .bits = 8,  .is_unsigned = true } };
    if (len == 6 && memcmp(r.start, "uint16", 6) == 0) return (Type){ .tag = Type_Int, .data.int_t = { .bits = 16, .is_unsigned = true } };
    if (len == 6 && memcmp(r.start, "uint32", 6) == 0) return (Type){ .tag = Type_Int, .data.int_t = { .bits = 32, .is_unsigned = true } };
    if (len == 6 && memcmp(r.start, "uint64", 6) == 0) return (Type){ .tag = Type_Int, .data.int_t = { .bits = 64, .is_unsigned = true } };
    // floats
    if (len == 5 && memcmp(r.start, "float", 5)   == 0) return (Type){ .tag = Type_Float, .data.float_t = { .bits = 32 } };
    if (len == 7 && memcmp(r.start, "float32", 7) == 0) return (Type){ .tag = Type_Float, .data.float_t = { .bits = 32 } };
    if (len == 7 && memcmp(r.start, "float64", 7) == 0) return (Type){ .tag = Type_Float, .data.float_t = { .bits = 64 } };
    // unknown - custom type
    return (Type){ .tag = Type_Custom, .data.custom.name = r };
}

RegisterEntry* register_expr(Register* reg, Exprs* expr, SourceRange class_name) {
    fprintf(stderr, "[register_expr] tag=%d\n", expr->tag);
    switch (expr->tag) {
        case Expr_Function: {
            Register* child = make_child(reg);
            size_t argc = expr->data.function_call.param_count;
            uint32_t* arg_ids = argc ? malloc(sizeof(uint32_t) * argc) : NULL;
            for (size_t i = 0; i < argc; i++) {
                RegisterEntry* arg_entry = register_expr(child, &expr->data.function_call.param[i].value, (SourceRange){0});
                arg_ids[i] = arg_entry ? arg_entry->eid.id : 0;
            }
            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprFunctionCall,
                .decl_range = expr->data.function_call.range,
                .data.expr_function_call = {
                    .name = expr->data.function_call.name,
                    .params = expr->data.function_call.param,
                    .params_count = expr->data.function_call.param_count,
                    .generic_args = expr->data.function_call.generic_params,
                    .generic_args_count = expr->data.function_call.generic_params_count,
                    .child_reg = child,
                    .arg_ids = arg_ids,
                    .arg_ids_count = argc,
                }
            };
            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }

        case Expr_Literals: {
            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprLiteral,
                .decl_range = expr->data.literals.range,
                .data.expr_literal = { .resolved_type = (Type){0} },
            };
            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }

        case Expr_Identifiers: {
            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprIdentifier,
                .decl_range = expr->data.identifiers.range,
                .data.expr_identifier = {
                    .name = expr->data.identifiers.name,
                    .resolved_type = (Type){0},
                },
            };
            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }

        case Expr_Vars: {
            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprVar,
                .decl_range = expr->data.vars.range,
                .data.expr_var = {
                    .name = expr->data.vars.name,
                    .resolved_type = (Type){0},
                },
            };
            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }


        case Expr_BinaryOps: {
            RegisterEntry* left  = register_expr(reg, expr->data.binary_ops.left, (SourceRange){0});
            RegisterEntry* right = register_expr(reg, expr->data.binary_ops.right, (SourceRange){0});
            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprBinaryOp,
                .decl_range = expr->data.binary_ops.range,
                .data.expr_binary_op = {
                    .op = expr->data.binary_ops.op,
                    .left_id = left ? left->eid.id : 0,
                    .right_id = right ? right->eid.id : 0,
                    .left_type = (Type){0},
                    .right_type = (Type){0},
                    .resolved_type = (Type){0},
                },
            };
            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }

        case Expr_Unary: {
            RegisterEntry* operand = register_expr(reg, expr->data.unary.operand, (SourceRange){0});
            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprUnary,
                .decl_range = expr->data.unary.range,
                .data.expr_unary = {
                    .op = expr->data.unary.op,
                    .operand = operand,
                    .resolved_type = (Type){0},
                },
            };
            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }

        case Expr_Cast: {
            RegisterEntry* inner = register_expr(reg, expr->data.cast.expr, (SourceRange){0});
            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprCast,
                .decl_range = expr->data.cast.range,
                .data.expr_cast = {
                    .expr_id = inner ? inner->eid.id : 0,
                    .ty = expr->data.cast.ty,
                },
            };

            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }


        case Expr_Idx: {
            RegisterEntry* base  = register_expr(reg, expr->data.idx.base,  class_name);
            RegisterEntry* index = register_expr(reg, expr->data.idx.index, class_name);

            Type base_type  = (Type){0};
            Type index_type = (Type){0};

            if (base && base->tag == Reg_ExprIdentifier) { RegisterEntry* base_decl = register_get(reg, sv_from_range(base->data.expr_identifier.name)); if (base_decl) base_type = base_decl->data.var.type; }

            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprIdx,
                .decl_range = expr->data.idx.range,
                .data.idx = {
                    .base_id = base ? base->eid.id : 0,
                    .index_id = index ? index->eid.id : 0,
                    .range = expr->data.idx.range,
                    .elem_ty = (base_type.tag == Type_Ptr && base_type.data.ptr.inner) ? *base_type.data.ptr.inner : (base_type.tag == Type_RawPtr && base_type.data.raw_ptr.inner) ? *base_type.data.raw_ptr.inner : base_type,
                },
            };
            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }


        case Expr_Field: {
            StringView obj_sv = sv_from_range(expr->data.field_access.object);
            RegisterEntry* object = register_lookup(reg, obj_sv);
            FieldOwnerKind kind = FieldOwner_Unknown;
            RegisterEntry* type_entry = NULL;

            if (object) {
                switch (object->tag) {
                    case Reg_Struct: type_entry = object; break;
                    case Reg_Class:  type_entry = object; break;
                    case Reg_Enum:   type_entry = object; break;

                    case Reg_Var: {
                        RegisterEntry* init = object->data.var.init;
                        if (init && init->tag == Reg_ExprFunctionCall) {
                            StringView callee_sv = sv_from_range(init->data.expr_function_call.name);
                            type_entry = register_get(reg, callee_sv);
                        }
                        break;
                    }

                    default: break;
                }

                if (type_entry) {
                    switch (type_entry->tag) {
                        case Reg_Struct: kind = FieldOwner_Struct; break;
                        case Reg_Class:  kind = FieldOwner_Class;  break;
                        case Reg_Enum:   kind = FieldOwner_Enum;   break;
                        default: break;
                    }
                }
            }

            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprField,
                .decl_range = expr->data.field_access.range,
                .data.expr_field = {
                    .object = object,
                    .field = expr->data.field_access.field,
                    .range = expr->data.field_access.range,
                    .kind = kind,
                    .type_eid = type_entry ? type_entry->eid : (EntityID){0},
                },
            };
            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }

        case Expr_Array: {
            size_t n = expr->data.array.elems_count;
            RegisterEntry** elems = n ? malloc(sizeof(RegisterEntry*) * n) : NULL;
            for (size_t i = 0; i < n; i++) elems[i] = register_expr(reg, &expr->data.array.elems[i], (SourceRange){0});

            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprArray,
                .data.array = { .elems = elems, .elems_count = n },
            };
            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }

        case Expr_AddrOf: {
            RegisterEntry* operand = register_expr(reg, expr->data.unary.operand, class_name);
            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprAddrOf,
                .decl_range = expr->data.unary.range,
                .data.expr_unary = {
                    .op = Ampersands,
                    .operand = operand,
                    .resolved_type = (Type){0},
                },
            };
            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }

        case Expr_MethodCalls: {
            RegisterEntry* object = register_expr(reg, expr->data.method_calls.object, (SourceRange){0});
            size_t n = expr->data.method_calls.args_count;
            RegisterEntry** args = n ? malloc(sizeof(RegisterEntry*) * n) : NULL;
            for (size_t i = 0; i < n; i++) args[i] = register_expr(reg, &expr->data.method_calls.args[i], (SourceRange){0});
            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprMethodCall,
                .decl_range = expr->data.method_calls.range,
                .data.expr_method_call = {
                    .object = object,
                    .method = expr->data.method_calls.method,
                    .args = args,
                    .args_count = n,
                    .range = expr->data.method_calls.range,
                },
            };
            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }

        case Expr_Class_Calls: {
            size_t argc = expr->data.class_calls.param_count;
            Register* child = make_child(reg);
            uint32_t* arg_ids = argc ? malloc(sizeof(uint32_t) * argc) : NULL;
            for (size_t i = 0; i < argc; i++) {
                RegisterEntry* arg_entry = register_expr(child, &expr->data.class_calls.param[i].value, (SourceRange){0});
                arg_ids[i] = arg_entry ? arg_entry->eid.id : 0;
            }

            SourceRange mangled = mangle_name(expr->data.class_calls.name, expr->data.class_calls.function);
            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprFunctionCall,
                .decl_range = expr->data.class_calls.range,
                .data.expr_function_call = {
                    .name = mangled,
                    .params = expr->data.class_calls.param,
                    .params_count = argc,
                    .generic_args = expr->data.class_calls.generic_params,
                    .generic_args_count = expr->data.class_calls.generic_params_count,
                    .child_reg = child,
                    .arg_ids = arg_ids,
                    .arg_ids_count = argc,
                }
            };
            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }

        case Expr_Self: {
            size_t argc = expr->data.self_access.args_count;
            uint32_t* arg_ids = argc ? malloc(sizeof(uint32_t) * argc) : NULL;

            for (size_t i = 0; i < argc; i++) {
                RegisterEntry* arg_entry = register_expr(reg, &expr->data.self_access.args[i].value, class_name);
                arg_ids[i] = arg_entry ? arg_entry->eid.id : 0;
            }

            if (expr->data.self_access.is_call) {
                SourceRange mangled = class_name.start ? mangle_name(class_name, expr->data.self_access.target) : expr->data.self_access.target;
                Register* child = make_child(reg);

                RegisterEntry entry = (RegisterEntry){
                    .tag = Reg_ExprFunctionCall,
                    .decl_range = expr->data.self_access.range,
                    .data.expr_function_call = {
                        .name = mangled,
                        .params = expr->data.self_access.args,
                        .params_count = argc,
                        .generic_args = NULL,
                        .generic_args_count = 0,
                        .child_reg = child,
                        .arg_ids = arg_ids,
                        .arg_ids_count = argc,
                    }
                };
                EntityID eid = register_insert(reg, entry);
                return register_get_by_id(reg, eid.id);
            }

            free(arg_ids);
            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprIdentifier,
                .decl_range = expr->data.self_access.range,
                .data.expr_identifier = {
                    .name = expr->data.self_access.target,
                    .resolved_type = (Type){0},
                },
            };
            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }


        case Expr_Struct_Calls: {
            size_t argc = expr->data.struct_calls.param_count;
            for (size_t i = 0; i < argc; i++) register_expr(reg, &expr->data.struct_calls.param[i].value, (SourceRange){0});
            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprStructCall,
                .decl_range = expr->data.struct_calls.range,
                .data.expr_struct_call = {
                    .name = expr->data.struct_calls.name,
                    .function = expr->data.struct_calls.function,
                    .params = expr->data.struct_calls.param,
                    .params_count = argc,
                    .generic_args = expr->data.struct_calls.generic_params,
                    .generic_args_count = expr->data.struct_calls.generic_params_count,
                },
            };
            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }

        case Expr_Enum_Calls: {
            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprEnumCall,
                .decl_range = expr->data.enum_calls.range,
                .data.expr_enum_call = {
                    .name = expr->data.enum_calls.name,
                    .field = expr->data.enum_calls.field,
                    .resolved_type = (Type){0},
                    .generic_args = expr->data.enum_calls.generic_params,
                    .generic_args_count = expr->data.enum_calls.generic_params_count,
                },
            };
            EntityID eid = register_insert(reg, entry);
            return register_get_by_id(reg, eid.id);
        }

        case Expr_Null:
        default:
            return NULL;
    }
}

void register_function(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_Functions);
    FunctionData fn = stmt->data.functions;
    Register* child = make_child(reg);
    uint32_t fn_id = reg->counter->next_id;

    for (size_t i = 0; i < fn.params_count; i++) {
        register_insert_child(child, (RegisterEntry){
            .tag = Reg_Param,
            .decl_name_range = fn.params[i].name,
            .data.var = {
                .type = DEREF_TYPE(fn.params[i].type_tree, fn.params[i].c_type),
                .mode = fn.params[i].mode,
                .is_mut = fn.params[i].mode.mutability == Mutability_Mutable,
            }
        }, fn_id);
    }

    REG_PARAM_EXPRS(child, fn.params, fn.params_count, (SourceRange){0});
    REG_STMTS(child, fn.body, fn.body_count, (SourceRange){0});

    register_insert(reg, (RegisterEntry){
        .tag = Reg_Function,
        .decl_range = fn.range,
        .decl_name_range = fn.name,
        .data.function   = {
            .params = fn.params,
            .params_count = fn.params_count,
            .return_type = DEREF_TYPE(fn.return_type_tree, fn.range),
            .is_pub = fn.is_pub,
            .is_unsafe = fn.is_unsafe,
            .generic_params = fn.generic_params,
            .generic_params_count = fn.generic_params_count,
            .generic_param_nodes  = fn.generic_param_nodes,
            .body = fn.body,
            .body_count = fn.body_count,
            .child_reg = child,
        }
    });
}

void register_class(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_Classes);
    ClassData c = stmt->data.classes;

    for (size_t i = 0; i < c.fields_count; i++)
        register_insert(reg, (RegisterEntry){
            .tag = Reg_Var,
            .decl_name_range = c.fields[i].name,
            .data.var = {
                .type = DEREF_TYPE(c.fields[i].type_tree, c.fields[i].c_type),
                .mode = c.fields[i].mode,
                .is_mut = c.fields[i].mode.mutability == Mutability_Mutable,
            }
        });

    for (size_t i = 0; i < c.methods_count; i++) {
        SourceRange mangled = mangle_name_unique(reg, c.name, c.methods[i].name);

        Register* method_child = make_child(reg);

        for (size_t j = 0; j < c.methods[i].params_count; j++) {
            register_insert(method_child, (RegisterEntry){
                .tag = Reg_Param,
                .decl_name_range = c.methods[i].params[j].name,
                .data.var = {
                    .type = DEREF_TYPE(c.methods[i].params[j].type_tree, c.methods[i].params[j].c_type),
                    .mode = c.methods[i].params[j].mode,
                    .is_mut = c.methods[i].params[j].mode.mutability == Mutability_Mutable,
                }
            });
            REG_PARAM_EXPRS(method_child, c.methods[i].params, c.methods[i].params_count, c.name);
        }

        REG_STMTS(method_child, c.methods[i].body, c.methods[i].body_count, c.name);

        register_insert(reg, (RegisterEntry){
            .tag = Reg_Function,
            .decl_range = c.methods[i].range,
            .decl_name_range = mangled,
            .data.function = {
                .params = c.methods[i].params,
                .params_count = c.methods[i].params_count,
                .return_type = DEREF_TYPE(c.methods[i].return_type_tree, c.methods[i].return_type),
                .is_pub = c.methods[i].is_pub,
                .child_reg = method_child,
            }
        });
    }
}

void register_struct(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_Structs);
    StructData s = stmt->data.structs;
    register_insert(reg, (RegisterEntry){
        .tag = Reg_Struct,
        .decl_range = s.range,
        .decl_name_range = s.name,
        .data.strct = {
            .fields = s.fields,
            .fields_count = s.fields_count,
            .is_pub = s.is_pub,
            .generic_params = s.generic_params,
            .generic_params_count = s.generic_params_count,
            .generic_param_nodes = s.generic_param_nodes,
        }
    });
}

void register_enum(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_Enums);
    EnumData e = stmt->data.enums;
    register_insert(reg, (RegisterEntry){
        .tag = Reg_Enum,
        .decl_range = e.range,
        .decl_name_range = e.name,
        .data.enm = {
            .variants = e.variants,
            .variants_count = e.variants_count,
            .is_pub = e.is_pub,
            .generic_params = e.generic_params,
            .generic_params_count = e.generic_params_count,
            .generic_param_nodes  = e.generic_param_nodes,
        }
    });
}

void register_trait(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_Traits);
    TraitData t = stmt->data.traits;

    for (size_t i = 0; i < t.methods_count; i++) {
        Register* method_child = make_child(reg);
        for (size_t j = 0; j < t.methods[i].params_count; j++) {
            register_insert(method_child, (RegisterEntry){
                .tag = Reg_Param,
                .decl_name_range = t.methods[i].params[j].name,
                .data.var = {
                    .type = DEREF_TYPE(t.methods[i].params[j].type_tree, t.methods[i].params[j].c_type),
                    .mode = t.methods[i].params[j].mode,
                }
            });

            REG_PARAM_EXPRS(method_child, t.methods[i].params, t.methods[i].params_count, (SourceRange){0});
        }

        REG_STMTS(method_child, t.methods[i].body, t.methods[i].body_count, (SourceRange){0});
    }

    register_insert(reg, (RegisterEntry){
        .tag = Reg_Trait,
        .decl_range = t.range,
        .decl_name_range = t.name,
        .data.trait = {
            .methods = t.methods,
            .methods_count = t.methods_count,
            .is_pub = t.is_pub,
        }
    });
}

void register_extern(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_Externs);
    SourceRange abi = stmt->data.extern_.abi;
    ExternFunction* funcs = stmt->data.extern_.funcs;
    size_t funcs_count = stmt->data.extern_.funcs_count;

    Register* child = make_child(reg);

    for (size_t i = 0; i < funcs_count; i++) {
        ExternFunction *f = &funcs[i];

        for (size_t j = 0; j < f->params_count; j++) {
            register_insert(child, (RegisterEntry){
                .tag = Reg_Param,
                .decl_name_range = f->params[j].name,
                .data.var = {
                    .type = DEREF_TYPE(f->params[j].type_tree, f->params[j].c_type),
                    .mode = f->params[j].mode,
                    .is_mut = f->params[j].mode.mutability == Mutability_Mutable,
                }
            });
        }

        register_insert(child, (RegisterEntry){
            .tag = Reg_ExternFunc,
            .decl_range = f->name,
            .decl_name_range = f->name,
            .data.extern_func = {
                .name = f->name,
                .return_type = DEREF_TYPE(f->return_type_tree, f->return_type),
                .params = f->params,
                .params_count = f->params_count,
            }
        });
    }

    register_insert(reg, (RegisterEntry){
        .tag = Reg_Extern,
        .decl_range = funcs_count > 0 ? funcs[0].name : (SourceRange){0},
        .decl_name_range = abi,
        .data.extern_ = {
            .abi = abi,
            .funcs = funcs,
            .funcs_count = funcs_count,
            .child_reg   = child,
        }
    });
}

static bool expr_is_empty(Exprs* expr) {
    if (expr == NULL) return true;
    if (expr->tag == Expr_Null) return true;
    if (expr->tag == Expr_Literals && expr->data.literals.range.start == expr->data.literals.range.end) return true;
    if (expr->tag == Expr_Function && expr->data.function_call.name.start == NULL) return true;

    return false;
}

void register_var(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Vars);
    VarData v = stmt->data.vars;

    bool has_init = v.value.tag != 0 || v.value.data.function_call.name.start != NULL;
    RegisterEntry* init_entry = NULL;

    if (has_init) {
        Register* child = make_child(reg);
        init_entry = register_expr(child, &stmt->data.vars.value, class_name);
    }

    register_insert(reg, (RegisterEntry){
        .tag = Reg_Var,
        .decl_range = v.range,
        .decl_name_range = v.name,
        .data.var = {
            .type   = DEREF_TYPE(v.type_tree, v.name),
            .mode   = v.mode,
            .is_mut = v.mode.mutability == Mutability_Mutable,
            .init   = init_entry,
        }
    });
}

void register_let(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Lets);
    LetData l = stmt->data.lets;
    RegisterEntry* init_entry = l.value.tag ? register_expr(reg, &l.value, class_name) : NULL;
    register_insert(reg, (RegisterEntry){
        .tag             = Reg_Let,
        .decl_range      = l.range,
        .decl_name_range = l.name,
        .data.let = {
            .type = DEREF_TYPE(l.type_tree, l.name),
            .mode = l.mode,
            .init = init_entry,
        }
    });
}

void register_const(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Consts);
    ConstData c = stmt->data.consts;
    RegisterEntry* init_entry = c.value.tag ? register_expr(reg, &c.value, class_name) : NULL;
    register_insert(reg, (RegisterEntry){
        .tag = Reg_Const,
        .decl_range = c.range,
        .decl_name_range = c.name,
        .data.const_ = {
            .type   = DEREF_TYPE(c.type_tree, c.name),
            .is_pub = c.is_pub,
            .init   = init_entry,
        }
    });
}


void register_if(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Ifs);
    IfData s = stmt->data.ifs;
    Register* cond_child = make_child(reg);
    Register* then_child = make_child(reg);
    Register* else_child = make_child(reg);
    RegisterEntry* cond_entry = register_expr(cond_child, &s.cond, class_name);
    uint32_t cond_id = cond_entry ? cond_entry->eid.id : 0;
    REG_STMTS(then_child, s.body, s.body_count, class_name);
    REG_STMTS(else_child, s.else_body, s.else_body_count, class_name);

    register_insert(reg, (RegisterEntry){
        .tag = Reg_If,
        .decl_range = s.range,
        .data.if_ = {
            .cond_id = cond_id, .cond_child = cond_child, .then_child = then_child, .else_child = else_child,
            .body = s.body, .body_count = s.body_count, .else_body = s.else_body, .else_body_count = s.else_body_count,
        }
    });
}

void register_elif(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Elifs);
    IfData s = stmt->data.ifs;
    Register* cond_child = make_child(reg);
    Register* then_child = make_child(reg);
    Register* else_child = make_child(reg);
    RegisterEntry* cond_entry = register_expr(cond_child, &s.cond, class_name);
    uint32_t cond_id = cond_entry ? cond_entry->eid.id : 0;
    REG_STMTS(then_child, s.body, s.body_count, class_name);
    REG_STMTS(else_child, s.else_body, s.else_body_count, class_name);
    register_insert(reg, (RegisterEntry){
        .tag = Reg_Elif,
        .decl_range = s.range,
        .data.if_ = {
            .cond_id = cond_id, .cond_child = cond_child, .then_child = then_child, .else_child = else_child,
            .body = s.body, .body_count = s.body_count, .else_body = s.else_body, .else_body_count = s.else_body_count,
        }
    });
}


void register_while(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Whiles);
    WhileData s = stmt->data.whiles;
    Register* cond_child = make_child(reg);
    Register* body_child = make_child(reg);
    RegisterEntry* cond_entry = register_expr(cond_child, &s.cond, class_name);
    uint32_t cond_id = cond_entry ? cond_entry->eid.id : 0;

    REG_STMTS(body_child, s.body, s.body_count, class_name);
    register_insert(reg, (RegisterEntry){
        .tag = Reg_While,
        .decl_range = s.range,
        .data.while_ = {
            .cond_id = cond_id, 
            .cond_child = cond_child, 
            .body_child = body_child,
            .body = s.body, 
            .body_count = s.body_count,
        }
    });
}


void register_for(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Fors);
    ForData s = stmt->data.fors;
    Register* iter_child = make_child(reg);
    Register* body_child = make_child(reg);
    RegisterEntry* iter_entry = register_expr(iter_child, &s.iter, class_name);
    uint32_t iter_id = iter_entry ? iter_entry->eid.id : 0;
    REG_STMTS(body_child, s.body, s.body_count, class_name);
    register_insert(reg, (RegisterEntry){
        .tag = Reg_For,
        .decl_range = s.range,
        .data.for_ = {
            .var = s._var, .iter_id = iter_id, .iter_child = iter_child, .body_child = body_child,
            .body = s.body, .body_count = s.body_count,
        }
    });
}

void register_match(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Matchs);
    MatchData s = stmt->data.matchs;
    Register* expr_child    = make_child(reg);
    Register* default_child = make_child(reg);
    Register** arm_children = s.cases_count ? malloc(sizeof(Register*) * s.cases_count) : NULL;
    RegisterEntry* expr_entry = register_expr(expr_child, &s.expr, class_name);
    uint32_t expr_id = expr_entry ? expr_entry->eid.id : 0;
    for (size_t i = 0; i < s.cases_count; i++) {
        arm_children[i] = make_child(reg);
        if (s.cases[i].pattern.tag == Pattern_Guard) register_expr(arm_children[i], s.cases[i].pattern.data.guard.expr, class_name);
        REG_STMTS(arm_children[i], s.cases[i].body, s.cases[i].body_count, class_name);
    }
    if (s.default_body) REG_STMTS(default_child, s.default_body, s.default_body_count, class_name);
    register_insert(reg, (RegisterEntry){
        .tag = Reg_Match,
        .decl_range = s.range,
        .data.match_ = {
            .expr_id = expr_id, .expr_child = expr_child, .default_child = default_child, .arm_children = arm_children,
            .cases = s.cases, .cases_count = s.cases_count, .default_body = s.default_body, .default_body_count = s.default_body_count,
        }
    });
}


void register_return(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Returns);
    RegisterEntry* expr_entry = !expr_is_empty(&stmt->data.returns.expr)
        ? register_expr(reg, &stmt->data.returns.expr, class_name)
        : NULL;

    register_insert(reg, (RegisterEntry){
        .tag = Reg_Return,
        .decl_range = stmt->data.returns.range,
        .data.return_ = {
            .expr     = expr_entry ? expr_entry->eid : (EntityID){0},
            .has_expr = expr_entry != NULL,
        }
    });
}

void register_expr_stmt(Register* reg, Stmts* stmt, SourceRange class_name) {
    RegisterEntry* expr_entry = register_expr(reg, &stmt->data.expr_stmt.expr, class_name);

    register_insert(reg, (RegisterEntry){
        .tag = Reg_ExprStmt,
        .data.expr_stmt_ = { .expr_id = expr_entry ? expr_entry->eid.id : 0 }
    });
}

void register_assign(Register* reg, Stmts* stmt, SourceRange class_name) {
    assert(stmt->tag == Stmt_Assigns);
    AssignData a = stmt->data.assigns;

    RegisterEntry* target_entry = register_expr(reg, &a.target, class_name);
    RegisterEntry* value_entry  = register_expr(reg, &a.value, class_name);

    register_insert(reg, (RegisterEntry){
        .tag = Reg_Assign,
        .decl_range = a.range,
        .data.assign = {
            .op        = a.op,
            .target_id = target_entry ? target_entry->eid.id : 0,
            .value_id  = value_entry  ? value_entry->eid.id  : 0,
        }
    });
}

void register_module(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_Modules);
    ModuleData m = stmt->data.modules;
    Register* child = make_child(reg);
    REG_STMTS(child, m.body, m.body_count, (SourceRange){0});
    register_insert(reg, (RegisterEntry){
        .tag = Reg_Module,
        .decl_range = m.range,
        .decl_name_range = m.name,
        .data.module_ = {
            .name       = m.name,
            .body       = m.body,
            .body_count = m.body_count,
            .is_pub     = m.is_pub,
        }
    });
}

void register_atomic(Register* reg, Stmts* stmt) {
    assert(stmt->tag == Stmt_AtomicOp);
    AtomicData a = stmt->data.atomic_op;
    REG_EXPRS(reg, a.args, a.args_count, (SourceRange){0});
    register_insert(reg, (RegisterEntry){
        .tag = Reg_Atomic,
        .decl_range = a.range,
        .data.atomic_ = {
            .target     = a.target,
            .op         = a.op,
            .args_count = a.args_count,
            .ordering   = a.ordering,
            .ordering2  = a.ordering2,
        }
    });
}


void register_stmt(Register* reg, Stmts* stmt, SourceRange class_name) {
    switch (stmt->tag) {
        case Stmt_Functions: register_function(reg, stmt); break;
        case Stmt_Structs:   register_struct(reg, stmt);   break;
        case Stmt_Enums:     register_enum(reg, stmt);     break;
        case Stmt_Classes:   register_class(reg, stmt);    break;
        case Stmt_Traits:    register_trait(reg, stmt);    break;
        case Stmt_Externs:   register_extern(reg, stmt);   break;
        case Stmt_Vars:      register_var(reg, stmt, class_name);      break;
        case Stmt_Lets:      register_let(reg, stmt, class_name);      break;
        case Stmt_Consts:    register_const(reg, stmt, class_name);    break;
        case Stmt_Ifs:       register_if(reg, stmt, class_name);       break;
        case Stmt_Whiles:    register_while(reg, stmt, class_name);    break;
        case Stmt_Fors:      register_for(reg, stmt, class_name);      break;
        case Stmt_Matchs:    register_match(reg, stmt, class_name);    break;
        case Stmt_Returns:   register_return(reg, stmt, class_name);   break;
        case Stmt_ExprStmt:  register_expr_stmt(reg, stmt, class_name); break;
        case Stmt_Assigns:   register_assign(reg, stmt, class_name);   break;
        case Stmt_Elifs:     register_elif(reg, stmt, class_name);    break;
        case Stmt_Modules:   register_module(reg, stmt);  break;
        case Stmt_AtomicOp:  register_atomic(reg, stmt);  break;

        default: break;
    }
}
