#include "import.h"
#include "file_manager.h"
#include "register.h"
#include "lexer.h"
#include "parser.h"
#include "footprint.h"
#include "pack.h"
#include "config.h"
#include "codegen.h"
#include "ir.h"
#include "codegen.h"
#include "helper.h"
#include "generic.h"


#include <llvm-c/Analysis.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Target.h>

void global_registry_init(void);
void print_register_entry(Register* reg, RegisterEntry* e, int depth);
IR_Expr lower_expr(Register *reg, RegisterEntry *entry);
IR_Expr *ir_expr_alloc(IR_Expr expr);
uint64_t pack_hash_source(const char* src, size_t len);
void pack_write_register(Register* reg, CheckerErrList* errors, LineStarts* ls, const char* source, size_t source_len, const char* source_file, const char* project_name);
char* read_file_to_string(const char* path);
void print_expression(Exprs expr, int depth);
void print_statement(Stmts stmt, int depth);
void parser_set_error_list(CheckerErrList* list);
ProjectPack project_pack_sync(const char* source_file, const char* project_name, uint64_t source_hash, uint8_t config_owns_flags, const char* footprint_link, const char* project_root);
ProjectPack project_pack_sync_from_config(const char* source_file, uint64_t source_hash, const Config* cfg);
void func_body_list_free(FuncBodyList* fl);
Type type_from_range(SourceRange r);
void ir_print_module(IR_Module *mod);
IR_Def lower_function(Register *reg, uint32_t id);
IR_Module lower_stmt(Register *reg, IR_Module mod, IR_StmtArr *stmts_out);
IR_Module codegen_def(Register *reg, IR_Module mod);
void cleanup_unresolved_generics(Register* global);
static bool expr_exists(Exprs expr) {
    return expr.tag != 0 || expr.data.literals.range.start != NULL;
}

static void print_type_inline(Type t) {
    switch (t.tag) {
        case Type_Ptr:    printf("*<inner>");               break;
        case Type_RawPtr: printf("**<inner>");              break;
        case Type_Bool:   printf("bool");                   break;
        case Type_Char:   printf("char");                   break;
        case Type_Str:    printf("str");                    break;
        case Type_Void:   printf("void");                   break;
        case Type_Int:
            printf("%sint%d",
                t.data.int_t.is_unsigned ? "u" : "",
                t.data.int_t.bits);
            break;
        case Type_Float:
            printf("float%d", t.data.float_t.bits);
            break;
        case Type_Array:
            printf("arr[%zu]", t.data.array_t.len);
            if (t.data.array_t.inner) {
                printf("<");
                print_type_inline(*t.data.array_t.inner);
                printf(">");
            }
            break;


case Type_Custom:
    printf("%.*s",
        (int)(t.data.custom.name.end - t.data.custom.name.start),
        t.data.custom.name.start);

    if (t.data.custom.generic_args_count > 0) {
        printf("[");
        for (size_t i = 0; i < t.data.custom.generic_args_count; i++) {

            ptrdiff_t len = t.data.custom.generic_args[i].end - t.data.custom.generic_args[i].start;
            printf("[DEBUG] arg[%zu] len=%td\n", i, len);

            if (len <= 0 || len > 256) {
                printf("<BAD_LEN>");
                continue;
            }

            printf("%.*s", (int)len, t.data.custom.generic_args[i].start);
            if (i + 1 < t.data.custom.generic_args_count) printf(", ");
        }
        printf("]");
    }
    break;            default: printf("?(tag=%d)", t.tag); break;

    }
}

