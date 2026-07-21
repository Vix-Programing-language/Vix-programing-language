#include "import.h"
#include "register.h"
#include "ir.h"
#include "import.h"
#include "third-party/khashl.h"
#include "helper.h"
#include "register_tables.h"

FieldOwnerKind get_kind(Register* reg, SourceRange name_range) {
    StringView sv = sv_from_range(name_range);
    RegisterEntry* entry = register_get(reg, sv);

    if (!entry) {
        entry = register_by_name(sv);
    }

    if (!entry) {
        return FieldOwner_Unknown;
    }

    switch (entry->tag) {
        case Reg_Class: return FieldOwner_Class;
        case Reg_Struct: return FieldOwner_Struct;
        case Reg_Enum: return FieldOwner_Enum;
        default: return FieldOwner_Unknown;
    }
}

EntityID register_expr_calls(Register* reg, RegisterEntry* parent, Exprs* expr, SourceRange* concrete_args, size_t concrete_args_count) {
    Register* child = make_child(reg);

    SourceRange name;
    size_t argc;
    Param* params;

    switch (expr->tag) {
        case Expr_Class_Calls: {
            name = mangle_name(parent->decl_name_range, expr->data.class_calls.function);
            argc = expr->data.class_calls.param_count;
            params = expr->data.class_calls.param;
            break;
        }
        case Expr_Struct_Calls: {
            name   = mangle_name(parent->decl_name_range, expr->data.struct_calls.function);
            argc   = expr->data.struct_calls.param_count;
            params = expr->data.struct_calls.param;
            break;
        }
        case Expr_Enum_Calls: {
            name   = mangle_name(parent->decl_name_range, expr->data.enum_calls.field);
            argc   = expr->data.enum_calls.param_count;
            params = expr->data.enum_calls.param;
            break;
        }
        default:
            return (EntityID){0};
    }

    uint32_t* arg_ids = argc ? malloc(sizeof(uint32_t) * argc) : NULL;

    for (size_t i = 0; i < argc; i++) {
        RegisterEntry* arg_entry = register_expr(child, &params[i].value, (SourceRange){0});
        arg_ids[i] = arg_entry ? arg_entry->eid.id : 0;
    }

    RegisterEntry entry = (RegisterEntry){
        .tag = Reg_ExprFunctionCall,
        .decl_name_range = name,
        .data.expr_function_call = {
            .name = name,
            .params = params,
            .params_count = argc,
            .generic_args = concrete_args,
            .generic_args_count = concrete_args_count,
            .child_reg = child,
            .arg_ids = arg_ids,
            .arg_ids_count = argc,
        }
    };

    EntityID eid = register_insert(reg, entry);
    return eid;
}

