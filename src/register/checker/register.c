// #include "import.h"
// #include "register.h"
// #include "type.h"


// #define _POSIX_C_SOURCE 200809L
// #include <string.h>

// bool register_expr(Exprs* expr, Register* reg, CheckerErrList* errors);
// bool register_body(Stmts* body, size_t count, Register* reg, CheckerErrList* errors);
// void resolve_operations(Exprs* expr, Register* reg, CheckerErrList* errors);
// char* type_tag_to_str(Type t);

// Register register_new(Register* parent) {
//     return (Register){ .table = NULL, .parent = parent };
// }

// void register_free(Register* reg) {
//     RegisterBucket *cur, *tmp;
//     HASH_ITER(hh, reg->table, cur, tmp) {
//         HASH_DEL(reg->table, cur);
//         free(cur->key);
//         free(cur);
//     }
// }


// void register_insert(Register* reg, const char* name, RegisterEntry entry) {
//     RegisterBucket* existing = NULL;
//     HASH_FIND_STR(reg->table, name, existing);
//     if (existing) { existing->value = entry; return; }
//     RegisterBucket* bucket = malloc(sizeof(RegisterBucket));
//     bucket->key   = strdup(name);
//     bucket->value = entry;
//     HASH_ADD_KEYPTR(hh, reg->table, bucket->key, strlen(bucket->key), bucket);
// }

// RegisterEntry* register_get(Register* reg, const char* name) {
//     for (Register* r = reg; r != NULL; r = r->parent) {
//         RegisterBucket* found = NULL;
//         HASH_FIND_STR(r->table, name, found);
//         if (found) return &found->value;
//     }
//     return NULL;
// }


// Type resolve_type(SourceRange r, Register* reg) {
//     size_t len = r.end - r.start;

//     if (len == 3 && memcmp(r.start, "i32",  3) == 0) return (Type){ .tag = Type_Int,   .data.int_t.bits   = 32 };
//     if (len == 3 && memcmp(r.start, "i64",  3) == 0) return (Type){ .tag = Type_Int,   .data.int_t.bits   = 64 };
//     if (len == 3 && memcmp(r.start, "f32",  3) == 0) return (Type){ .tag = Type_Float, .data.float_t.bits = 32 };
//     if (len == 3 && memcmp(r.start, "f64",  3) == 0) return (Type){ .tag = Type_Float, .data.float_t.bits = 64 };
//     if (len == 4 && memcmp(r.start, "bool", 4) == 0) return (Type){ .tag = Type_Bool };
//     if (len == 4 && memcmp(r.start, "char", 4) == 0) return (Type){ .tag = Type_Char };
//     if (len == 3 && memcmp(r.start, "str",  3) == 0) return (Type){ .tag = Type_Str  };

//     char* name = strndup(r.start, len);
//     RegisterEntry* entry = register_get(reg, name);
//     free(name);

//     if (entry) {
//         switch (entry->tag) {
//             case Reg_Class:  return (Type){ .tag = Type_Custom, .data.custom.name = r };
//             case Reg_Struct: return (Type){ .tag = Type_Custom, .data.custom.name = r };
//             case Reg_Enum:   return (Type){ .tag = Type_Custom, .data.custom.name = r };
//             default: break;
//         }
//     }

//     return (Type){ .tag = Type_Void };
// }

// Type infer_literal_type(SourceRange range) {
//     size_t len = range.end - range.start;
//     if (len == 0) return (Type){ .tag = Type_Void };
//     if ((len == 4 && memcmp(range.start, "true",  4) == 0) ||
//         (len == 5 && memcmp(range.start, "false", 5) == 0))
//         return (Type){ .tag = Type_Bool };
//     if (range.start[0] == '"')  return (Type){ .tag = Type_Str  };
//     if (range.start[0] == '\'') return (Type){ .tag = Type_Char };

//     bool has_dot = false;
//     for (size_t i = 0; i < len; i++) {
//         if (range.start[i] == '.') { has_dot = true; break; }
//     }

//     if (has_dot) return (Type){ .tag = Type_Float, .data.float_t.bits = 64 };
//     return (Type){ .tag = Type_Int, .data.int_t.bits = 32 };
// }

// Type infer_expr_type(Exprs* expr, Register* reg) {
//     if (!expr) return (Type){ .tag = Type_Void };