static void print_child_registry(Register* child, int depth) {
    if (!child) return;
    for (khint_t k = 0; k != kh_end(child->table); k++) {
        if (!kh_exist(child->table, k)) continue;
        print_register_entry(child, &kh_val(child->table, k), depth);
    }
}
static void print_register_entry(Register* reg, RegisterEntry* e, int depth) {
    for (int i = 0; i < depth; i++) printf("  ");
    switch (e->tag) {
        case Reg_Function:
            printf("fn %.*s() -> ",
                (int)(e->decl_name_range.end - e->decl_name_range.start),
                e->decl_name_range.start);
            print_type_inline(e->data.function.return_type);
            printf(" {\n");
            print_child_registry(e->data.function.child_reg, depth + 1);
            for (int i = 0; i < depth; i++) printf("  ");
            printf("}\n");
            break;

            case Reg_ExprIdx:
    printf("Idx (base_id=%u index_id=%u)\n",
        e->data.idx.base_id, e->data.idx.index_id);
    {
        RegisterEntry* base  = register_from_scope(reg, e->data.idx.base_id);
        RegisterEntry* index = register_from_scope(reg, e->data.idx.index_id);
        if (base)  print_register_entry(reg, base,  depth + 1);
        if (index) print_register_entry(reg, index, depth + 1);
    }
    break;

    
        case Reg_Param:
            printf("param %.*s: ",
                (int)(e->decl_name_range.end - e->decl_name_range.start),
                e->decl_name_range.start);
            print_type_inline(e->data.var.type);
            printf("%s\n", e->data.var.is_mut ? " [mut]" : "");
            break;

        case Reg_Var:
            printf("var %.*s: ",
                (int)(e->decl_name_range.end - e->decl_name_range.start),
                e->decl_name_range.start);
            print_type_inline(e->data.var.type);
            printf("%s\n", e->data.var.is_mut ? " [mut]" : "");
            if (e->data.var.init) print_register_entry(reg, e->data.var.init, depth + 1);
            break;

        case Reg_Let:
            printf("let %.*s: ",
                (int)(e->decl_name_range.end - e->decl_name_range.start),
                e->decl_name_range.start);
            print_type_inline(e->data.let.type);
            printf("\n");
            if (e->data.let.init) print_register_entry(reg, e->data.let.init, depth + 1);
            break;

        case Reg_Const:
            printf("const %.*s: ",
                (int)(e->decl_name_range.end - e->decl_name_range.start),
                e->decl_name_range.start);
            print_type_inline(e->data.const_.type);
            printf("\n");
            if (e->data.const_.init) print_register_entry(reg, e->data.const_.init, depth + 1);
            break;

            case Reg_If:
            printf("if {\n");
            if (e->data.if_.cond_id) print_register_entry(reg, register_from_scope(e->data.if_.cond_child, e->data.if_.cond_id), depth + 1);
            print_child_registry(e->data.if_.then_child, depth + 1);
            print_child_registry(e->data.if_.else_child, depth + 1);
            for (int i = 0; i < depth; i++) printf("  ");
            printf("}\n");
            break;

        case Reg_Elif:
            printf("elif {\n");
            if (e->data.if_.cond_id) print_register_entry(reg, register_from_scope(e->data.if_.cond_child, e->data.if_.cond_id), depth + 1);
            print_child_registry(e->data.if_.then_child, depth + 1);
            print_child_registry(e->data.if_.else_child, depth + 1);
            for (int i = 0; i < depth; i++) printf("  ");
            printf("}\n");
            break;

case Reg_While:
    printf("while {\n");
    if (e->data.while_.cond_id) {
        RegisterEntry* cond = register_from_scope(e->data.while_.cond_child, e->data.while_.cond_id);
        printf("[printer] while cond lookup id=%u -> %p\n", e->data.while_.cond_id, (void*)cond);
        if (cond) print_register_entry(e->data.while_.cond_child, cond, depth + 1);
    }
    print_child_registry(e->data.while_.body_child, depth + 1);
    for (int i = 0; i < depth; i++) printf("  ");
    printf("}\n");
    break;

        case Reg_For:
            printf("for {\n");
            print_child_registry(e->data.for_.iter_child, depth + 1);
            print_child_registry(e->data.for_.body_child, depth + 1);
            for (int i = 0; i < depth; i++) printf("  ");
            printf("}\n");
            break;

        case Reg_Match:
            printf("match {\n");
            print_child_registry(e->data.match_.expr_child, depth + 1);
            print_child_registry(e->data.match_.default_child, depth + 1);
            for (int i = 0; i < depth; i++) printf("  ");
            printf("}\n");
            break;

        case Reg_Class:
            printf("class %.*s {\n",
                (int)(e->decl_name_range.end - e->decl_name_range.start),
                e->decl_name_range.start);
            for (size_t i = 0; i < e->data._class.methods_count; i++) {
                for (int d = 0; d < depth + 1; d++) printf("  ");
                printf("method %.*s\n",
                    (int)(e->data._class.methods[i].name.end - e->data._class.methods[i].name.start),
                    e->data._class.methods[i].name.start);
            }
            for (int i = 0; i < depth; i++) printf("  ");
            printf("}\n");
            break;

        case Reg_Struct:
            printf("struct %.*s\n",
                (int)(e->decl_name_range.end - e->decl_name_range.start),
                e->decl_name_range.start);
            break;

        case Reg_ExprStmt: {
            printf("EXPR_STMT\n");
            RegisterEntry* inner = register_from_scope(reg, e->data.expr_stmt_.expr_id);
            if (inner) print_register_entry(reg, inner, depth + 1);
            break;
        }

        case Reg_ExprLiteral:
            printf("LIT %.*s\n",
                (int)(e->decl_range.end - e->decl_range.start),
                e->decl_range.start);
            break;

        case Reg_ExprIdentifier:
            printf("Ident: %.*s\n",
                (int)(e->data.expr_identifier.name.end - e->data.expr_identifier.name.start),
                e->data.expr_identifier.name.start);
            break;

        case Reg_ExprVar:
            printf("VarRef: %.*s\n",
                (int)(e->data.expr_var.name.end - e->data.expr_var.name.start),
                e->data.expr_var.name.start);
            break;

        case Reg_ExprFunctionCall:
            printf("Call: %.*s (args=%zu)\n",
                (int)(e->data.expr_function_call.name.end - e->data.expr_function_call.name.start),
                e->data.expr_function_call.name.start,
                e->data.expr_function_call.arg_ids_count);
            for (size_t i = 0; i < e->data.expr_function_call.arg_ids_count; i++) {
                for (int d = 0; d < depth + 1; d++) printf("  ");
                if (!e->data.expr_function_call.child_reg) {
                    printf("ARG[%zu] id=%u [no child_reg]\n", i, e->data.expr_function_call.arg_ids[i]);
                    continue;
                }
                RegisterEntry* arg = register_from_scope(e->data.expr_function_call.child_reg,
                    e->data.expr_function_call.arg_ids[i]);
                if (arg) {
                    printf("ARG[%zu]:\n", i);
                    print_register_entry(reg, arg, depth + 2);
                } else {
                    printf("ARG[%zu] id=%u [unresolved]\n", i, e->data.expr_function_call.arg_ids[i]);
                }
            }
            break;

case Reg_ExprBinaryOp: {
    printf("BinaryOp (tag=%d)\n", e->data.expr_binary_op.op);
    RegisterEntry* bleft  = register_from_scope(reg, e->data.expr_binary_op.left_id);
    RegisterEntry* bright = register_from_scope(reg, e->data.expr_binary_op.right_id);
    if (bleft)  print_register_entry(reg, bleft,  depth + 1);
    if (bright) print_register_entry(reg, bright, depth + 1);
    break;
}

        case Reg_ExprUnary:
            printf("UnaryOp (tag=%d)\n", e->data.expr_unary.op);
            if (e->data.expr_unary.operand) print_register_entry(reg, e->data.expr_unary.operand, depth + 1);
            break;

        case Reg_ExprField:
            printf("Field: .%.*s\n",
                (int)(e->data.expr_field.field.end - e->data.expr_field.field.start),
                e->data.expr_field.field.start);
            if (e->data.expr_field.object) print_register_entry(reg, e->data.expr_field.object, depth + 1);
            break;

        case Reg_ExprMethodCall:
            printf("MethodCall: .%.*s (args=%zu)\n",
                (int)(e->data.expr_method_call.method.end - e->data.expr_method_call.method.start),
                e->data.expr_method_call.method.start,
                e->data.expr_method_call.args_count);
            if (e->data.expr_method_call.object) print_register_entry(reg, e->data.expr_method_call.object, depth + 1);
            for (size_t i = 0; i < e->data.expr_method_call.args_count; i++)
                if (e->data.expr_method_call.args[i]) print_register_entry(reg, e->data.expr_method_call.args[i], depth + 1);
            break;

        case Reg_Return:
            printf("RETURN (has_expr=%d)\n", e->data.return_.has_expr);
            break;

        case Reg_Assign:
            printf("ASSIGN (op=%d)\n", e->data.assign.op);
            break;

        default:
            printf("// entry tag=%d\n", e->tag);
            break;
    }
}

