#include "import.h"
#include "register.h"
#include "ir.h"
#include "import.h"
#include "third-party/khashl.h"
#include "helper.h"
#include "generic.h"
#include "register_tables.h"


Type generic_return(GenericSeenTable* g, Type return_type, Type* generic, size_t generic_count, Type* fn_generic);

UInt32Arr collect_generic_template_ids(Register* reg) {
    UInt32Arr template_ids = {0};

    for (uint32_t i = 0; i < reg->counter->next_id; i++) {
        RegisterEntry *entry = register_from_scope(reg, i);
        if (!entry) continue;

        if (entry->tag == Reg_Function && entry->data.function.generic_params_count > 0) {
            ARR_PUSH(template_ids, i);
        }
    }

    return template_ids;
}

Type set_type_name(GenericSeenTable* global_table, Type t, TypeArr specialized_args, size_t generic_count) {
    SourceRange base_name = t.data.custom.name;
    SourceRange mangled_name = set_name(global_table, base_name, specialized_args.data, specialized_args.len);
    Type resolved_type = t;

    resolved_type.data.custom.name = mangled_name;

    return resolved_type;
}

Type resolve_type(GenericSeenTable* g, Type t, Type* generic, size_t generic_count, Type* fn_generic) {
    if (t.tag == Type_Custom && t.data.custom.generic_args_count == 0) {
        size_t idx = match_generic(t.data.custom.name, generic, generic_count);
        if (idx != (size_t)-1) { 
            return fn_generic[idx];
        }
        return t;
    }

    if (t.tag == Type_Custom && t.data.custom.generic_args_count > 0) {
        size_t args_count = t.data.custom.generic_args_count;
        Type* args = set_generic_type(t.data.custom.generic_args, args_count);

        TypeArr specialized_args = {0};

        for (size_t i = 0; i < args_count; i++) {
            Type resolved_sub = resolve_type(g, args[i], generic, generic_count, fn_generic);
            ARR_PUSH(specialized_args, resolved_sub);
        }

        Type resolved_type = set_type_name(g, t, specialized_args, generic_count);
        SourceRange final_name = resolved_type.data.custom.name;

        if (!register_by_name(sv_from_range(final_name))) {
            RegisterEntry* original_type = register_by_name(sv_from_range(t.data.custom.name));
            
            if (original_type && original_type->tag == Reg_Enum) {
                Type* enum_generic = set_generic_type(original_type->data.enm.generic_params, original_type->data.enm.generic_params_count);
                size_t enum_count = original_type->data.enm.generic_params_count;
                size_t variant_count = original_type->data.enm.variants_count;

                EnumVariant* variant = malloc(sizeof(EnumVariant) * variant_count);
                memcpy(variant, original_type->data.enm.variants, sizeof(EnumVariant) * variant_count);

                variant = generic_variant(g, variant, variant_count, enum_generic, enum_count, specialized_args.data);

                Register* child = make_child(global_reg_ptr);
                EntityID new_enum_eid = register_insert(global_reg_ptr, (RegisterEntry){
                    .tag = Reg_Enum,
                    .decl_name_range = final_name,
                    .data.enm = {
                        .variants = variant,
                        .variants_count = variant_count,
                        .is_pub = original_type->data.enm.is_pub,
                        .child_reg = child,
                    }
                });
                child->owner_id = new_enum_eid.id;

                for (size_t v = 0; v < variant_count; v++) {
                    register_insert_child(child, (RegisterEntry){
                        .tag = Reg_EnumVariant,
                        .data.enum_variant = {
                            .enum_name = final_name,
                            .variant_name = variant[v].name,
                            .tag_index = (uint32_t)v,
                            .fields = variant[v].fields,
                            .fields_count = variant[v].fields_count,
                        }
                    }, new_enum_eid.id);
                }

                generic_index(g, sv_from_range(final_name), register_from_global(new_enum_eid.id));
            } 
            else if (original_type && original_type->tag == Reg_Struct) {
                Type* struct_generic = set_generic_type(original_type->data.strct.generic_params, original_type->data.strct.generic_params_count);
                size_t struct_count = original_type->data.strct.generic_params_count;
                size_t field_count = original_type->data.strct.fields_count;

                StructParam* fields = malloc(sizeof(StructParam) * field_count);
                memcpy(fields, original_type->data.strct.fields, sizeof(StructParam) * field_count);

                fields = generic_field(g, fields, field_count, struct_generic, struct_count, specialized_args.data);

                EntityID new_struct_eid = register_insert(global_reg_ptr, (RegisterEntry){
                    .tag = Reg_Struct,
                    .decl_name_range = final_name,
                    .data.strct = {
                        .fields = fields,
                        .fields_count = field_count,
                        .is_pub = original_type->data.strct.is_pub,
                    }
                });
                generic_index(g, sv_from_range(final_name), register_from_global(new_struct_eid.id));
            }
        }

        resolved_type.data.custom.generic_args = NULL;
        resolved_type.data.custom.generic_args_count = 0;

        return resolved_type;
    }

    return t;
}


