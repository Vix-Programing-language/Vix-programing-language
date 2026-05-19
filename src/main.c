#include "import.h"
#include "file_manager.h"
#include "register.h"
#include "lexer.h"
#include "parser.h"
#include "footprint.h"
#include "pack.h"   /* NEW */
#include "config.h"

uint64_t pack_hash_source(const char* src, size_t len);
void pack_write_register(Register* reg, CheckerErrList* errors, LineStarts* ls, const char* source, size_t source_len, const char* source_file, const char* project_name);
char*       read_file_to_string(const char* path);
LexerToken* lex_all(FileManager* files, FileId file_id, const char* source, size_t* out_count);
void        print_expression(Exprs expr, int depth);
void        print_statement(Stmts stmt, int depth);
void        parser_set_error_list(CheckerErrList* list);
ProjectPack project_pack_sync(const char* source_file, const char* project_name, uint64_t source_hash, uint8_t config_owns_flags, const char* footprint_link, const char* project_root);
ProjectPack project_pack_sync_from_config(const char* source_file, uint64_t source_hash, const Config* cfg);
Register  register_new(Register* parent, IDCounter* counter);
void      register_insert(Register* reg, StringView name, RegisterEntry entry);
RegisterEntry* register_get(Register* reg, StringView name);
FuncBodyList register_body(Stmts* body, size_t count, Register* reg, CheckerErrList* errors);
void func_body_list_free(FuncBodyList* fl);

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

/* ── AST printers (unchanged) ────────────────────────────────────────────── */
static bool expr_exists(Exprs expr) {
    return expr.tag != 0 || expr.data.literals.range.start != NULL;
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
        case Expr_Function:
            printf("Call: %.*s\n",
                (int)(expr.data.function_call.name.end - expr.data.function_call.name.start),
                expr.data.function_call.name.start);
            for (size_t i = 0; i < expr.data.function_call.param_count; i++) {
                for (int d = 0; d < depth + 1; d++) printf("  ");
                printf("ARG: %.*s:\n",
                    (int)(expr.data.function_call.param[i].name.end - expr.data.function_call.param[i].name.start),
                    expr.data.function_call.param[i].name.start);
                print_expression(expr.data.function_call.param[i].value, depth + 2);
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
        case Stmt_Functions:
            printf("FUNC: %.*s%s%s\n",
                (int)(stmt.data.functions.name.end - stmt.data.functions.name.start),
                stmt.data.functions.name.start,
                stmt.data.functions.is_pub    ? " [pub]"    : "",
                stmt.data.functions.is_unsafe ? " [unsafe]" : "");
            for (size_t i = 0; i < stmt.data.functions.params_count; i++) {
                for (int d = 0; d < depth + 1; d++) printf("  ");
                printf("PARAM: %.*s: %.*s\n",
                    (int)(stmt.data.functions.params[i].name.end - stmt.data.functions.params[i].name.start),
                    stmt.data.functions.params[i].name.start,
                    (int)(stmt.data.functions.params[i].c_type.end - stmt.data.functions.params[i].c_type.start),
                    stmt.data.functions.params[i].c_type.start);
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
            printf("VAR: %.*s\n",
                (int)(stmt.data.vars.name.end - stmt.data.vars.name.start),
                stmt.data.vars.name.start);
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
        case Stmt_ExprStmt:
            printf("EXPR_STMT\n");
            print_expression(stmt.data.expr_stmt.expr, depth + 1);
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

/* ── main ────────────────────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: vix run <filename.vix>\n");
        return 1;
    }
    if (strcmp(argv[1], "run") != 0) {
        printf("Unknown command: %s (Did you mean 'run'?)\n", argv[1]);
        return 1;
    }

    const char *filename = argv[2];
    char *source = read_file_to_string(filename);
    if (!source) return 1;

    /* ── .pack sync ─────────────────────────────────────────────────────── */
    uint64_t src_hash = pack_hash_source(source, strlen(source));

    Config cfg = config_parse_upwards(filename); // finds config.toml walking up from filename

    // replace bin_max with max:
    PACK_CHUNK_SIZE = cfg.footprint.max != 0 && cfg.footprint.max != UINT64_MAX ? (size_t)cfg.footprint.max : (64 * 1024 * 1024);
    FP_LINK_OVERRIDE = cfg.footprint.link; // link stting

    const char *project_name = (cfg.valid && cfg.info.name) ? cfg.info.name        : "my_project";
    const char *footprint_link = (cfg.valid && cfg.footprint.link) ? cfg.footprint.link   : NULL;
    uint32_t owns = cfg.valid ? config_pack_own_flags(&cfg) : 0;

    ProjectPack proj = project_pack_sync_from_config(filename, src_hash, &cfg);


    /* ── end .pack sync ─────────────────────────────────────────────────── */

    FileManager files = file_manager_new();
    FileId file_id = file_manager_add(&files, filename, source);

    CheckerErrList errors = {0};
    parser_set_error_list(&errors);

    Parser parser = parser_new(lexer_new(file_id, files.slots.data[file_id].source));
    StmtsArr program = {0};

    while (parser_current(&parser).tag != EOFs) {
        size_t pos_before = (size_t)(parser_current(&parser).range.start - source);
        Stmts s = parser_stmt(&parser);
        size_t pos_after  = (size_t)(parser_current(&parser).range.start - source);

        if (s.tag != 0) ARR_PUSH(program, s);
        if (pos_after == pos_before) parser_advance(&parser);
    }

    ARR_PUSH(parser.lexer.line_starts, source + strlen(source) + 1);
    file_manager_set_line_starts(&files, file_id, parser.lexer.line_starts);

    FileTable table = {0};
    table.file_count = files.slots.len;
    table.filenames = calloc(table.file_count, sizeof(*table.filenames));
    table.sources = calloc(table.file_count, sizeof(*table.sources));
    table.line_starts = calloc(table.file_count, sizeof(*table.line_starts));

    for (size_t i = 0; i < table.file_count; i++) {
        table.filenames[i]  = files.slots.data[i].path;
        table.sources[i]    = files.slots.data[i].source;
        table.line_starts[i] = files.slots.data[i].line_starts;
    }
    checker_set_file_table(table);

    printf("\n=== AST ===\n");
    for (size_t i = 0; i < program.len; i++)
        print_statement(program.data[i], 0);

    IDCounter counter = { .next_id = 1 };
    Register global_reg  = register_new(NULL, &counter);

    FuncBodyList bodies = register_body(program.data, program.len, &global_reg, &errors);

    // add before pack_write_register call:
    RegisterEntry* dbg = register_get(&global_reg, (StringView){"main", 4});
    fprintf(stderr, "[debug] main entry: %p, child_reg: %p\n",
        (void*)dbg,
        dbg ? (void*)dbg->data.function.child_reg : NULL);
    pack_write_register(&global_reg, &errors, &parser.lexer.line_starts, source, strlen(source), filename, project_name);
    register_free(&global_reg);
    func_body_list_free(&bodies);
    project_pack_free(&proj);

    free(errors.errors);
    free((void*)table.filenames);
    free((void*)table.sources);
    free(table.line_starts);
    file_manager_free(&files);
    free(source);
    config_free(&cfg);

    return errors.count > 0 ? 1 : 0;
}

// This file is temp and used only for testing.