static void print_type(Type* t) {
    if (!t) { printf("?"); return; }
    switch (t->tag) {
        case Type_Ptr:    printf("*"); print_type(t->data.ptr.inner);     break;
        case Type_RawPtr: printf("**"); print_type(t->data.raw_ptr.inner); break;
        case Type_Int:    printf("int%d", t->data.int_t.bits);            break;
        case Type_Float:  printf("float%d", t->data.float_t.bits);        break;
        case Type_Bool:   printf("bool");                                  break;
        case Type_Char:   printf("char");                                  break;
        case Type_Str:    printf("str");                                   break;
        case Type_Void:   printf("void");                                  break;
        case Type_Custom:
            printf("%.*s", (int)(t->data.custom.name.end - t->data.custom.name.start), t->data.custom.name.start);
            break;
        default:          printf("?");                                     break;
    }
}

char *read_file_to_string(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Error: Could not open file '%s'\n", path);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *buffer = malloc(length + 1);
    if (buffer) {
        fread(buffer, 1, length, file);
        buffer[length] = '\0';
    }
    fclose(file);
    return buffer;
}



void print_expression(Exprs expr, int depth) {
    for (int i = 0; i < depth; i++) printf("  ");

    if (expr.tag == 0 && expr.data.literals.range.start == NULL) {
        printf("[Empty Expr]\n");
        return;
    }

    switch (expr.tag) {
        case Expr_Literals:
            printf("Literal: %.*s\n",
                (int)(expr.data.literals.range.end - expr.data.literals.range.start),
                expr.data.literals.range.start);
            break;
            
        case Expr_Identifiers:
            printf("Ident: %.*s\n",
                (int)(expr.data.identifiers.name.end - expr.data.identifiers.name.start),
                expr.data.identifiers.name.start);
            break;
        case Expr_Vars:
            printf("Var: %.*s\n",
                (int)(expr.data.vars.name.end - expr.data.vars.name.start),
                expr.data.vars.name.start);
            break;
        case Expr_BinaryOps:
            printf("BinaryOp (Tag: %d)\n", expr.data.binary_ops.op);
            if (expr.data.binary_ops.left)  print_expression(*expr.data.binary_ops.left,  depth + 1);
            if (expr.data.binary_ops.right) print_expression(*expr.data.binary_ops.right, depth + 1);
            break;
        case Expr_Unary:
            printf("UnaryOp (Tag: %d)\n", expr.data.unary.op);
            if (expr.data.unary.operand) print_expression(*expr.data.unary.operand, depth + 1);
            break;
            
            case Expr_Idx:
    printf("Idx\n");
    if (expr.data.idx.base)  print_expression(*expr.data.idx.base,  depth + 1);
    if (expr.data.idx.index) print_expression(*expr.data.idx.index, depth + 1);
    break;
case Expr_Function:
    printf("Call: %.*s (param_count=%zu, generic_count=%zu)\n",
        (int)(expr.data.function_call.name.end - expr.data.function_call.name.start),
        expr.data.function_call.name.start,
        expr.data.function_call.param_count,
        expr.data.function_call.generic_params_count);
    for (size_t i = 0; i < expr.data.function_call.generic_params_count; i++) {
        for (int d = 0; d < depth + 1; d++) printf("  ");
        printf("GENERIC[%zu]: %.*s\n", i,
            (int)(expr.data.function_call.generic_params[i].end - expr.data.function_call.generic_params[i].start),
            expr.data.function_call.generic_params[i].start);
    }
    
    break;
        case Expr_Class_Calls:
            printf("ClassCall: %.*s.%.*s\n",
                (int)(expr.data.class_calls.name.end - expr.data.class_calls.name.start),
                expr.data.class_calls.name.start,
                (int)(expr.data.class_calls.function.end - expr.data.class_calls.function.start),
                expr.data.class_calls.function.start);
            for (size_t i = 0; i < expr.data.class_calls.param_count; i++) {
                for (int d = 0; d < depth + 1; d++) printf("  ");
                printf("ARG: %.*s:\n",
                    (int)(expr.data.class_calls.param[i].name.end - expr.data.class_calls.param[i].name.start),
                    expr.data.class_calls.param[i].name.start);
                print_expression(expr.data.class_calls.param[i].value, depth + 2);
            }
            break;
        case Expr_Struct_Calls:
            printf("StructCall: %.*s.%.*s\n",
                (int)(expr.data.struct_calls.name.end - expr.data.struct_calls.name.start),
                expr.data.struct_calls.name.start,
                (int)(expr.data.struct_calls.function.end - expr.data.struct_calls.function.start),
                expr.data.struct_calls.function.start);
            break;
        case Expr_Enum_Calls:
            printf("EnumCall: %.*s::%.*s\n",
                (int)(expr.data.enum_calls.name.end  - expr.data.enum_calls.name.start),
                expr.data.enum_calls.name.start,
                (int)(expr.data.enum_calls.field.end - expr.data.enum_calls.field.start),
                expr.data.enum_calls.field.start);
            break;
        default:
            printf("Expr (Tag: %d)\n", expr.tag);
            break;
    }
}

