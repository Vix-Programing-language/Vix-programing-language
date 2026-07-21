#include "import.h"
#include "register.h"
#include "helper.h"
#include "ast.h"
#include "debugger.h"

void print_register_entry(Register* reg, RegisterEntry* e, int depth);

void print_child_registry(Register* child, int depth) {
    if (!child) return;
    for (khint_t k = 0; k != kh_end(child->table); k++) {
        if (!kh_exist(child->table, k)) continue;
        print_register_entry(child, &kh_val(child->table, k), depth);
    }
}
void print_register_entry(Register* reg, RegisterEntry* e, int depth) {
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

void print_type(Type* t) {
    if (!t) { printf("?"); return; }
    switch (t->tag) {
        case Type_Ptr:    printf("*"); print_type(t->data.ptr.inner);     break;
        case Type_RawPtr: printf("**"); print_type(t->data.raw_ptr.inner); break;
        case Type_Int:    printf("int%d", t->data.int_t.bits);            break;
        case Type_Float:  printf("float%d", t->data.float_t.bits);        break;
        case Type_Bool:   printf("bool");                                  break;
        case Type_Char:   printf("char");                                  break;
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

