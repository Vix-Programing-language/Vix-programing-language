#ifndef VIX_IR_H
#define VIX_IR_H

#include "import.h"
#include "register.h"

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
    EntityID eid;
} IR_Param;

typedef struct {
    SourceRange name;
    Type ty;
} IR_FieldDef;

typedef struct {
    SourceRange  name;
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
    EntityID eid;
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
    IR_Expr_UnaryOp,
    IR_Expr_Call,
    IR_Expr_Idx,
    IR_Expr_Array,
    IR_Expr_MethodCall,
    IR_Expr_MakeStruct,
    IR_Expr_MakeClass,
    IR_Expr_MakeEnum,
    IR_Expr_Field,
    IR_Expr_Index,
    IR_Expr_Cast,
    IR_Expr_Deref,
    IR_Expr_MakeTuple,
    IR_Expr_AddrOf,
    IR_Expr_TupleIndex,
} IR_ExprTag;

struct IR_Expr {
    IR_ExprTag  tag;
    Type        ty;
    SourceRange origin;
    union {
        struct { IR_LiteralData data; } literal;
        struct { SourceRange name; EntityID eid; } var_ref;
        struct { LexerTokenTag op; IR_Expr *lhs; IR_Expr *rhs; } bin;
        struct { LexerTokenTag op; IR_Expr *operand; } unary;
        struct { SourceRange name; EntityID eid; IR_Expr **args; size_t args_count; } call;
        struct { IR_Expr *object; SourceRange method; EntityID method_eid; IR_Expr **args; size_t args_count; } method_call;
        struct { SourceRange name; EntityID eid; IR_FieldInit *fields; size_t fields_count; } make_struct;
        struct { SourceRange name; EntityID eid; IR_Expr **args; size_t args_count; } make_class;
        struct { SourceRange type_name; SourceRange variant; EntityID eid; IR_Expr **args; size_t args_count; } make_enum;