GenericTables generic_new(void);

GenericTables generic_new(void) {
    return (GenericTables){
        .param_table  = generic_count_table_init(),
        .return_table = generic_return_table_init(),
        .call_table   = generic_call_table_init(),
        .args_table   = generic_args_table_init(),
        .global_table = generic_seen_table_init(),  
    };
}



void generic_free(GenericTables* g) {
    generic_count_table_destroy(g->param_table);
    generic_return_table_destroy(g->return_table);
    generic_call_table_destroy(g->call_table);
    generic_args_table_destroy(g->args_table);
    generic_seen_table_destroy(g->global_table);
    *g = (GenericTables){0};
}


void check_generic(Register* reg, GenericTables* g) {
    for (uint32_t i = 0; i < reg->counter->next_id; i++) {
        RegisterEntry *entry = register_from_scope(reg, i);
        if (!entry) continue;

        switch (entry->tag) {
            case Reg_Function: check_function(i, g); break;
            case Reg_Enum:     check_enum(i, g); break;
            case Reg_Struct:   check_struct(i, g); break;
            case Reg_Const:    check_var(entry, g, false, true); break;
        }
    }
}

void check_enumcall(RegisterEntry* entry, GenericTables* g) {
    Type* generic = set_generic_type(entry->data.expr_enum_call.generic_args, entry->data.expr_enum_call.generic_args_count);
    size_t generic_count = entry->data.expr_enum_call.generic_args_count;
    SourceRange name = entry->data.expr_enum_call.name;
    RegisterEntry* enums = register_by_name(sv_from_range(name));

    if (!enums || enums->tag != Reg_Enum) return;

    Type* enum_generic = set_generic_type(enums->data.enm.generic_params, enums->data.enm.generic_params_count);
    size_t enum_count = enums->data.enm.generic_params_count;
    SourceRange final_name = set_name(g->global_table, name, generic, generic_count);

    if (!register_by_name(sv_from_range(final_name))) {
        size_t variant_count = enums->data.enm.variants_count;
        EnumVariant* variant = malloc(sizeof(EnumVariant) * variant_count);
        memcpy(variant, enums->data.enm.variants, sizeof(EnumVariant) * variant_count);

        variant = generic_variant(g->param_table, variant, variant_count, enum_generic, enum_count, generic);

        Register* child = make_child(global_reg_ptr);

        EntityID new_enum_eid = register_insert(global_reg_ptr, (RegisterEntry){
            .tag = Reg_Enum,
            .decl_name_range = final_name,
            .data.enm = {
                .variants = variant,
                .variants_count = variant_count,
                .generic_params = NULL,
                .generic_params_count = 0,
                .generic_param_nodes = 0,
                .is_pub = enums->data.enm.is_pub,
                .child_reg = child,
            }
        });

        child->owner_id = new_enum_eid.id;

        for (size_t i = 0; i < variant_count; i++) {
            register_insert_child(child, (RegisterEntry){
                .tag = Reg_EnumVariant,
                .data.enum_variant = {
                    .enum_name = final_name,
                    .variant_name = variant[i].name,
                    .tag_index = (uint32_t)i,
                    .fields = variant[i].fields,
                    .fields_count = variant[i].fields_count,
                }
            }, new_enum_eid.id);
        }

        generic_index(g->global_table, sv_from_range(final_name), register_from_global(new_enum_eid.id));
    }

    entry->data.expr_enum_call.name = final_name;
    entry->data.expr_enum_call.generic_args = NULL;
    entry->data.expr_enum_call.generic_args_count = 0;
}

