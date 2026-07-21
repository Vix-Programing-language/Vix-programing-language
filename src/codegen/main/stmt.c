#include "import.h"
#include "ast.h"
#include "register.h"
#include "ir.h"
#include "codegen.h"


#include <llvm-c/Core.h>


void codegen_predeclare_function(Codegen_FuncDef fn) {
    LLVMTypeRef func_type = LLVMFunctionType(fn.return_type, fn.params, fn.params_count, 0);
    LLVMAddFunction(llvm_mod, fn.name, func_type);
}
void codegen_generate_function(Register *reg, uint32_t *param_ids, Codegen_FuncDef fn) {
    LLVMTypeRef func_type = LLVMFunctionType(fn.return_type, fn.params, fn.params_count, 0);
    LLVMValueRef llvm_func = LLVMGetNamedFunction(llvm_mod, fn.name);
    if (!llvm_func) {
        llvm_func = LLVMAddFunction(llvm_mod, fn.name, func_type);
    }

    LLVMBasicBlockRef entry_block = LLVMAppendBasicBlock(llvm_func, "entry");
    LLVMPositionBuilderAtEnd(llvm_builder, entry_block);

    for (size_t i = 0; i < fn.params_count; i++) {
        LLVMValueRef llvm_param = LLVMGetParam(llvm_func, (unsigned int)i);
        LLVMSetValueName2(llvm_param, fn.param_names[i], strlen(fn.param_names[i]));
        LLVMValueRef alloca = LLVMBuildAlloca(llvm_builder, fn.params[i], fn.param_names[i]);
        LLVMBuildStore(llvm_builder, llvm_param, alloca);

        if (param_ids[i] != 0) {
            symbol_table_set(param_ids[i], alloca);
        }
    }
}


void codegen_generate_struct(Codegen_Struct s) {
    LLVMTypeRef struct_type = LLVMStructCreateNamed(llvm_ctx, s.name);
    LLVMStructSetBody(struct_type, s.fields, s.fields_count, 0);
    free(s.name);
    free(s.fields);
}


void codegen_generate_var(uint32_t id, Codegen_Var v) {
    LLVMValueRef alloca = LLVMBuildAlloca(llvm_builder, v.type, v.name);

    if (v.init) {
        LLVMBuildStore(llvm_builder, v.init, alloca);
    }

    symbol_table_set(id, alloca);
    free(v.name);
}


void codegen_generate_let(uint32_t id, Codegen_Let l) {
    LLVMValueRef alloca = LLVMBuildAlloca(llvm_builder, l.type, l.name);
    if (l.init) LLVMBuildStore(llvm_builder, l.init, alloca);
    symbol_table_set(id, alloca);
    free(l.name);
}

void codegen_generate_const(uint32_t id, Codegen_Const c, bool globle) {
    if (globle == true) {
        LLVMValueRef var = LLVMAddGlobal(llvm_mod, c.type, c.name);
        if (c.init) LLVMSetInitializer(var, c.init);
        LLVMSetGlobalConstant(var, 1);
        LLVMSetLinkage(var, LLVMInternalLinkage);
        LLVMSetUnnamedAddress(var, LLVMGlobalUnnamedAddr);
        symbol_table_set(id, var);
        free(c.name);
    } else {
        LLVMValueRef alloca = LLVMBuildAlloca(llvm_builder, c.type, c.name);
        if (c.init) LLVMBuildStore(llvm_builder, c.init, alloca);
        symbol_table_set(id, alloca);
        free(c.name);
    }
}

void codegen_generate_extern(Codegen_Extern e) {
    LLVMTypeRef fn_type = LLVMFunctionType(e.return_type, e.params, e.params_count, 0);
    LLVMValueRef fn = LLVMAddFunction(llvm_mod, e.name, fn_type);
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    free(e.name);
    free(e.params); 
    free(e.param_names);
}


void codegen_generate_enum(Codegen_Enum e) {
    size_t total = 1 + e.payload_count;
    ARR(LLVMTypeRef) body = {0};
    ARR_PUSH(body, LLVMInt32TypeInContext(llvm_ctx));

    for (size_t i = 0; i < e.payload_count; i++) ARR_PUSH(body, e.payload_types[i]);

    LLVMTypeRef enum_type = LLVMGetTypeByName2(llvm_ctx, e.name);

    if (enum_type) {
        ARR_FREE(body);
        free(e.name);
        return;
    }

    enum_type = LLVMStructCreateNamed(llvm_ctx, e.name);
    LLVMStructSetBody(enum_type, body.data, (unsigned)body.len, 0);

    ARR_FREE(body);
    free(e.name);
}


