#include "import.h"
#include "ast.h"
#include "register.h"
#include "ir.h"
/* ── helpers ─────────────────────────────────────────────────── */

static void indent(int d) { for (int i = 0; i < d; i++) printf("  "); }

#define IRSR(r) (int)((r).end - (r).start), (r).start

static void print_ir_type(Type t) {
    switch (t.tag) {
        case Type_Void:   printf("void");   return;
        case Type_Bool:   printf("bool");   return;
        case Type_Char:   printf("char");   return;
        case Type_Str:    printf("str");    return;
        case Type_Ptr:    printf("*");      print_ir_type(*t.data.ptr.inner);     return;
        case Type_RawPtr: printf("**");     print_ir_type(*t.data.raw_ptr.inner); return;
        case Type_Int:
            printf("%sint%d", t.data.int_t.is_unsigned ? "u" : "", t.data.int_t.bits);
            return;
        case Type_Float:
            printf("float%d", t.data.float_t.bits);
            return;
        case Type_Custom:
            printf("%.*s", IRSR(t.data.custom.name));
            return;
        default: printf("?"); return;
    }
}

/* ── expr ─────────────────────────────────────────────────────── */

static void print_ir_expr(IR_Expr *e, int d) {
    if (!e) { indent(d); printf("<null-expr>\n"); return; }
    indent(d);
    switch (e->tag) {
        case IR_Expr_Literal:
            switch (e->ty.tag) {
                case Type_Int:   printf("LIT int %lld\n",   (long long)e->data.literal.data.int_val);   break;
                case Type_Float: printf("LIT float %g\n",   e->data.literal.data.float_val);            break;
                case Type_Bool:  printf("LIT bool %s\n",    e->data.literal.data.bool_val ? "true" : "false"); break;
                case Type_Char:  printf("LIT char '%c'\n",  e->data.literal.data.char_val);             break;
                case Type_Str:   printf("LIT str %.*s\n",   IRSR(e->data.literal.data.str_range));        break;
                case Type_Ptr:   printf("LIT null\n");                                                   break;
                default:         printf("LIT ?\n");                                                      break;
            }
            break;

        case IR_Expr_VarRef:
            printf("VAR %.*s\n", IRSR(e->data.var_ref.name));
            break;

        case IR_Expr_BinOp:
            printf("BINOP op=%d\n", e->data.bin.op);
            print_ir_expr(e->data.bin.lhs, d + 1);
            print_ir_expr(e->data.bin.rhs, d + 1);
            break;

        case IR_Expr_Cast:
            printf("CAST -> "); print_ir_type(e->ty); printf("\n");
            print_ir_expr(e->data.cast.expr, d + 1);
            break;

        case IR_Expr_Deref:
            printf("DEREF\n");
            print_ir_expr(e->data.deref.ptr, d + 1);
            break;

        case IR_Expr_Field:
            printf("FIELD .%.*s\n", IRSR(e->data.field.field));
            print_ir_expr(e->data.field.object, d + 1);
            break;

        case IR_Expr_Index:
            printf("INDEX\n");
            print_ir_expr(e->data.index.object, d + 1);
            print_ir_expr(e->data.index.index,  d + 1);
            break;

        case IR_Expr_Call:
            printf("CALL %.*s (args=%zu)\n", IRSR(e->data.call.name), e->data.call.args_count);
            for (size_t i = 0; i < e->data.call.args_count; i++)
                print_ir_expr(e->data.call.args[i], d + 1);
            break;

        case IR_Expr_MethodCall:
            printf("METHOD_CALL .%.*s (args=%zu)\n", IRSR(e->data.method_call.method), e->data.method_call.args_count);
            print_ir_expr(e->data.method_call.object, d + 1);
            for (size_t i = 0; i < e->data.method_call.args_count; i++)
                print_ir_expr(e->data.method_call.args[i], d + 1);
            break;

        case IR_Expr_MakeStruct:
            printf("MAKE_STRUCT %.*s (fields=%zu)\n", IRSR(e->data.make_struct.name), e->data.make_struct.fields_count);
            for (size_t i = 0; i < e->data.make_struct.fields_count; i++) {
                indent(d + 1);
                printf(".%.*s =\n", IRSR(e->data.make_struct.fields[i].name));
                print_ir_expr(e->data.make_struct.fields[i].val, d + 2);
            }
            break;

        case IR_Expr_MakeClass:
            printf("MAKE_CLASS %.*s (args=%zu)\n", IRSR(e->data.make_class.name), e->data.make_class.args_count);
            for (size_t i = 0; i < e->data.make_class.args_count; i++)
                print_ir_expr(e->data.make_class.args[i], d + 1);
            break;

        case IR_Expr_MakeEnum:
            printf("MAKE_ENUM %.*s::%.*s (args=%zu)\n",
                IRSR(e->data.make_enum.type_name), IRSR(e->data.make_enum.variant),
                e->data.make_enum.args_count);
            for (size_t i = 0; i < e->data.make_enum.args_count; i++)
                print_ir_expr(e->data.make_enum.args[i], d + 1);
            break;

        case IR_Expr_MakeTuple:
            printf("MAKE_TUPLE (elems=%zu)\n", e->data.make_tuple.elems_count);
            for (size_t i = 0; i < e->data.make_tuple.elems_count; i++)
                print_ir_expr(e->data.make_tuple.elems[i], d + 1);
            break;

        case IR_Expr_TupleIndex:
            printf("TUPLE_IDX [%zu]\n", e->data.tuple_index.index);
            print_ir_expr(e->data.tuple_index.tuple, d + 1);
            break;

        default:
            printf("EXPR(tag=%d)\n", e->tag);
            break;
    }
}