void print_statement(Stmts stmt, int depth) {
    if (stmt.tag == 0) return;
    for (int i = 0; i < depth; i++) printf("  ");

    switch (stmt.tag) {
            case Stmt_Externs: {

printf("EXTERN abi=\"%.*s\" ffi=\"%.*s\" funcs_count=%zu\n",
    (int)(stmt.data.extern_.abi.end - stmt.data.extern_.abi.start), stmt.data.extern_.abi.start,
    (int)(stmt.data.extern_.ffi.end - stmt.data.extern_.ffi.start), stmt.data.extern_.ffi.start,
    stmt.data.extern_.funcs_count);

if (!stmt.data.extern_.funcs) { printf("  [funcs is NULL]\n"); break; }

for (size_t i = 0; i < stmt.data.extern_.funcs_count; i++) {
    ExternFunction* fn = &stmt.data.extern_.funcs[i];
    

        for (int d = 0; d < depth + 1; d++) printf("  ");

        
        if (!fn->name.start || !fn->name.end || fn->name.end < fn->name.start) {
            printf("EXTERN_FUNC: [bad name range]\n"); continue;
        }
        
        print_type_inline(fn->return_type);

        for (int d = 0; d < depth + 1; d++) printf("  ");
        printf("EXTERN_FUNC: %.*s -> ",
            (int)(fn->name.end - fn->name.start), fn->name.start);
        print_type_inline(fn->return_type);
        printf("\n");

        if (!fn->params) { printf("  [params is NULL, count=%zu]\n", fn->params_count); continue; }

        for (size_t j = 0; j < fn->params_count; j++) {
            for (int d = 0; d < depth + 2; d++) printf("  ");
            Param* p = &fn->params[j];
            printf("PARAM: %.*s: ", (int)(p->name.end - p->name.start), p->name.start);
            print_type_inline(p->type);
            printf("\n");

        }
    }
    break;
}
        case Stmt_Functions:
            printf("FUNC: %.*s%s%s\n",
                (int)(stmt.data.functions.name.end - stmt.data.functions.name.start),
                stmt.data.functions.name.start,
                stmt.data.functions.is_pub    ? " [pub]"    : "",
                stmt.data.functions.is_unsafe ? " [unsafe]" : "");
            for (size_t i = 0; i < stmt.data.functions.params_count; i++) {
                for (int d = 0; d < depth + 1; d++) printf("  ");
                printf("PARAM: %.*s: ",
                    (int)(stmt.data.functions.params[i].name.end - stmt.data.functions.params[i].name.start),
                    stmt.data.functions.params[i].name.start);
                print_type_inline(stmt.data.functions.params[i].type);
                printf("\n");
            }
            for (size_t i = 0; i < stmt.data.functions.body_count; i++)
                print_statement(stmt.data.functions.body[i], depth + 1);
            break;
        case Stmt_Classes:
            printf("CLASS: %.*s%s\n",
                (int)(stmt.data.classes.name.end - stmt.data.classes.name.start),
                stmt.data.classes.name.start,
                stmt.data.classes.is_pub ? " [pub]" : "");
            if (stmt.data.classes.parent.start) {
                for (int d = 0; d < depth + 1; d++) printf("  ");
                printf("EXTENDS: %.*s\n",
                    (int)(stmt.data.classes.parent.end - stmt.data.classes.parent.start),
                    stmt.data.classes.parent.start);
            }
            for (size_t i = 0; i < stmt.data.classes.fields_count; i++) {
                for (int d = 0; d < depth + 1; d++) printf("  ");
                printf("FIELD: %.*s\n",
                    (int)(stmt.data.classes.fields[i].name.end - stmt.data.classes.fields[i].name.start),
                    stmt.data.classes.fields[i].name.start);
            }
            for (size_t i = 0; i < stmt.data.classes.methods_count; i++) {
                for (int d = 0; d < depth + 1; d++) printf("  ");
                printf("METHOD: %.*s\n",
                    (int)(stmt.data.classes.methods[i].name.end - stmt.data.classes.methods[i].name.start),
                    stmt.data.classes.methods[i].name.start);
                for (size_t j = 0; j < stmt.data.classes.methods[i].body_count; j++)
                    print_statement(stmt.data.classes.methods[i].body[j], depth + 2);
            }
            break;
        case Stmt_Traits:
            printf("TRAIT: %.*s%s\n",
                (int)(stmt.data.traits.name.end - stmt.data.traits.name.start),
                stmt.data.traits.name.start,
                stmt.data.traits.is_pub ? " [pub]" : "");
            for (size_t i = 0; i < stmt.data.traits.methods_count; i++) {
                for (int d = 0; d < depth + 1; d++) printf("  ");
                printf("TRAIT_METHOD: %.*s\n",
                    (int)(stmt.data.traits.methods[i].name.end - stmt.data.traits.methods[i].name.start),
                    stmt.data.traits.methods[i].name.start);
                for (size_t j = 0; j < stmt.data.traits.methods[i].body_count; j++)
                    print_statement(stmt.data.traits.methods[i].body[j], depth + 2);
            }
            break;
        case Stmt_Structs:
            printf("STRUCT: %.*s%s\n",
                (int)(stmt.data.structs.name.end - stmt.data.structs.name.start),
                stmt.data.structs.name.start,
                stmt.data.structs.is_pub ? " [pub]" : "");
            for (size_t i = 0; i < stmt.data.structs.fields_count; i++) {
                for (int d = 0; d < depth + 1; d++) printf("  ");
                printf("FIELD: %.*s\n",
                    (int)(stmt.data.structs.fields[i].name.end - stmt.data.structs.fields[i].name.start),
                    stmt.data.structs.fields[i].name.start);
            }
            break;
        case Stmt_Enums:
            printf("ENUM: %.*s%s\n",
                (int)(stmt.data.enums.name.end - stmt.data.enums.name.start),
                stmt.data.enums.name.start,
                stmt.data.enums.is_pub ? " [pub]" : "");
            for (size_t i = 0; i < stmt.data.enums.variants_count; i++) {
                for (int d = 0; d < depth + 1; d++) printf("  ");
                printf("VARIANT: %.*s\n",
                    (int)(stmt.data.enums.variants[i].name.end - stmt.data.enums.variants[i].name.start),
                    stmt.data.enums.variants[i].name.start);
            }
            break;
        case Stmt_Matchs:
            printf("MATCH\n");
            print_expression(stmt.data.matchs.expr, depth + 1);
            for (size_t i = 0; i < stmt.data.matchs.cases_count; i++) {
                for (int d = 0; d < depth + 1; d++) printf("  ");
                printf("CASE\n");
                for (size_t j = 0; j < stmt.data.matchs.cases[i].body_count; j++)
                    print_statement(stmt.data.matchs.cases[i].body[j], depth + 2);
            }
            if (stmt.data.matchs.default_body) {
                for (int d = 0; d < depth + 1; d++) printf("  ");
                printf("DEFAULT\n");
                for (size_t i = 0; i < stmt.data.matchs.default_body_count; i++)
                    print_statement(stmt.data.matchs.default_body[i], depth + 2);
            }
            break;
        case Stmt_Unsafes:
            printf("UNSAFE\n");
            for (size_t i = 0; i < stmt.data.unsafes.body_count; i++)
                print_statement(stmt.data.unsafes.body[i], depth + 1);
            break;
        case Stmt_Whiles:
            printf("WHILE\n");
            print_expression(stmt.data.whiles.cond, depth + 1);
            for (size_t i = 0; i < stmt.data.whiles.body_count; i++)
                print_statement(stmt.data.whiles.body[i], depth + 1);
            break;
        case Stmt_Ifs:
            printf("IF\n");
            print_expression(stmt.data.ifs.cond, depth + 1);
            for (int d = 0; d < depth + 1; d++) printf("  ");
            printf("THEN\n");
            for (size_t i = 0; i < stmt.data.ifs.body_count; i++)
                print_statement(stmt.data.ifs.body[i], depth + 2);
            for (size_t i = 0; i < stmt.data.ifs.else_body_count; i++)
                print_statement(stmt.data.ifs.else_body[i], depth);
            break;
        case Stmt_Elifs:
            printf("ELIF\n");
            print_expression(stmt.data.ifs.cond, depth + 1);
            for (size_t i = 0; i < stmt.data.ifs.body_count; i++)
                print_statement(stmt.data.ifs.body[i], depth + 1);
            if (stmt.data.ifs.else_body_count > 0)
                for (size_t i = 0; i < stmt.data.ifs.else_body_count; i++)
                    print_statement(stmt.data.ifs.else_body[i], depth);
            break;
        case Stmt_Elses:
            printf("ELSE\n");
            for (size_t i = 0; i < stmt.data.elses.body_count; i++)
                print_statement(stmt.data.elses.body[i], depth + 1);
            break;
        case Stmt_Fors:
            printf("FOR: %.*s\n",
                (int)(stmt.data.fors._var.end - stmt.data.fors._var.start),
                stmt.data.fors._var.start);
            print_expression(stmt.data.fors.iter, depth + 1);
            for (size_t i = 0; i < stmt.data.fors.body_count; i++)
                print_statement(stmt.data.fors.body[i], depth + 1);
            break;
        case Stmt_Returns:
            printf("RETURN\n");
            print_expression(stmt.data.returns.expr, depth + 1);
            break;
        case Stmt_Vars:
            printf("VAR: %.*s : ",
                (int)(stmt.data.vars.name.end - stmt.data.vars.name.start),
                stmt.data.vars.name.start);
            print_type_inline(stmt.data.vars.type);
            printf("\n");
            if (expr_exists(stmt.data.vars.value))
                print_expression(stmt.data.vars.value, depth + 1);
            break;
        case Stmt_Lets:
            printf("LET: %.*s\n",
                (int)(stmt.data.lets.name.end - stmt.data.lets.name.start),
                stmt.data.lets.name.start);
            if (expr_exists(stmt.data.lets.value))
                print_expression(stmt.data.lets.value, depth + 1);
            break;
        case Stmt_Locals:
            printf("LOCAL: %.*s\n",
                (int)(stmt.data.locals.name.end - stmt.data.locals.name.start),
                stmt.data.locals.name.start);
            if (expr_exists(stmt.data.vars.value))
                print_expression(stmt.data.vars.value, depth + 1);
            break;
        case Stmt_Consts:
            printf("CONST: %.*s\n",
                (int)(stmt.data.consts.name.end - stmt.data.consts.name.start),
                stmt.data.consts.name.start);
            if (expr_exists(stmt.data.consts.value))
                print_expression(stmt.data.consts.value, depth + 1);
            break;

        case Stmt_Assigns:
            printf("ASSIGN (op: %d)\n", stmt.data.assigns.op);
            print_expression(stmt.data.assigns.target, depth + 1);
            print_expression(stmt.data.assigns.value,  depth + 1);
            break;
        default:
            printf("STMT (Tag: %d)\n", stmt.tag);
            break;
    }
}