void codegen_generate_if(Register *reg, LLVMValueRef cond, struct IR_Stmt *body, size_t body_count,struct IR_Stmt *else_body, size_t else_count) {
    LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(llvm_builder));

    LLVMBasicBlockRef then_block  = LLVMAppendBasicBlock(current_func, "if.then");
    LLVMBasicBlockRef else_block  = LLVMAppendBasicBlock(current_func, "if.else");
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(current_func, "if.merge");

    LLVMBuildCondBr(llvm_builder, cond, then_block, else_block);
    LLVMPositionBuilderAtEnd(llvm_builder, then_block);

    if (body_count > 0) codegen_stmts(reg, body, body_count);

    bool then_has_term = LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(llvm_builder)) != NULL;
    if (!then_has_term) LLVMBuildBr(llvm_builder, merge_block);
    LLVMPositionBuilderAtEnd(llvm_builder, else_block);
    if (else_count > 0) codegen_stmts(reg, else_body, else_count);
    bool else_has_term = LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(llvm_builder)) != NULL;
    if (!else_has_term) LLVMBuildBr(llvm_builder, merge_block);

    LLVMPositionBuilderAtEnd(llvm_builder, merge_block);

    if (then_has_term && else_has_term) {
        LLVMBuildUnreachable(llvm_builder);
    }
}

void codegen_generate_while(Register *reg, struct IR_Expr *cond_expr, struct IR_Stmt *body, size_t body_count) {
    if (!llvm_builder) {
        fprintf(stderr, "[DEBUG WHILE ERROR] llvm_builder is NULL!\n");
        return;
    }

    LLVMBasicBlockRef current_bb = LLVMGetInsertBlock(llvm_builder);
    if (!current_bb) {
        fprintf(stderr, "[DEBUG WHILE ERROR] Builder has no current basic block set!\n");
        return;
    }

    LLVMValueRef current_func = LLVMGetBasicBlockParent(current_bb);
    if (!current_func) {
        fprintf(stderr, "[DEBUG WHILE ERROR] Basic block has no parent function!\n");
        return;
    }

    LLVMBasicBlockRef cond_block = LLVMAppendBasicBlock(current_func, "while.cond");
    LLVMBasicBlockRef body_block = LLVMAppendBasicBlock(current_func, "while.body");
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(current_func, "while.merge");
    LLVMBuildBr(llvm_builder, cond_block);
    LLVMPositionBuilderAtEnd(llvm_builder, cond_block);

    if (!cond_expr) {
        fprintf(stderr, "[DEBUG WHILE ERROR] cond_expr is NULL!\n");
        return;
    }

    LLVMValueRef cond = codegen_expr(reg, cond_expr);

    if (!cond) {
        fprintf(stderr, "[DEBUG WHILE ERROR] codegen_expr returned NULL for condition!\n");
        return;
    }

    LLVMBuildCondBr(llvm_builder, cond, body_block, merge_block);
    LLVMPositionBuilderAtEnd(llvm_builder, body_block);

    if (body_count > 0) {
        if (!body) {
            fprintf(stderr, "[DEBUG WHILE ERROR] body_count > 0 (%zu) but body pointer is NULL!\n", body_count);
            return;
        }
        codegen_stmts(reg, body, body_count);
    }

    LLVMBasicBlockRef body_end_bb = LLVMGetInsertBlock(llvm_builder);

    if (!LLVMGetBasicBlockTerminator(body_end_bb)) {
        LLVMBuildBr(llvm_builder, cond_block);
    }

    LLVMPositionBuilderAtEnd(llvm_builder, merge_block);
}