        struct { IR_Expr *object; IR_Expr *index; } index;
        struct { IR_Expr *expr; } cast;
        struct { IR_Expr *ptr; } deref;
        struct { IR_Expr** elems; size_t elems_count; } make_tuple;
        struct { IR_Expr*  tuple; size_t index; } tuple_index;
        struct { IR_Expr* base; IR_Expr* index; SourceRange range; } idx;
        struct { IR_Expr** elems; size_t elems_count; } array;
        struct { IR_Expr* object; SourceRange field; FieldOwnerKind kind; EntityID type_eid; } field;
        struct { IR_Expr *expr; } addr_of;
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
    IR_Stmt_AtomicOp,
} IR_StmtTag;

struct IR_Stmt {
    IR_StmtTag  tag;
    SourceRange origin;
    union {
        struct { SourceRange name; EntityID eid; Type ty; IR_Expr *init; VarMode mode; } var_decl;
        struct { SourceRange name; EntityID eid; Type ty; IR_Expr *init; VarMode mode; } let_decl;
        struct { SourceRange name; EntityID eid; Type ty; IR_Expr *init; } const_decl;
        struct { SourceRange name; EntityID eid; Type ty; } local_decl;
        struct { IR_Expr *target; LexerTokenTag op; IR_Expr *value; } assign;
        struct { IR_Expr *val; } ret;
        struct { IR_Expr *cond; Pattern guard_pattern; IR_Stmt *body; size_t body_count; IR_Stmt *else_body; size_t else_body_count; } if_;
        struct { IR_Expr *cond; IR_Stmt *body; size_t body_count; } while_;
        struct { SourceRange var; EntityID var_eid; Type var_ty; IR_Expr *iter; IR_Stmt *body; size_t body_count; } for_;
        struct { IR_Expr *expr; IR_MatchArm *arms; size_t arms_count; IR_Stmt *default_body; size_t default_body_count; } match;
        struct { IR_Expr *expr; } expr;
        struct { EntityID eid; Type ty; IR_Expr *val; } ssa_temp;
        struct { SourceRange target; EntityID target_eid; AtomicOpTag op; IR_Expr *args[3]; size_t args_count; OrderingTag ordering; OrderingTag ordering2; } atomic_op;
    } data;
};

typedef enum {
    IR_Def_Function,
    IR_Def_Struct,
    IR_Def_Enum,
    IR_Def_Class,
    IR_Def_Trait,
    IR_Def_VTable,
    IR_Def_Const,
    IR_Def_Var,
    IR_Def_Let,
    IR_Def_Extern,
} IR_DefTag;

typedef struct {
    IR_DefTag   tag;
    SourceRange origin;
    union {
        struct { IR_FuncDef def; } function;
        struct { SourceRange name; EntityID eid; IR_FieldDef *fields; size_t fields_count; bool is_pub; } struct_;
        struct { SourceRange name; EntityID eid; IR_VariantDef *variants; size_t variants_count; bool is_pub; } enum_;
        struct { SourceRange name; EntityID eid; IR_FieldDef *fields; size_t fields_count; IR_FuncDef *methods; size_t methods_count; bool is_pub; } class_;
        struct { SourceRange name; EntityID eid; IR_FuncDef *methods; size_t methods_count; bool is_pub; } trait_;
        struct { SourceRange abi; SourceRange ffi; SourceRange name; EntityID eid; Type return_type; IR_Param *params; size_t params_count; bool is_pub; } extern_;
        struct { SourceRange trait_name; SourceRange impl_type; EntityID eid; IR_FuncDef* methods; size_t methods_count; } vtable;    
        struct { SourceRange name; EntityID eid; Type ty; IR_Expr *init; } const_;
        struct { SourceRange name; EntityID eid; Type ty; IR_Expr *init; VarMode mode; } var_;
        struct { SourceRange name; EntityID eid; Type ty; IR_Expr *init; VarMode mode; } let_;
    } data;
} IR_Def;

typedef struct {
    char *name;
    ARR(IR_Def) defs;
} IR_Module;

typedef IR_Expr* IR_Expr_Ptr;

typedef struct { IR_Stmt      *data; size_t len; size_t cap; } IR_StmtArr;
typedef struct { IR_Expr_Ptr  *data; size_t len; size_t cap; } IR_ExprPtrArr;
typedef struct { IR_FieldInit *data; size_t len; size_t cap; } IR_FieldInitArr;
typedef struct { IR_MatchArm  *data; size_t len; size_t cap; } IR_MatchArmArr;
typedef struct { IR_Param     *data; size_t len; size_t cap; } IR_ParamArr;
typedef struct { IR_FuncDef   *data; size_t len; size_t cap; } IR_FuncDefArr;
typedef struct { IR_FieldDef  *data; size_t len; size_t cap; } IR_FieldDefArr;
typedef struct { IR_VariantDef*data; size_t len; size_t cap; } IR_VariantDefArr;


static inline void ir_module_push(IR_Module *mod, IR_Def def) {
    if (mod) ARR_PUSH(mod->defs, def);
}

IR_Expr *ir_expr_alloc(IR_Expr expr) {
    IR_Expr *p = (IR_Expr *)malloc(sizeof(IR_Expr));
    *p = expr;
    return p;
}


static inline IR_Expr ir_literal_null(SourceRange o) {
    IR_Expr e = {};
    e.tag            = IR_Expr_Literal;
    e.ty.tag         = Type_Ptr;
    e.ty.data.ptr.inner = NULL;
    e.origin         = o;
    return e;
}

static inline IR_Expr ir_literal_int(int64_t v, SourceRange o) {
    IR_Expr e = {};
    e.tag                        = IR_Expr_Literal;
    e.ty.tag                     = Type_Int;
    e.ty.data.int_t.bits         = 64;
    e.origin                     = o;
    e.data.literal.data.int_val  = v;
    return e;
}

static inline IR_Expr ir_literal_float(double v, SourceRange o) {
    IR_Expr e = {};
    e.tag                          = IR_Expr_Literal;
    e.ty.tag                       = Type_Float;
    e.ty.data.float_t.bits         = 64;
    e.origin                       = o;
    e.data.literal.data.float_val  = v;
    return e;
}

static inline IR_Expr ir_literal_bool(bool v, SourceRange o) {
    IR_Expr e = {};
    e.tag                         = IR_Expr_Literal;
    e.ty.tag                      = Type_Bool;
    e.origin                      = o;
    e.data.literal.data.bool_val  = v;
    return e;
}

static inline IR_Expr ir_literal_char(char v, SourceRange o) {
    IR_Expr e = {};
    e.tag                         = IR_Expr_Literal;
    e.ty.tag                      = Type_Char;
    e.origin                      = o;
    e.data.literal.data.char_val  = v;
    return e;
}

static inline IR_Expr ir_literal_str(SourceRange o) {
    IR_Expr e = {};
    e.tag                           = IR_Expr_Literal;
    e.ty.tag                        = Type_Str;
    e.origin                        = o;
    e.data.literal.data.str_range   = o;
    return e;
}


void ir_push_stmt(IR_StmtArr *out, IR_Stmt s) { ARR_PUSH(*out, s); }

#define LOWER_EXPR(reg, src) \
    ((src) && (src)->tag ? lower_expr((reg), (src)) : (IR_Expr){0})

// Safe alloc - only allocs if src is non-null and has a tag
#define LOWER_EXPR_ALLOC(reg, src) \
    ((src) && (src)->tag ? ir_expr_alloc(lower_expr((reg), (src))) : NULL)

static inline void ir_push_if(IR_StmtArr *out, IR_Stmt s) {
    if (out) ir_push_stmt(out, s);
}

static inline void ir_mod_push_if(IR_Module *mod, IR_Def d) {
    if (mod) ir_module_push(mod, d);
}

#define IR_PUSH(out, stmt)   ir_push_if((out), (stmt))
#define IR_MOD_PUSH(mod, def) ir_mod_push_if((mod), (def))


#define LOWER_STMTS(reg, out, mod, arr, count) \
    for (size_t _i = 0; _i < (count); _i++) \
        lower_stmt((reg), (mod) ? *(mod) : (IR_Module){0})

#define LOWER_PARAMS(reg, params_arr, src, count) \
    for (size_t _i = 0; _i < (count); _i++) { \
        RegisterEntry *_pe = register_get((reg), sv_from_range((src)[_i].name)); \
        ARR_PUSH((params_arr), ((IR_Param){ \
            .name = (src)[_i].name, \
            .ty   = DEREF_TYPE((src)[_i].type_tree, (src)[_i].c_type), \
            .mode = (src)[_i].mode, \
            .eid  = _pe ? _pe->eid : (EntityID){0}, \
        })); \
    }

#define LOWER_ARGS(reg, out_ptr, out_count, src, count) \
    do { \
        IR_ExprPtrArr _arr = {0}; \
        for (size_t _i = 0; _i < (count); _i++) \
            ARR_PUSH(_arr, ir_expr_alloc(lower_expr((reg), &(src)[_i].value))); \
        (out_ptr)   = _arr.data; \
        (out_count) = _arr.len; \
    } while(0)

#define LOWER_ARGS_RAW(reg, out_ptr, out_count, src, count) \
    do { \
        IR_ExprPtrArr _arr = {0}; \
        for (size_t _i = 0; _i < (count); _i++) \
            ARR_PUSH(_arr, ir_expr_alloc(lower_expr((reg), &(src)[_i]))); \
        (out_ptr)   = _arr.data; \
        (out_count) = _arr.len; \
    } while(0)
#define LOWER_FIELDS(src, count, out_arr) \
    for (size_t _i = 0; _i < (count); _i++) { \
        ARR_PUSH((out_arr), ((IR_FieldDef){ \
            .name = (src)[_i].name, \
            .ty   = (src)[_i].type_tree ? *(src)[_i].type_tree : (Type){0}, \
        })); \
    }

IR_Module lower_stmt(Register *reg, IR_Module mod, IR_StmtArr *stmts_out);
IR_Def lower_function(Register *reg, uint32_t id);

Type type_from_range(SourceRange r);

#endif /* VIX_IR_H */
