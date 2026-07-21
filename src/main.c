
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
#include "dir.h"
#include "debugger.h"

#define COMPILER_VERSION "0.1.0-alpha"


#include <llvm-c/Analysis.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Target.h>



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
    for (size_t i = 0; i < program.len; i++) print_statement(program.data[i], 0);

    printf("Printer is done!");
    IDCounter counter = { .next_id = 1 };
    global_registry_init();


    Register global_reg = register_new(NULL, &counter);
    global_reg_ptr = &global_reg;
    FuncBodyList bodies = register_body(program.data, program.len, &global_reg, &errors);

    GenericTables g = generic_new();
    check_generic(&global_reg, &g);
    generic_free(&g);

    printf("\n=== REGISTER ===\n");
    REG_FOREACH(&global_reg, entry, {
        print_register_entry(&global_reg, entry, 0);
    });

    IR_Module ir_mod = lower_stmt(&global_reg, (IR_Module){ .name = (char*)filename }, NULL);

    printf("\n=== IR MODULE ===\n");
    ir_print_module(&ir_mod);
    
    codegen_new(filename, source);
    codegen_def(&global_reg, ir_mod);

    char *verify_err = NULL;
    bool is_good = LLVMVerifyModule(llvm_mod, LLVMReturnStatusAction, &verify_err);
    if (is_good) {
        if (verify_err) {
            fprintf(stderr, "Verifier Message:\n%s\n", verify_err);
            LLVMDisposeMessage(verify_err);
        }

        char *file_err = NULL;
        if (LLVMPrintModuleToFile(llvm_mod, "main.ll", &file_err)) {
            fprintf(stderr, "[codegen] Could not dump main.ll: %s\n", file_err);
            LLVMDisposeMessage(file_err);
        } else {
            fprintf(stderr, "[codegen] Broken LLVM IR saved to 'main.ll'\n");
        }

        fprintf(stderr, "\n--- Per-Function Diagnostic ---\n");
        for (LLVMValueRef fn = LLVMGetFirstFunction(llvm_mod); fn != NULL; fn = LLVMGetNextFunction(fn)) {
            if (LLVMVerifyFunction(fn, LLVMReturnStatusAction)) {
                size_t name_len = 0;
                const char *fn_name = LLVMGetValueName2(fn, &name_len);
                fprintf(stderr, "[!] Broken function detected: '@%.*s'\n", (int)name_len, fn_name);
            }
        }

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
        fprintf(stderr, "\n[codegen] Failed to get target: %s\n", target_err);
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
    if (dot && strcmp(dot, ".vix") == 0) *dot = '\0';

    char obj_path[1024];
    snprintf(obj_path, sizeof(obj_path), "%s.o", base_name);

    char bin_path[1024];
    snprintf(bin_path, sizeof(bin_path), "%s.exe", base_name);

    char *emit_err = NULL;
    if (LLVMTargetMachineEmitToFile(target_machine, llvm_mod, obj_path, LLVMObjectFile, &emit_err)) {
        fprintf(stderr, "\n[codegen] Failed to emit object file: %s\n", emit_err);
        LLVMDisposeMessage(emit_err);
        return 1;
    }

    char link_cmd[2048];
    snprintf(link_cmd, sizeof(link_cmd),
        "clang \"%s\" -o \"%s\" -fuse-ld=lld -Wl,/GUARD:NO -lkernel32 -luser32 -lmsvcrt > NUL 2>&1",
        obj_path, bin_path);
    int link_status = system(link_cmd);
    if (link_status != 0) {
        fprintf(stderr, "\n[codegen] Link step failed (exit code %d)\n", link_status);
        return 1;
    }

    printf("[Success] Compilion is done!\n");
    printf("[Error] This is a alpha unverfied version. Please get cli.\n");
    LLVMDisposeTargetMachine(target_machine);
    LLVMDisposeMessage(target_triple);

    register_free(&global_reg);
    func_body_list_free(&bodies);
    ARR_FREE(ir_mod.defs);  
    file_manager_free(&files);
    free(source);

    return 0;
}