
#include "import.h"
#include "ast.h"
#include "helper.h"
#include "debugger.h"

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