void check_structcall(RegisterEntry* entry, GenericTables* g) {
    Type* generic = set_generic_type(entry->data.expr_struct_call.generic_args, entry->data.expr_struct_call.generic_args_count);
    size_t generic_count = entry->data.expr_struct_call.generic_args_count;
    SourceRange name = entry->data.expr_struct_call.name;

    RegisterEntry* structs = register_by_name(sv_from_range(name));
    if (!structs || structs->tag != Reg_Struct) return;

    Type* struct_generic = set_generic_type(structs->data.strct.generic_params, structs->data.strct.generic_params_count);
    size_t struct_count = structs->data.strct.generic_params_count;

    SourceRange final_name = set_name(g->global_table, name, generic, generic_count);

    if (!register_by_name(sv_from_range(final_name))) {
        size_t field_count = structs->data.strct.fields_count;
        StructParam* fields = malloc(sizeof(StructParam) * field_count);
        memcpy(fields, structs->data.strct.fields, sizeof(StructParam) * field_count);

        fields = generic_field(g->global_table, fields, field_count, struct_generic, struct_count, generic);

        Register* child = make_child(global_reg_ptr);

        EntityID new_struct_eid = register_insert(global_reg_ptr, (RegisterEntry){
            .tag = Reg_Struct,
            .decl_name_range = final_name,
            .data.strct = {
                .fields = fields,
                .fields_count = field_count,
                .generic_params = NULL,
                .generic_params_count = 0,
                .generic_param_nodes = 0,
                .is_pub = structs->data.strct.is_pub,
            }
        });

        child->owner_id = new_struct_eid.id;

        generic_index(g->global_table, sv_from_range(final_name), register_from_global(new_struct_eid.id));
    }

    entry->data.expr_struct_call.name = final_name;
    entry->data.expr_struct_call.generic_args = NULL;
    entry->data.expr_struct_call.generic_args_count = 0;
}


void check_functioncall(RegisterEntry* entry, GenericTables* g) {
    size_t fn_generic_count = entry->data.expr_function_call.generic_args_count;
    if (fn_generic_count == 0) return;

    Type* fn_generic = set_generic_type(entry->data.expr_function_call.generic_args, entry->data.expr_function_call.generic_args_count);
    SourceRange fn_name = entry->data.expr_function_call.name;
    RegisterEntry* fn = register_by_name(sv_from_range(fn_name));

    if (!fn) {
        return;
    }


    Type* generic = set_generic_type(fn->data.function.generic_params, fn->data.function.generic_params_count);
    size_t generic_count = fn->data.function.generic_params_count;

    SourceRange name = set_name(g->global_table, fn_name, fn_generic, fn_generic_count);
    RegisterEntry* existing = register_by_name(sv_from_range(name));

    if (!existing) {
        size_t param_count = fn->data.function.params_count;

        Param* param = malloc(sizeof(Param) * param_count);
        memcpy(param, fn->data.function.params, sizeof(Param) * param_count);

        Type return_type = fn->data.function.return_type;

        param = generic_param(g->global_table, param, param_count, generic, generic_count, fn_generic);
        return_type = generic_return(g->global_table, return_type, generic, generic_count, fn_generic);

        Register* child = make_child(global_reg_ptr);
        size_t body_count = fn->data.function.body_count;
        EntityID new_fn_eid = register_insert(global_reg_ptr, (RegisterEntry){
            .tag = Reg_Function,
            .decl_name_range = name,
            .data.function = {
                .body = fn->data.function.body,
                .body_count = body_count,
                .params = param,
                .params_count = param_count,
                .generic_param_nodes = 0,
                .is_pub = fn->data.function.is_pub,
                .is_unsafe = fn->data.function.is_unsafe,
                .return_type = return_type,
                .child_reg = child,
            }
        });

        child->owner_id = new_fn_eid.id;

        for (size_t i = 0; i < param_count; i++) {
            register_insert_child(child, (RegisterEntry){
                .tag = Reg_Param,
                .decl_name_range = param[i].name,
                .data.var = {
                    .type = param[i].type,
                    .mode = param[i].mode,
                    .is_mut = param[i].mode.mutability == Mutability_Mutable,
                }
            }, new_fn_eid.id);
        }

        if (fn->data.function.child_reg) {
            Register* orig_child = fn->data.function.child_reg;
        
            for (uint32_t i = 0; i < orig_child->counter->next_id; i++) {
                RegisterEntry* orig_entry = register_from_scope(orig_child, i);
                if (!orig_entry) continue;

                if (orig_entry->tag != Reg_Param) {
                    RegisterEntry cloned_entry = *orig_entry;

                    register_insert_child(child, cloned_entry, new_fn_eid.id);
                }
            }
        }

        generic_index(g->global_table, sv_from_range(name), register_from_global(new_fn_eid.id));
        RegisterEntry* new_fn = register_from_global(new_fn_eid.id);


        if (new_fn && new_fn->data.function.child_reg) {
            check_body(new_fn->data.function.child_reg, new_fn_eid.id, g);
        }
    }


    entry->data.expr_function_call.name = name;
    entry->data.expr_function_call.generic_args = NULL;
    entry->data.expr_function_call.generic_args_count = 0;
    
    RegisterEntry* by_name = register_by_name(sv_from_range(name));
}