//     switch (expr->tag) {
//         case Expr_Literals: return infer_literal_type(expr->data.literals.range);

//         case Expr_Identifiers:
//         case Expr_Vars: {
//             SourceRange r = (expr->tag == Expr_Identifiers) ? expr->data.identifiers.name : expr->data.vars.name;
//             char* name = strndup(r.start, r.end - r.start);
//             RegisterEntry* entry = register_get(reg, name);
//             free(name);
//             if (entry) return entry->type;
//             return (Type){ .tag = Type_Void };
//         }

//         case Expr_BinaryOps: {
//             LexerTokenTag op = expr->data.binary_ops.op;
//             if (op == DoubleEqualss   || op == NotEqualss     ||
//                 op == Lesses          || op == Greaters        ||
//                 op == LessEqualss     || op == GreaterEqualss  ||
//                 op == Ands            || op == Ors)
//                 return (Type){ .tag = Type_Bool };
//             return infer_expr_type(expr->data.binary_ops.left, reg);
//         }

//         case Expr_Function: {
//             char* name = strndup(
//                 expr->data.function_call.name.start,
//                 expr->data.function_call.name.end - expr->data.function_call.name.start
//             );
//             RegisterEntry* entry = register_get(reg, name);
//             free(name);
//             if (entry && entry->tag == Reg_Function) return entry->data.function.return_type;
//             return (Type){ .tag = Type_Void };
//         }

//         case Expr_MethodCalls: {
//             return (Type){ .tag = Type_Void };
//         }

//         case Expr_Class_Calls:
//         case Expr_Struct_Calls: {
//             SourceRange r = (expr->tag == Expr_Class_Calls)
//                 ? expr->data.class_calls.name
//                 : expr->data.struct_calls.name;
//             return (Type){ .tag = Type_Custom, .data.custom.name = r };
//         }

//         default:
//             return (Type){ .tag = Type_Void };
//     }
// }


// bool register_class(Stmts* stmt, Register* reg, CheckerErrList* errors) {
//     size_t len = stmt->data.classes.name.end - stmt->data.classes.name.start;
//     char*  key = strndup(stmt->data.classes.name.start, len);
//     RegisterEntry* existing = register_get(reg, key);

//     if (existing) {
//         bool allowed = existing->tag == Reg_Struct || existing->tag == Reg_Enum;
//         if (!allowed) {
//             checker_err_push(errors, (CheckerErr){
//                 .tag      = Err_Tag_RDL,
//                 .data.rdl = {
//                     .file     = stmt->data.classes.range.pos.file,
//                     .line     = stmt->data.classes.range.pos.line,
//                     .col      = stmt->data.classes.range.pos.col,
//                     .var_name = key,
//                 }
//             });
//             free(key);
//             return true;
//         }

//         stmt->data.classes.attached_tag = existing->tag == Reg_Struct ? ClassAttach_Struct : ClassAttach_Enum;
//         stmt->data.classes.attached_fields       = existing->data.strct.fields;
//         stmt->data.classes.attached_fields_count = existing->data.strct.fields_count;
//     }

//     register_insert(reg, key, (RegisterEntry){
//         .tag  = Reg_Class,
//         .name = key,
//         .type = (Type){ .tag = Type_Custom, .data.custom.name = stmt->data.classes.name },
//         .data.class = {
//             .methods               = stmt->data.classes.methods,
//             .methods_count         = stmt->data.classes.methods_count,
//             .has_attached          = existing != NULL,
//             .attached_tag          = stmt->data.classes.attached_tag,
//             .attached_fields       = stmt->data.classes.attached_fields,
//             .attached_fields_count = stmt->data.classes.attached_fields_count,
//         }
//     });
// }

// bool register_var(Stmts* stmt, Register* reg, CheckerErrList* errors) {
//     size_t len = stmt->data.vars.name.end - stmt->data.vars.name.start;
//     char*  key = strndup(stmt->data.vars.name.start, len);

//     if (register_get(reg, key)) {
//         checker_err_push(errors, (CheckerErr){
//             .tag      = Err_Tag_RDL,
//             .data.rdl = {
//                 .file     = stmt->data.vars.range.pos.file,
//                 .line     = stmt->data.vars.range.pos.line,
//                 .col      = stmt->data.vars.range.pos.col,
//                 .var_name = key,
//             }
//         });
//         free(key); return false;
//     }