RegisterEntry* register_expr(Register* reg, Exprs* expr, SourceRange class_name) {
    switch (expr->tag) {
            case Expr_Function: {
                if (!expr->data.function_call.name.start ||  expr->data.function_call.name.start == expr->data.function_call.name.end) return NULL;
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
                return register_from_scope(reg, eid.id);
            }

        case Expr_Literals: {
            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprLiteral,
                .decl_range = expr->data.literals.range,
                .data.expr_literal = { .resolved_type = (Type){0} },
            };
            EntityID eid = register_insert(reg, entry);
            return register_from_scope(reg, eid.id);
        }

        case Expr_Identifiers: {
            StringView sv = sv_from_range(expr->data.identifiers.name);
            RegisterEntry* found = register_get(reg, sv);
            
            Type resolved = (Type){0};
            if (found && (found->tag == Reg_Function || found->tag == Reg_ExternFunc)) {
                resolved = (Type){ .tag = Type_FnPtr };
            }

            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprIdentifier,
                .decl_range = expr->data.identifiers.range,
                .data.expr_identifier = {
                    .name = expr->data.identifiers.name,
                    .resolved_type = resolved,
                },
            };
            EntityID eid = register_insert(reg, entry);
            return register_from_scope(reg, eid.id);
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
            return register_from_scope(reg, eid.id);
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
            return register_from_scope(reg, eid.id);
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
            return register_from_scope(reg, eid.id);
        }

        case Expr_Cast: {
            RegisterEntry* inner = register_expr(reg, expr->data.cast.expr, (SourceRange){0});
            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprCast,
                .decl_range = expr->data.cast.range,
                .data.expr_cast = {
                    .expr_id = inner ? inner->eid.id : 0,
                    .ty = expr->data.cast.ty,
                    .child_reg = reg,
                },
            };

            EntityID eid = register_insert(reg, entry);
            return register_from_scope(reg, eid.id);
        }


        case Expr_Idx: {
            RegisterEntry* base  = register_expr(reg, expr->data.idx.base,  class_name);
            RegisterEntry* index = register_expr(reg, expr->data.idx.index, class_name);

            Type elem_ty = infer_expr_type(reg, expr);
            Type base_type = infer_expr_type(reg, expr->data.idx.base);

            bool is_const = (base_type.tag == Type_Ptr) ? base_type.data.ptr.is_const : (base_type.tag == Type_RawPtr) ? base_type.data.raw_ptr.is_const : false;

            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprIdx,
                .decl_range = expr->data.idx.range,
                .data.idx = {
                    .base_id  = base ? base->eid.id : 0,
                    .index_id = index ? index->eid.id : 0,
                    .range    = expr->data.idx.range,
                    .is_const = is_const,
                    .elem_ty  = elem_ty
                },
            };

            EntityID eid = register_insert(reg, entry);
            return register_from_scope(reg, eid.id);
        }

        
        case Expr_Field: {
            SourceRange last_field = expr->data.field_access.field;
            StringView field_sv = sv_from_range(last_field);
            Exprs* root_obj = expr->data.field_access.object;
            size_t chain_depth = 0;
            while (root_obj && root_obj->tag == Expr_Field) {
                root_obj = root_obj->data.field_access.object;
                chain_depth++;
            }

            Register* child = make_child(reg);
            RegisterEntry* object_entry = register_expr(child, root_obj, class_name);

            Type parent_type = infer_expr_type(reg, expr->data.field_access.object);

            FieldOwnerKind owner_kind = FieldOwner_Unknown;
            uint32_t owner_id = 0;

            SourceRange owner_name_range = parent_type.data.custom.name;
            StringView owner_sv = sv_from_range(owner_name_range);

            RegisterEntry* owner_entry = register_get(reg, owner_sv);
            if (!owner_entry) {
                owner_entry = register_by_name(owner_sv);
            }

            owner_id = register_get_id(reg, owner_sv);
            owner_kind = get_kind(reg, owner_name_range);

            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprField,
                .decl_range = expr->data.field_access.range,
                .data.expr_field = {
                    .object = object_entry,
                    .field = last_field,
                    .range = expr->data.field_access.range,
                    .kind = owner_kind,
                    .type_eid = (EntityID){ .id = owner_id },
                    .child_reg = child,
                },
            };

            EntityID eid = register_insert(reg, entry);
            RegisterEntry* result = register_from_scope(reg, eid.id);

            return result;
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
            return register_from_scope(reg, eid.id);
        }

        case Expr_Tuple: {
            size_t n = expr->data.tuple.elems_count;
            RegisterEntry** elems = n ? malloc(sizeof(RegisterEntry*) * n) : NULL;
            for (size_t i = 0; i < n; i++) {
                elems[i] = register_expr(reg, &expr->data.tuple.elems[i], class_name);
            }

            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprTuple,
                .data.expr_tuple = { .elems = elems, .elems_count = n },
            };
            EntityID eid = register_insert(reg, entry);
            return register_from_scope(reg, eid.id);
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
            return register_from_scope(reg, eid.id);
        }

        case Expr_Class_Calls: {
            RegisterEntry* obj_entry = register_expr(reg, expr->data.class_calls.object, class_name);

            Type obj_type = infer_expr_type(reg, expr->data.class_calls.object);
            if (obj_type.tag != Type_Custom) break;

            StringView parent_sv = sv_from_range(obj_type.data.custom.name);
            RegisterEntry* parent = register_get(reg, parent_sv);
            if (!parent) {
                parent = register_by_name(parent_sv);
            }
            if (!parent) break;

            SourceRange* concrete_args = obj_type.data.custom.generic_args;
            size_t concrete_args_count = obj_type.data.custom.generic_args_count;
            EntityID id = register_expr_calls(reg, parent, expr, concrete_args, concrete_args_count); 

            return register_from_scope(reg, id.id);
        }

        case Expr_Struct_Calls: {
            size_t argc = expr->data.struct_calls.param_count;
            Param* params = expr->data.struct_calls.param;

            for (size_t i = 0; i < argc; i++) {
                register_expr(reg, &params[i].value, (SourceRange){0});

                if (params[i].type.tag == 0) {
                    params[i].type = infer_expr_type(reg, &params[i].value);
                }
            }

            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprStructCall,
                .decl_range = expr->data.struct_calls.range,
                .data.expr_struct_call = {
                    .name = expr->data.struct_calls.name,
                    .function = expr->data.struct_calls.function,
                    .params = params,
                    .params_count = argc,
                    .generic_args = expr->data.struct_calls.generic_params,
                    .generic_args_count = expr->data.struct_calls.generic_params_count,
                },
            };
            EntityID eid = register_insert(reg, entry);
            return register_from_scope(reg, eid.id);
        }

        case Expr_Enum_Calls: {
            RegisterEntry* enm = register_by_name(sv_from_range(expr->data.enum_calls.name));
            if (!enm) return NULL;

            EnumVariant* variants = enm->data.enm.variants;
            size_t variant_count = enm->data.enm.variants_count;
            size_t argc = expr->data.enum_calls.param_count;
            Param* params = expr->data.enum_calls.param;
            EnumVariant tallest_variant = {0};
            size_t current = 0;

            for (size_t i = 0; i < variant_count; i++) {
                if (variants[i].fields_count >= current) {
                    current = variants[i].fields_count;
                    tallest_variant = variants[i];
                }
            }

            uint32_t active_tag_idx = 0;
            StringView active_field_sv = sv_from_range(expr->data.enum_calls.field);

            for (size_t i = 0; i < variant_count; i++) {
                StringView v = sv_from_range(variants[i].name);
                if (v.len == active_field_sv.len && memcmp(v.ptr, active_field_sv.ptr, v.len) == 0) {
                    active_tag_idx = (uint32_t)i;
                    break;
                }
            }

            Register* child = make_child(reg);
            uint32_t* arg_ids = argc ? malloc(sizeof(uint32_t) * argc) : NULL;

            for (size_t i = 0; i < argc; i++) {
                RegisterEntry* arg_entry = register_expr(child, &params[i].value, class_name);
                arg_ids[i] = arg_entry ? arg_entry->eid.id : 0;

                params[i].type = infer_expr_type(reg, &params[i].value);
            }

            RegisterEntry entry = (RegisterEntry){
                .tag = Reg_ExprEnumCall,
                .decl_range = expr->data.enum_calls.range,
                .data.expr_enum_call = {
                    .name = expr->data.enum_calls.name,
                    .field = expr->data.enum_calls.field,
                    .resolved_type = (Type){0},
                    .generic_args = expr->data.enum_calls.generic_params,
                    .generic_args_count = expr->data.enum_calls.generic_params_count,
                    .params = params,
                    .params_count = argc,
                    .variant.max_count  = current, 
                    .variant.tallest = tallest_variant, 
                    .variant.active_idx = active_tag_idx, 
                    .child_reg = child,
                    .arg_ids = arg_ids,
                    .arg_ids_count = argc,
                },
            };
            EntityID eid = register_insert(reg, entry);

            return register_from_scope(reg, eid.id);
        }
        case Expr_Null:
        default:
            return NULL;
    }
}
