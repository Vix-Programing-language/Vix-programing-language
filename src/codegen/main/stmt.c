#include "import.h"
#include "ast.h"
#include "register.h"
#include "ir.h"
#include "codegen.h"


#include <llvm-c/Core.h>

void codegen_generate_function(Register *reg, uint32_t *param_ids, Codegen_FuncDef fn) {
    LLVMTypeRef func_type = LLVMFunctionType(fn.return_type, fn.params, fn.params_count, 0);
    LLVMValueRef llvm_func = LLVMAddFunction(llvm_mod, fn.name, func_type);
    LLVMBasicBlockRef entry_block = LLVMAppendBasicBlock(llvm_func, "entry");

    LLVMPositionBuilderAtEnd(llvm_builder, entry_block);

    for (size_t i = 0; i < fn.params_count; i++) {
        LLVMValueRef llvm_param = LLVMGetParam(llvm_func, (unsigned int)i);
        LLVMSetValueName2(llvm_param, fn.param_names[i], strlen(fn.param_names[i]));

        LLVMValueRef alloca = LLVMBuildAlloca(llvm_builder, fn.params[i], fn.param_names[i]);
        LLVMBuildStore(llvm_builder, llvm_param, alloca);

        if (param_ids[i] != 0) symbol_table_set(param_ids[i], alloca);
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

    if (v.init) LLVMBuildStore(llvm_builder, v.init, alloca);

    symbol_table_set(id, alloca);
    free(v.name);
}

void codegen_generate_let(uint32_t id, Codegen_Let l) {
    LLVMValueRef alloca = LLVMBuildAlloca(llvm_builder, l.type, l.name);
    if (l.init) LLVMBuildStore(llvm_builder, l.init, alloca);
    symbol_table_set(id, alloca);
    free(l.name);
}

void codegen_generate_const(uint32_t id, Codegen_Const c) {
    LLVMValueRef alloca = LLVMBuildAlloca(llvm_builder, c.type, c.name);
    if (c.init) LLVMBuildStore(llvm_builder, c.init, alloca);
    symbol_table_set(id, alloca);
    free(c.name);
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
    LLVMTypeRef body[] = { LLVMInt32Type() };
    LLVMTypeRef enum_type = LLVMStructCreateNamed(llvm_ctx, e.name);
    LLVMStructSetBody(enum_type, body, 1, 0);
    free(e.name);
}

void codegen_stmts(Register *reg, IR_Stmt *stmts, size_t count);

void codegen_generate_if(Register *reg, LLVMValueRef cond, struct IR_Stmt *body, size_t body_count, struct IR_Stmt *else_body, size_t else_count) {
    LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(llvm_builder));

    LLVMBasicBlockRef then_block = LLVMAppendBasicBlock(current_func, "if.then");
    LLVMBasicBlockRef else_block = LLVMAppendBasicBlock(current_func, "if.else");
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(current_func, "if.merge");

    LLVMBuildCondBr(llvm_builder, cond, then_block, else_block);
    LLVMPositionBuilderAtEnd(llvm_builder, then_block);

    if (body_count > 0) {
        codegen_stmts(reg, body, body_count);
    }

    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(llvm_builder))) {
        LLVMBuildBr(llvm_builder, merge_block);
    }

    LLVMPositionBuilderAtEnd(llvm_builder, else_block);
    if (else_count > 0) {
        codegen_stmts(reg, else_body, else_count);
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(llvm_builder))) {
        LLVMBuildBr(llvm_builder, merge_block);
    }

    LLVMPositionBuilderAtEnd(llvm_builder, merge_block);
}

void codegen_generate_while(Register *reg, struct IR_Expr *cond_expr, struct IR_Stmt *body, size_t body_count) {
    LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(llvm_builder));

    LLVMBasicBlockRef cond_block = LLVMAppendBasicBlock(current_func, "while.cond");
    LLVMBasicBlockRef body_block = LLVMAppendBasicBlock(current_func, "while.body");
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(current_func, "while.merge");

    LLVMBuildBr(llvm_builder, cond_block);

    LLVMPositionBuilderAtEnd(llvm_builder, cond_block);
    LLVMValueRef cond = codegen_expr(reg, cond_expr);
    LLVMBuildCondBr(llvm_builder, cond, body_block, merge_block);

    LLVMPositionBuilderAtEnd(llvm_builder, body_block);
    if (body_count > 0) {
        codegen_stmts(reg, body, body_count);
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(llvm_builder))) {
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