//     Type t = (stmt->data.vars.c_type.start != stmt->data.vars.c_type.end)
//         ? resolve_type(stmt->data.vars.c_type, reg)
//         : infer_expr_type(&stmt->data.vars.value, reg);

//     if (stmt->data.vars.has_value && stmt->data.vars.c_type.start != stmt->data.vars.c_type.end) {
//         Type vt = infer_expr_type(&stmt->data.vars.value, reg);
//         if (t.tag != vt.tag) {
//             checker_err_push(errors, (CheckerErr){
//                 .tag      = Err_Tag_VMV,
//                 .data.vmv = {
//                     .file          = stmt->data.vars.range.pos.file,
//                     .line          = stmt->data.vars.range.pos.line,
//                     .col           = stmt->data.vars.range.pos.col,
//                     .var_name      = key,
//                     .expected_type = type_tag_to_str(t),
//                     .actual_type   = type_tag_to_str(vt),
//                 }
//             });
//             free(key); return false;
//         }
//     }

//     register_insert(reg, key, (RegisterEntry){
//         .tag      = Reg_Var,
//         .name     = key,
//         .type     = t,
//         .data.var = { .type = t, .mode = stmt->data.vars.mode, .is_mut = true }
//     });
//     return true;
// }

// bool register_let(Stmts* stmt, Register* reg, CheckerErrList* errors) {
//     size_t len = stmt->data.lets.name.end - stmt->data.lets.name.start;
//     char*  key = strndup(stmt->data.lets.name.start, len);

//     if (register_get(reg, key)) {
//         checker_err_push(errors, (CheckerErr){
//             .tag      = Err_Tag_RDL,
//             .data.rdl = {
//                 .file     = stmt->data.lets.range.pos.file,
//                 .line     = stmt->data.lets.range.pos.line,
//                 .col      = stmt->data.lets.range.pos.col,
//                 .var_name = key,
//             }
//         });
//         free(key); return false;
//     }

//     Type t = (stmt->data.lets.c_type.start != stmt->data.lets.c_type.end)
//         ? resolve_type(stmt->data.lets.c_type, reg)
//         : infer_expr_type(&stmt->data.lets.value, reg);

//     if (stmt->data.lets.has_value && stmt->data.lets.c_type.start != stmt->data.lets.c_type.end) {
//         Type vt = infer_expr_type(&stmt->data.lets.value, reg);
//         if (t.tag != vt.tag) {
//             checker_err_push(errors, (CheckerErr){
//                 .tag      = Err_Tag_VMV,
//                 .data.vmv = {
//                     .file          = stmt->data.lets.range.pos.file,
//                     .line          = stmt->data.lets.range.pos.line,
//                     .col           = stmt->data.lets.range.pos.col,
//                     .var_name      = key,
//                     .expected_type = type_tag_to_str(t),
//                     .actual_type   = type_tag_to_str(vt),
//                 }
//             });
//             free(key); return false;
//         }
//     }

//     register_insert(reg, key, (RegisterEntry){
//         .tag      = Reg_Let,
//         .name     = key,
//         .type     = t,
//         .data.let = { .type = t, .mode = stmt->data.lets.mode }
//     });
//     return true;
// }

// bool register_const(Stmts* stmt, Register* reg, CheckerErrList* errors) {
//     size_t len = stmt->data.consts.name.end - stmt->data.consts.name.start;
//     char*  key = strndup(stmt->data.consts.name.start, len);

//     if (!stmt->data.consts.has_value) {
//         checker_err_push(errors, (CheckerErr){
//             .tag = Err_Tag_CVN,
//             .data.cvn = {
//                 .file = stmt->data.consts.range.pos.file,
//                 .line = stmt->data.consts.range.pos.line,
//                 .col = stmt->data.consts.range.pos.col,
//                 .var_name = key,
//             }
//         });
//         free(key); return false;
//     }

