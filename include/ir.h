#ifndef VIX_IR_H
#define VIX_IR_H

#include "import.h"
#include "ast.h"

typedef struct IR_Expr IR_Expr;
typedef struct IR_Stmt IR_Stmt;

typedef union {
    int64_t int_val;
    double float_val;
    bool bool_val;
    char char_val;
    SourceRange str_range;
} IR_LiteralData;

typedef struct {
    SourceRange name;
    IR_Expr *val;
} IR_FieldInit;

typedef struct {
    SourceRange name;
    Type ty;
    VarMode mode;
} IR_Param;

typedef struct {
    SourceRange name;
    Type ty;
} IR_FieldDef;

typedef struct {
    SourceRange name;
    IR_FieldDef *fields;
    size_t fields_count;
} IR_VariantDef;

typedef enum {
    CC_Default,
    CC_C,
    CC_Fast,
    CC_Cold, 
} CallingConv;

typedef struct {
    SourceRange name;
    Type return_type;
    IR_Param *params;
    size_t params_count;
    IR_Stmt *body;
    size_t body_count;
    bool is_pub;
    bool is_unsafe;
    LexerTokenTag operation_op;
    CallingConv   cc;
} IR_FuncDef;

typedef struct {
    Pattern  pattern;
    IR_Stmt *body;
    size_t   body_count;
} IR_MatchArm;

typedef enum {
    IR_Expr_Literal,
    IR_Expr_VarRef,
    IR_Expr_BinOp,
    IR_Expr_Call,
    IR_Expr_MethodCall,
    IR_Expr_MakeStruct,
    IR_Expr_MakeClass,
    IR_Expr_MakeEnum,
    IR_Expr_Field,
    IR_Expr_Index,
    IR_Expr_Cast,
    IR_Expr_Deref,
} IR_ExprTag;

struct IR_Expr {
    IR_ExprTag  tag;
    Type        ty;
    SourceRange origin;
    union {
        struct { IR_LiteralData data; } literal;
        struct { SourceRange name; uint32_t eid; } var_ref;
        struct { LexerTokenTag op; IR_Expr *lhs; IR_Expr *rhs; } bin;
        struct { SourceRange name; uint32_t eid; IR_Expr **args; size_t args_count; } call;
        struct { IR_Expr *object; SourceRange method; uint32_t method_eid; IR_Expr **args; size_t args_count; } method_call;
        struct { SourceRange name; uint32_t eid; IR_FieldInit *fields; size_t fields_count; } make_struct;
        struct { SourceRange name; uint32_t eid; IR_Expr **args; size_t args_count; } make_class;
        struct { SourceRange type_name; SourceRange variant; uint32_t eid; IR_Expr **args; size_t args_count; } make_enum;
        struct { IR_Expr *object; SourceRange field; } field;
        struct { IR_Expr *object; IR_Expr *index; } index;
        struct { IR_Expr *expr; } cast;
        struct { IR_Expr *ptr; } deref;
    } data;
};

typedef enum {
    IR_Stmt_VarDecl,
    IR_Stmt_LetDecl,
    IR_Stmt_ConstDecl,
    IR_Stmt_LocalDecl,
    IR_Stmt_Assign,
    IR_Stmt_Return,
    IR_Stmt_If,
    IR_Stmt_While,
    IR_Stmt_For,
    IR_Stmt_Match,
    IR_Stmt_Expr,
    IR_Stmt_SsaTemp,
    IR_Def_Extern,
} IR_StmtTag;

struct IR_Stmt {
    IR_StmtTag  tag;
    SourceRange origin;
    union {
        struct { SourceRange name; uint32_t eid; Type ty; IR_Expr *init; VarMode mode; } var_decl;
        struct { SourceRange name; uint32_t eid; Type ty; IR_Expr *init; VarMode mode; } let_decl;
        struct { SourceRange name; uint32_t eid; Type ty; IR_Expr *init; } const_decl;
        struct { SourceRange name; uint32_t eid; Type ty; } local_decl;
        struct { IR_Expr *target; LexerTokenTag op; IR_Expr *value; } assign;
        struct { IR_Expr *val; } ret;
        struct { IR_Expr *cond; Pattern guard_pattern; IR_Stmt *body; size_t body_count; IR_Stmt *else_body;  size_t else_body_count; } if_;
        struct { IR_Expr *cond; IR_Stmt *body; size_t body_count; } while_;
        struct { SourceRange var; uint32_t var_eid; Type var_ty; IR_Expr *iter; IR_Stmt *body; size_t body_count; } for_;
        struct { IR_Expr *expr; IR_MatchArm *arms; size_t arms_count; IR_Stmt *default_body; size_t default_body_count; } match;
        struct { IR_Expr *expr; } expr;
        struct { uint32_t eid; Type ty; IR_Expr *val; } ssa_temp;
    } data;
};