void check_expr(RegisterEntry* expr, GenericTables* g) {
    switch (expr->tag) {
        case Reg_ExprFunctionCall: check_functioncall(expr, g); break;
        case Reg_ExprEnumCall:     check_enumcall(expr, g); break;
        case Reg_ExprStructCall:   check_structcall(expr, g); break;
    }
}

void check_var(RegisterEntry* entry, GenericTables* g, bool is_mut, bool is_const) {
    Type* type;
    RegisterEntry* init;

    if (is_const) {
        type = &entry->data.const_.type;
        init = entry->data.const_.init;
    } else if (is_mut) {
        type = &entry->data.var.type;
        init = entry->data.var.init;
    } else {
        type = &entry->data.let.type;
        init = entry->data.let.init;
    }

    if (type->tag == Type_Custom && type->data.custom.generic_args_count > 0) {
        SourceRange enum_name = type->data.custom.name;
        SourceRange* raw_args = (SourceRange*)type->data.custom.generic_args;
        size_t args_count = type->data.custom.generic_args_count;
        Type* args = set_generic_type(raw_args, args_count);
        SourceRange final_name = set_name(g->global_table, enum_name, args, args_count);

        if (!register_by_name(sv_from_range(final_name))) {
            RegisterEntry* enums = register_by_name(sv_from_range(enum_name));
            if (enums) {
                Type* enum_generic = set_generic_type(enums->data.enm.generic_params, enums->data.enm.generic_params_count);
                size_t enum_count = enums->data.enm.generic_params_count;

                size_t variant_count = enums->data.enm.variants_count;
                EnumVariant* variant = malloc(sizeof(EnumVariant) * variant_count);
                memcpy(variant, enums->data.enm.variants, sizeof(EnumVariant) * variant_count);

                variant = generic_variant(g->global_table, variant, variant_count, enum_generic, enum_count, args);

                Register* child = make_child(global_reg_ptr);

                EntityID new_enum_eid = register_insert(global_reg_ptr, (RegisterEntry){
                    .tag = Reg_Enum,
                    .decl_name_range = final_name,
                    .data.enm = {
                        .variants = variant,
                        .variants_count = variant_count,
                        .is_pub = enums->data.enm.is_pub,
                        .child_reg = child,
                    }
                });

                child->owner_id = new_enum_eid.id;
                generic_index(g->global_table, sv_from_range(final_name), register_from_global(new_enum_eid.id));
            }
        }


        type->data.custom.name = final_name;
        type->data.custom.generic_args = NULL;
        type->data.custom.generic_args_count = 0;
    }

    if (entry->data.var.init) {
        check_expr(entry->data.var.init, g);
    }
}


void check_return(RegisterEntry* entry, uint32_t id, GenericTables* g) {
    RegisterEntry* fn = register_from_global(id);
    uint32_t expr = entry->data.return_.expr.id;
    RegisterEntry* expr_entry = register_from_global(expr);
    if (!expr_entry) return;

    entry->data.return_.return_type = fn->data.function.return_type;
    check_expr(expr_entry, g);
}