static void print_ir_stmts(IR_Stmt *stmts, size_t count, int d);

static void print_ir_stmt(IR_Stmt *s, int d) {
    indent(d);
    switch (s->tag) {

        case IR_Stmt_VarDecl:
            printf("VAR %.*s: ", IRSR(s->data.var_decl.name));
            print_ir_type(s->data.var_decl.ty);
            printf("\n");
            if (s->data.var_decl.init) print_ir_expr(s->data.var_decl.init, d + 1);
            break;

        case IR_Stmt_LetDecl:
            printf("LET %.*s: ", IRSR(s->data.let_decl.name));
            print_ir_type(s->data.let_decl.ty);
            printf("\n");
            if (s->data.let_decl.init) print_ir_expr(s->data.let_decl.init, d + 1);
            break;

        case IR_Stmt_ConstDecl:
            printf("CONST %.*s: ", IRSR(s->data.const_decl.name));
            print_ir_type(s->data.const_decl.ty);
            printf("\n");
            if (s->data.const_decl.init) print_ir_expr(s->data.const_decl.init, d + 1);
            break;

        case IR_Stmt_LocalDecl:
            printf("LOCAL %.*s: ", IRSR(s->data.local_decl.name));
            print_ir_type(s->data.local_decl.ty);
            printf("\n");
            break;

        case IR_Stmt_Assign:
            printf("ASSIGN op=%d\n", s->data.assign.op);
            print_ir_expr(s->data.assign.target, d + 1);
            print_ir_expr(s->data.assign.value,  d + 1);
            break;

        case IR_Stmt_Return:
            printf("RETURN\n");
            if (s->data.ret.val) print_ir_expr(s->data.ret.val, d + 1);
            break;

        case IR_Stmt_Expr:
            printf("EXPR_STMT\n");
            if (s->data.expr.expr) print_ir_expr(s->data.expr.expr, d + 1);
            break;

        case IR_Stmt_If:
            printf("IF\n");
            print_ir_expr(s->data.if_.cond, d + 1);
            indent(d); printf("THEN\n");
            print_ir_stmts(s->data.if_.body, s->data.if_.body_count, d + 1);
            if (s->data.if_.else_body_count) {
                indent(d); printf("ELSE\n");
                print_ir_stmts(s->data.if_.else_body, s->data.if_.else_body_count, d + 1);
            }
            break;

        case IR_Stmt_While:
            printf("WHILE\n");
            print_ir_expr(s->data.while_.cond, d + 1);
            print_ir_stmts(s->data.while_.body, s->data.while_.body_count, d + 1);
            break;

        case IR_Stmt_For:
            printf("FOR %.*s: ", IRSR(s->data.for_.var));
            print_ir_type(s->data.for_.var_ty);
            printf("\n");
            indent(d + 1); printf("ITER\n");
            print_ir_expr(s->data.for_.iter, d + 2);
            print_ir_stmts(s->data.for_.body, s->data.for_.body_count, d + 1);
            break;

        case IR_Stmt_Match:
            printf("MATCH\n");
            print_ir_expr(s->data.match.expr, d + 1);
            for (size_t i = 0; i < s->data.match.arms_count; i++) {
                indent(d + 1); printf("ARM\n");
                print_ir_stmts(s->data.match.arms[i].body, s->data.match.arms[i].body_count, d + 2);
            }
            if (s->data.match.default_body_count) {
                indent(d + 1); printf("DEFAULT\n");
                print_ir_stmts(s->data.match.default_body, s->data.match.default_body_count, d + 2);
            }
            break;

        case IR_Stmt_AtomicOp:
            printf("ATOMIC_OP op=%d target=%.*s\n", s->data.atomic_op.op, IRSR(s->data.atomic_op.target));
            for (size_t i = 0; i < s->data.atomic_op.args_count; i++)
                print_ir_expr(s->data.atomic_op.args[i], d + 1);
            break;

        case IR_Stmt_SsaTemp:
            printf("SSA_TEMP eid=%u: ", s->data.ssa_temp.eid.id);
            print_ir_type(s->data.ssa_temp.ty);
            printf("\n");
            if (s->data.ssa_temp.val) print_ir_expr(s->data.ssa_temp.val, d + 1);
            break;

        default:
            printf("STMT(tag=%d)\n", s->tag);
            break;
    }
}

static void print_ir_stmts(IR_Stmt *stmts, size_t count, int d) {
    for (size_t i = 0; i < count; i++)
        print_ir_stmt(&stmts[i], d);
}