typedef enum {
    IR_Def_Function,
    IR_Def_Struct,
    IR_Def_Enum,
    IR_Def_Class,
    IR_Def_Trait,
} IR_DefTag;

typedef struct {
    IR_DefTag   tag;
    SourceRange origin;
    union {
        struct { IR_FuncDef def; } function;
        struct { SourceRange name; uint32_t eid; IR_FieldDef *fields; size_t fields_count; bool is_pub; } struct_;
        struct { SourceRange name; uint32_t eid; IR_VariantDef *variants; size_t variants_count; bool is_pub; } enum_;
        struct { SourceRange name; uint32_t eid; IR_FieldDef *fields; size_t fields_count; IR_FuncDef  *methods; size_t methods_count;                 bool is_pub; } class_;
        struct { SourceRange name; uint32_t eid; IR_FuncDef *methods; size_t methods_count; bool is_pub; } trait_;
        struct { SourceRange  abi; SourceRange  ffi; SourceRange  name; uint32_t eid; Type return_type; IR_Param *params; size_t       params_count; bool         is_pub; } extern_;
    } data;
} IR_Def;
     
typedef struct {
    char    *name;
    IR_Def  *defs;
    size_t   defs_count;
    size_t   defs_cap;
} IR_Module;

typedef IR_Expr* IR_Expr_Ptr;

typedef struct { IR_Stmt*       data; size_t len; size_t cap; } IR_StmtArr;
typedef struct { IR_Expr_Ptr*   data; size_t len; size_t cap; } IR_ExprPtrArr;
typedef struct { IR_FieldInit*  data; size_t len; size_t cap; } IR_FieldInitArr;
typedef struct { IR_MatchArm*   data; size_t len; size_t cap; } IR_MatchArmArr;
typedef struct { IR_Param*      data; size_t len; size_t cap; } IR_ParamArr;
typedef struct { IR_FuncDef*    data; size_t len; size_t cap; } IR_FuncDefArr;
typedef struct { IR_FieldDef*   data; size_t len; size_t cap; } IR_FieldDefArr;
typedef struct { IR_VariantDef* data; size_t len; size_t cap; } IR_VariantDefArr;

static inline void ir_module_push(IR_Module *mod, IR_Def def) {
    if (mod->defs_count == mod->defs_cap) {
        mod->defs_cap = mod->defs_cap ? mod->defs_cap * 2 : 16;
        mod->defs = realloc(mod->defs, mod->defs_cap * sizeof(IR_Def));
    }
    mod->defs[mod->defs_count++] = def;
}

static inline IR_Expr *ir_expr_alloc(IR_Expr expr) {
    IR_Expr *p = malloc(sizeof(IR_Expr));
    *p = expr;
    return p;
}

static inline IR_Expr ir_literal_int(int64_t v, SourceRange o) { return (IR_Expr){ .tag = IR_Expr_Literal, .ty = { .tag = Type_Int,   .data.int_t.bits   = 64 }, .origin = o, .data.literal.data.int_val   = v }; }
static inline IR_Expr ir_literal_float(double v, SourceRange o) { return (IR_Expr){ .tag = IR_Expr_Literal, .ty = { .tag = Type_Float, .data.float_t.bits = 64 }, .origin = o, .data.literal.data.float_val = v }; }
static inline IR_Expr ir_literal_bool(bool v, SourceRange o) { return (IR_Expr){ .tag = IR_Expr_Literal, .ty = { .tag = Type_Bool                          }, .origin = o, .data.literal.data.bool_val  = v }; }
static inline IR_Expr ir_literal_char(char v, SourceRange o) { return (IR_Expr){ .tag = IR_Expr_Literal, .ty = { .tag = Type_Char                          }, .origin = o, .data.literal.data.char_val  = v }; }
static inline IR_Expr ir_literal_str(SourceRange o) { return (IR_Expr){ .tag = IR_Expr_Literal, .ty = { .tag = Type_Str                           }, .origin = o, .data.literal.data.str_range  = o }; }

#endif