//     if (register_get(reg, key)) {
//         checker_err_push(errors, (CheckerErr){
//             .tag = Err_Tag_RDL,
//             .data.rdl = {
//                 .file = stmt->data.consts.range.pos.file,
//                 .line = stmt->data.consts.range.pos.line,
//                 .col = stmt->data.consts.range.pos.col,
//                 .var_name = key,
//             }
//         });
//         free(key); return false;
//     }

//     Type t = (stmt->data.consts.c_type.start != stmt->data.consts.c_type.end)
//         ? resolve_type(stmt->data.consts.c_type, reg)
//         : infer_expr_type(&stmt->data.consts.value, reg);

//     register_insert(reg, key, (RegisterEntry){
//         .tag         = Reg_Const,
//         .name        = key,
//         .type        = t,
//         .data.const_ = { .type = t, .is_pub = stmt->data.consts.is_pub }
//     });
//     return true;
// }

// bool register_local(Stmts* stmt, Register* reg, CheckerErrList* errors) {
//     size_t len = stmt->data.locals.name.end - stmt->data.locals.name.start;
//     char*  key = strndup(stmt->data.locals.name.start, len);

//     if (register_get(reg, key)) {
//         checker_err_push(errors, (CheckerErr){
//             .tag      = Err_Tag_RDL,
//             .data.rdl = {
//                 .file     = stmt->data.locals.range.pos.file,
//                 .line     = stmt->data.locals.range.pos.line,
//                 .col      = stmt->data.locals.range.pos.col,
//                 .var_name = key,
//             }
//         });
//         free(key); return false;
//     }

//     Type t = resolve_type(stmt->data.locals.c_type, reg);
//     if (t.tag == Type_Void) {
//         checker_err_push(errors, (CheckerErr){
//             .tag = Err_Tag_TNF,
//             .data.tnf = {
//                 .file      = stmt->data.locals.range.pos.file,
//                 .line      = stmt->data.locals.range.pos.line,
//                 .col       = stmt->data.locals.range.pos.col,
//                 .type_name = key,
//             }
//         });
//         free(key); return false;
//     }

//     register_insert(reg, key, (RegisterEntry){
//         .tag        = Reg_Local,
//         .name       = key,
//         .type       = t,
//         .data.local = { .type = t, .is_pub = stmt->data.locals.is_pub }
//     });
//     return true;
// }



// bool register_expr(Exprs* expr, Register* reg, CheckerErrList* errors) {
//     if (!expr) return true;
//     switch (expr->tag) {

//         default:                return true;
//     }
// }

// bool register_stmt(Stmts* stmt, Register* reg, CheckerErrList* errors) {
//     if (!stmt) return true;
//     switch (stmt->tag) {
//         case Stmt_Vars:   return register_var(stmt, reg, errors);
//         case Stmt_Lets:   return register_let(stmt, reg, errors);
//         case Stmt_Consts: return register_const(stmt, reg, errors);
//         case Stmt_Classes: return register_class(stmt, reg, errors);
//         case Stmt_Functions: {
//             Register child = register_new(reg);
//             for (size_t i = 0; i < stmt->data.functions.params_count; i++) {
//                 Param* p   = &stmt->data.functions.params[i];
//                 size_t len = p->name.end - p->name.start;
//                 char* key = strndup(p->name.start, len);
//                 Type t   = resolve_type(p->c_type, reg);
//                 register_insert(&child, key, (RegisterEntry){
//                     .tag  = Reg_Var,
//                     .name = key,
//                     .type = t,
//                     .data.var = { .type = t, .is_mut = false }
//                 });
//             }
//             bool ok = register_body(stmt->data.functions.body, stmt->data.functions.body_count, &child, errors);
//             register_free(&child);
//             return ok;
//         }
//         case Stmt_Assigns: {
//             Exprs op_expr = {
//                 .tag = Expr_BinaryOps,
//                 .data.binary_ops = {
//                     .left  = &stmt->data.assigns.target,
//                     .right = &stmt->data.assigns.value,
//                     .op    = stmt->data.assigns.op,
//                 }
//             };
//             resolve_operations(&op_expr, reg, errors);
//             return true;
//         }
//         default: return true;
//     }

// }


// bool register_body(Stmts* body, size_t count, Register* reg, CheckerErrList* errors) {
//     bool ok = true;
//     for (size_t i = 0; i < count; i++) ok = register_stmt(&body[i], reg, errors) && ok;
//     return ok;
// }