static void print_ir_funcdef(IR_FuncDef *fn, int d) {
    indent(d);
    printf("fn %.*s(", IRSR(fn->name));
    for (size_t i = 0; i < fn->params_count; i++) {
        if (i) printf(", ");
        printf("%.*s: ", IRSR(fn->params[i].name));
        print_ir_type(fn->params[i].ty);
    }
    printf(") -> ");
    print_ir_type(fn->return_type);
    if (fn->is_pub)    printf(" [pub]");
    if (fn->is_unsafe) printf(" [unsafe]");
    printf("\n");
    print_ir_stmts(fn->body, fn->body_count, d + 1);
}


static void print_ir_def(IR_Def *def) {
    switch (def->tag) {

        case IR_Def_Function:
            print_ir_funcdef(&def->data.function.def, 0);
            break;

        case IR_Def_Struct:
            printf("struct %.*s%s\n",
                IRSR(def->data.struct_.name),
                def->data.struct_.is_pub ? " [pub]" : "");
            for (size_t i = 0; i < def->data.struct_.fields_count; i++) {
                indent(1);
                printf("%.*s: ", IRSR(def->data.struct_.fields[i].name));
                print_ir_type(def->data.struct_.fields[i].ty);
                printf("\n");
            }
            break;

        case IR_Def_Enum:
            printf("enum %.*s%s\n",
                IRSR(def->data.enum_.name),
                def->data.enum_.is_pub ? " [pub]" : "");
            for (size_t i = 0; i < def->data.enum_.variants_count; i++) {
                indent(1);
                printf("variant %.*s\n", IRSR(def->data.enum_.variants[i].name));
                for (size_t j = 0; j < def->data.enum_.variants[i].fields_count; j++) {
                    indent(2);
                    printf("%.*s: ", IRSR(def->data.enum_.variants[i].fields[j].name));
                    print_ir_type(def->data.enum_.variants[i].fields[j].ty);
                    printf("\n");
                }
            }
            break;

        case IR_Def_Class:
            printf("class %.*s%s\n",
                IRSR(def->data.class_.name),
                def->data.class_.is_pub ? " [pub]" : "");
            for (size_t i = 0; i < def->data.class_.fields_count; i++) {
                indent(1);
                printf("field %.*s: ", IRSR(def->data.class_.fields[i].name));
                print_ir_type(def->data.class_.fields[i].ty);
                printf("\n");
            }
            for (size_t i = 0; i < def->data.class_.methods_count; i++)
                print_ir_funcdef(&def->data.class_.methods[i], 1);
            break;

        case IR_Def_Trait:
            printf("trait %.*s%s\n",
                IRSR(def->data.trait_.name),
                def->data.trait_.is_pub ? " [pub]" : "");
            for (size_t i = 0; i < def->data.trait_.methods_count; i++)
                print_ir_funcdef(&def->data.trait_.methods[i], 1);
            break;

        case IR_Def_VTable:
            printf("vtable %.*s impl %.*s\n",
                IRSR(def->data.vtable.trait_name),
                IRSR(def->data.vtable.impl_type));
            for (size_t i = 0; i < def->data.vtable.methods_count; i++)
                print_ir_funcdef(&def->data.vtable.methods[i], 1);
            break;

        case IR_Def_Const:
            printf("const %.*s: ", IRSR(def->data.const_.name));
            print_ir_type(def->data.const_.ty);
            printf("\n");
            if (def->data.const_.init) print_ir_expr(def->data.const_.init, 1);
            break;

        case IR_Def_Var:
            printf("var %.*s: ", IRSR(def->data.var_.name));
            print_ir_type(def->data.var_.ty);
            printf("\n");
            if (def->data.var_.init) print_ir_expr(def->data.var_.init, 1);
            break;

        case IR_Def_Let:
            printf("let %.*s: ", IRSR(def->data.let_.name));
            print_ir_type(def->data.let_.ty);
            printf("\n");
            if (def->data.let_.init) print_ir_expr(def->data.let_.init, 1);
            break;
            
        default:
            if (def->data.extern_.name.start) {
                printf("extern \"%.*s\" fn %.*s(",
                    IRSR(def->data.extern_.abi),
                    IRSR(def->data.extern_.name));
                for (size_t i = 0; i < def->data.extern_.params_count; i++) {
                    if (i) printf(", ");
                    printf("%.*s: ", IRSR(def->data.extern_.params[i].name));
                    print_ir_type(def->data.extern_.params[i].ty);
                }
                printf(") -> ");
                print_ir_type(def->data.extern_.return_type);
                printf("\n");
            } else {
                printf("DEF(tag=%d)\n", def->tag);
            }
            break;
    }
}

void ir_print_module(IR_Module *mod) {
    printf("module \"%s\" (%zu defs)\n", mod->name ? mod->name : "?", mod->defs.len);
    for (size_t i = 0; i < mod->defs.len; i++) {
        print_ir_def(&mod->defs.data[i]);

        print_ir_def(&mod->defs.data[i]);
        printf("\n");
    }
}