void check_body(Register* child_reg, uint32_t id, GenericTables* g) {
    for (uint32_t i = 0; i < child_reg->counter->next_id; i++) {
        RegisterEntry* entry = register_from_scope(child_reg, i);
        if (!entry) continue;

        switch (entry->tag) {
            case Reg_ExprFunctionCall: check_functioncall(entry, g); break;
            case Reg_ExprEnumCall:     check_enumcall(entry, g);  break;
            case Reg_ExprStructCall:   check_structcall(entry, g); break;
            case Reg_Var:              check_var(entry, g, true, false); break;
            case Reg_Let:              check_var(entry, g, false, false); break;
            case Reg_Const:            check_var(entry, g, false, true); break;
            case Reg_Return:           check_return(entry, id, g); break;

            case Reg_Match: {
                if (entry->data.match_.expr_child) {
                    check_body(entry->data.match_.expr_child, id, g);
                }

                if (entry->data.match_.default_child) {
                    check_body(entry->data.match_.default_child, id, g);
                }

                for (size_t c = 0; c < entry->data.match_.cases_count; c++) {
                    if (entry->data.match_.arm_children[c]) {
                        check_body(entry->data.match_.arm_children[c], id, g);
                    }
                }
                break;
            }

            case Reg_If:
            case Reg_Elif: {
                if (entry->data.if_.cond_child) {
                    check_body(entry->data.if_.cond_child, id, g);
                }

                if (entry->data.if_.then_child) {
                    check_body(entry->data.if_.then_child, id, g);
                }

                if (entry->data.if_.else_child) {
                    check_body(entry->data.if_.else_child, id, g);
                }
                break;
            }
        }
    }
}

StructParam* generic_field(GenericSeenTable* table, StructParam* field, size_t count, Type* generic, size_t generic_count, Type* fn_generic) {
    if (!fn_generic) {
        fn_generic = generic;
    }

    for (size_t i = 0; i < count; i++) {
        field[i].type = resolve_type(table, field[i].type, generic, generic_count, fn_generic);
    }
    
    return field;
}

EnumVariant* generic_variant(GenericSeenTable* table, EnumVariant* variant, size_t count, Type* generic, size_t generic_count, Type* fn_generic) {
    if (!fn_generic) {
        fn_generic = generic;
    }

    for (size_t v = 0; v < count; v++) {
        for (size_t f = 0; f < variant[v].fields_count; f++) {
            variant[v].fields[f].type = resolve_type(table, variant[v].fields[f].type, generic, generic_count, fn_generic);
        }
    }
    
    return variant;
}

Param* generic_param(GenericSeenTable* table, Param* param, size_t count, Type* generic, size_t generic_count, Type* fn_generic) {
    if (!fn_generic) {
        fn_generic = generic;
    }

    for (size_t i = 0; i < count; i++) {
        param[i].type = resolve_type(table, param[i].type, generic, generic_count, fn_generic);
    }
    
    return param;
}

Type generic_return(GenericSeenTable* g, Type return_type, Type* generic, size_t generic_count, Type* fn_generic) {
    if (!fn_generic) {
        fn_generic = generic;
    }

    Type resolved_type = resolve_type(g, return_type, generic, generic_count, fn_generic);

    return resolved_type;
}

void check_struct(uint32_t id, GenericTables* g) {
    RegisterEntry* structs = register_from_global(id);
    if (!structs) {
        return;
    }

    SourceRange original_name = structs->decl_name_range;
    StringView original_sv = sv_from_range(original_name);  

    if (generic_contains(g->global_table, original_sv)) {
        return;
    }
}


void check_enum(uint32_t id, GenericTables* g) {
    RegisterEntry* enums = register_from_global(id);
    if (!enums) {
        return;
    }

    SourceRange original_name = enums->decl_name_range;
    StringView original_sv = sv_from_range(original_name);  

    if (generic_contains(g->global_table, original_sv)) {
        return;
    }

    if (enums->data.enm.child_reg) {
        check_body(enums->data.enm.child_reg, enums->eid.id, g);
    }
}

void check_function(uint32_t id, GenericTables* g) {
    RegisterEntry* fn = register_from_global(id);

    if (!fn || fn->tag != Reg_Function) return;

    SourceRange original_name = fn->decl_name_range;
    StringView original_sv = sv_from_range(original_name);  



    if (fn->data.function.child_reg) {
        check_body(fn->data.function.child_reg, fn->eid.id, g);
    }
}