int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: vix <command> [args]\n");
        printf("Commands:\n");
        printf("  run <file.vix>   Compile and execute\n");
        printf("  test             Run internal fuzz tests\n");
        return 1;
    }

    const char* cmd = argv[1];

    if (strcmp(cmd, "run") == 0) {
        if (argc < 3) {
            printf("Usage: vix run <filename.vix>\n");
            return 1;
        }
    }


    const char *filename = argv[2];
    char *source = read_file_to_string(filename);
    if (!source) return 1;

    uint64_t src_hash = pack_hash_source(source, strlen(source));
    Config cfg = config_parse_upwards(filename); 
    
    ProjectPack proj = project_pack_sync_from_config(filename, src_hash, &cfg);
    FileManager files = file_manager_new();
    FileId file_id = file_manager_add(&files, filename, source);
    CheckerErrList errors = {0};
    parser_set_error_list(&errors);

    Parser parser = parser_new(lexer_new(file_id, files.slots.data[file_id].source));
    StmtsArr program = {0};

    while (parser_current(&parser).tag != EOFs) {
        size_t pos_before = (size_t)(parser_current(&parser).range.start - source);
        Stmts s = parser_stmt(&parser);
        if (s.tag != 0) ARR_PUSH(program, s);
        if ((size_t)(parser_current(&parser).range.start - source) == pos_before) parser_advance(&parser);
    }


    printf("\n=== AST ===\n");