void codegen_generate_for(Register *reg, struct IR_Expr *iter_expr, struct IR_Stmt *body, size_t body_count) {
    LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(llvm_builder));

    LLVMBasicBlockRef cond_block = LLVMAppendBasicBlock(current_func, "for.cond");
    LLVMBasicBlockRef body_block = LLVMAppendBasicBlock(current_func, "for.body");
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(current_func, "for.merge");

    LLVMBuildBr(llvm_builder, cond_block);

    LLVMPositionBuilderAtEnd(llvm_builder, cond_block);
    LLVMValueRef has_next = codegen_expr(reg, iter_expr);
    LLVMBuildCondBr(llvm_builder, has_next, body_block, merge_block);

    LLVMPositionBuilderAtEnd(llvm_builder, body_block);
    if (body_count > 0) {
        codegen_stmts(reg, body, body_count);
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(llvm_builder))) {
        LLVMBuildBr(llvm_builder, cond_block); 
    }

    LLVMPositionBuilderAtEnd(llvm_builder, merge_block);
}

void codegen_generate_match(Register *reg, LLVMValueRef match_val, IR_MatchArm *arms, size_t arms_count, struct IR_Stmt *default_body, size_t default_body_count) {
    LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(llvm_builder));
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(current_func, "match.merge");
    LLVMBasicBlockRef default_block = LLVMAppendBasicBlock(current_func, "match.default");
    LLVMValueRef loaded_match_val = match_val;
    LLVMTypeRef val_type = LLVMTypeOf(loaded_match_val);
    LLVMTypeKind current_kind = LLVMGetTypeKind(val_type);

    int pointer_layers = 0;

    while (current_kind == LLVMPointerTypeKind) {
        pointer_layers++;
        LLVMTypeRef element_type = LLVMGetElementType(val_type);

        if (LLVMGetTypeKind(element_type) == LLVMFunctionTypeKind) {
            break;
        }

        char load_debug_name[64];
        snprintf(load_debug_name, sizeof(load_debug_name), "match.deref_layer_%d", pointer_layers);
        
        loaded_match_val = LLVMBuildLoad2(llvm_builder, element_type, loaded_match_val, load_debug_name);
        val_type = LLVMTypeOf(loaded_match_val);
        current_kind = LLVMGetTypeKind(val_type);
    }

    LLVMValueRef switch_target = loaded_match_val;

    if (current_kind == LLVMStructTypeKind) {
        switch_target = LLVMBuildExtractValue(llvm_builder, loaded_match_val, 0, "match.tag_discriminant");
    }

    LLVMValueRef switch_instr = LLVMBuildSwitch(llvm_builder, switch_target, default_block, (unsigned int)arms_count);

    ARR(LLVMBasicBlockRef) arm_blocks = {0};

    for (size_t i = 0; i < arms_count; i++) {
        char block_name[32];
        snprintf(block_name, sizeof(block_name), "match.case.%zu", i);
        
        LLVMBasicBlockRef block = LLVMAppendBasicBlock(current_func, block_name);
        ARR_PUSH(arm_blocks, block);

        LLVMTypeRef target_type = LLVMTypeOf(switch_target);
        if (LLVMGetTypeKind(target_type) != LLVMIntegerTypeKind) {
            target_type = LLVMInt32Type();
        }

        LLVMValueRef case_const = LLVMConstInt(target_type, (unsigned long long)i, 0);
        LLVMAddCase(switch_instr, case_const, block);
    }


    for (size_t i = 0; i < arms_count; i++) {
        LLVMPositionBuilderAtEnd(llvm_builder, ARR_AT(arm_blocks, i));

        if (current_kind == LLVMStructTypeKind) {
            unsigned int num_elements = LLVMCountStructElementTypes(val_type);
            if (num_elements > 1) {
                LLVMValueRef bound_var_val = LLVMBuildExtractValue(llvm_builder, loaded_match_val, 1, "match.payload");
                LLVMValueRef local_i_alloc = LLVMBuildAlloca(llvm_builder, LLVMTypeOf(bound_var_val), "i");
                LLVMBuildStore(llvm_builder, bound_var_val, local_i_alloc);
            }
        }

        if (arms[i].body_count > 0) {
            codegen_stmts(reg, arms[i].body, arms[i].body_count);
        }

        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(llvm_builder))) {
            LLVMBuildBr(llvm_builder, merge_block);
        }
    }


    LLVMPositionBuilderAtEnd(llvm_builder, default_block);
    if (default_body_count > 0 && default_body != NULL) {
        codegen_stmts(reg, default_body, default_body_count);
    }
    
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(llvm_builder))) {
        LLVMBuildBr(llvm_builder, merge_block);
    }
    ARR_FREE(arm_blocks);

    LLVMPositionBuilderAtEnd(llvm_builder, merge_block);
}