for (size_t i = 0; i < program.len; i++)
    print_statement(program.data[i], 0);

    printf("\n=== Entre Register ===\n");
    IDCounter counter = { .next_id = 1 };
    global_registry_init();


    Register global_reg  = register_new(NULL, &counter);
    global_reg_ptr = &global_reg;
    FuncBodyList bodies = register_body(program.data, program.len, &global_reg, &errors);


    GenericTables g = generic_new();

    check_generic(&global_reg, &g);


    generic_free(&g);

    printf("\n=== REGISTER (AFTER GENERIC CHANGE) ===\n");
    REG_FOREACH(&global_reg, entry, {
        print_register_entry(&global_reg, entry, 0);
    });

    printf("\n=== IR MODULE ===\n");
    IR_Module ir_mod = lower_stmt(&global_reg, (IR_Module){ .name = (char*)filename }, NULL);
    ir_print_module(&ir_mod);


    
    printf("[dump] global register table:\n");
for (khint_t i = 0; i != kh_end(global_reg.table); i++) {
    if (!kh_exist(global_reg.table, i)) continue;
    StringView k = kh_key(global_reg.table, i);
    RegisterEntry* v = kh_val(global_reg.table, i);

    printf("[dump]   key='%.*s' tag=%d eid=%u\n",
        (int)k.len, k.ptr, v->tag, v->eid.id);
}

    printf("\n=== CODEGEN ===\n");
    codegen_new(filename, source);

fflush(stdout);
codegen_def(&global_reg, ir_mod);

char *verify_err = NULL;
if (LLVMVerifyModule(llvm_mod, LLVMReturnStatusAction, &verify_err)) {
    fprintf(stderr, "[codegen] LLVM module verification FAILED:\n%s\n", verify_err);
    LLVMDisposeMessage(verify_err);
    char *ir_str = LLVMPrintModuleToString(llvm_mod);
    if (ir_str) { printf("%s\n", ir_str); LLVMDisposeMessage(ir_str); }
    return 1;
}
if (verify_err) LLVMDisposeMessage(verify_err);

LLVMInitializeNativeTarget();
LLVMInitializeNativeAsmPrinter();
LLVMInitializeNativeAsmParser();

char *target_triple = LLVMGetDefaultTargetTriple();
char *target_err = NULL;
LLVMTargetRef target;
if (LLVMGetTargetFromTriple(target_triple, &target, &target_err)) {
    fprintf(stderr, "[codegen] Failed to get target: %s\n", target_err);
    LLVMDisposeMessage(target_err);
    return 1;
}

LLVMTargetMachineRef target_machine = LLVMCreateTargetMachine(
    target, target_triple, "generic", "",
    LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault
);


LLVMSetTarget(llvm_mod, target_triple);
LLVMTargetDataRef data_layout = LLVMCreateTargetDataLayout(target_machine);
char *data_layout_str = LLVMCopyStringRepOfTargetData(data_layout);
LLVMSetDataLayout(llvm_mod, data_layout_str);
LLVMDisposeMessage(data_layout_str);

char base_name[1024];
strncpy(base_name, filename, sizeof(base_name) - 1);
base_name[sizeof(base_name) - 1] = '\0';

char *dot = strrchr(base_name, '.');
if (dot && strcmp(dot, ".vix") == 0) {
    *dot = '\0';
}

char ll_path[1024];
snprintf(ll_path, sizeof(ll_path), "%s.ll", base_name);
char *ir_str = LLVMPrintModuleToString(llvm_mod);
if (!ir_str) {
    fprintf(stderr, "[codegen] ERROR: ir_str is NULL\n");
} else {
    FILE *ll_file = fopen(ll_path, "w");
    if (ll_file) {
        fputs(ir_str, ll_file);
        fclose(ll_file);
    } else {
        fprintf(stderr, "[codegen] ERROR: could not open %s for writing\n", ll_path);
    }
    LLVMDisposeMessage(ir_str);
}

char obj_path[1024];
snprintf(obj_path, sizeof(obj_path), "%s.o", base_name);

char bin_path[1024];
snprintf(bin_path, sizeof(bin_path), "%s.exe", base_name);

char *emit_err = NULL;
if (LLVMTargetMachineEmitToFile(target_machine, llvm_mod, obj_path, LLVMObjectFile, &emit_err)) {
    fprintf(stderr, "[codegen] Failed to emit object file: %s\n", emit_err);
    LLVMDisposeMessage(emit_err);
    return 1;
}

char link_cmd[2048];
snprintf(link_cmd, sizeof(link_cmd),
    "clang \"%s\" -o \"%s\" -fuse-ld=lld -Wl,/GUARD:NO -lkernel32 -luser32 -lmsvcrt",
    obj_path, bin_path);

int link_status = system(link_cmd);
if (link_status != 0) {
    fprintf(stderr, "[codegen] Link step failed (exit code %d)\n", link_status);
    return 1;
}

LLVMDisposeTargetMachine(target_machine);
LLVMDisposeMessage(target_triple);

    printf("[codegen] done\n");

    register_free(&global_reg);
    func_body_list_free(&bodies);
    ARR_FREE(ir_mod.defs);  
    file_manager_free(&files);
    free(source);

    #ifdef VIX_LEAK_TRACK
        leak_track_report();
    #endif
        return 0;

    return 0;
}