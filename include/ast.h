#ifndef VIX_TOKEN_AST_H
#define VIX_TOKEN_AST_H

#include "import.h"

typedef struct Stmts Stmts;
typedef struct Type Type;
typedef struct Exprs Exprs;

typedef enum {
    Plus = 1,
    Minuss,
    Stars,
    Slashs,
    Percents,
    Lesses,
    Greaters,
    NEqs,
    Equalss,
    Ampersands,
    Lambdas,
    Externs,
    Pipes,
    Carets,
    Tildes,
    Bangs,
    LeftParens,
    RightParens,
    LeftBrackets,
    RightBrackets,
    LeftBraces,
    RightBraces,
    Dots,
    DoubleDots,
    TrippleDots,
    Commas,
    Colons,
    Semicolons,
    Ats,
    Hashs,
    Questions,
    Backslashs,
    SingleQuotes,
    DoubleQuotes,
    Backticks,
    Underscores,
    Returns,
    Functions,
    Classes,
    Traits,
    Ifs,
    Elifs,
    Elses,
    Cases,
    Defaults,
    Matchs,
    Types,
    Whiles,
    Thens,
    Ass,
    Dos,
    Fors,
    Selfs,
    Continues,
    Unsafes,
    Breaks,
    Publics,
    Refs,
    Muts,
    Borrows,
    EOFs,
    Ors,
    Ands,
    PlusEqualss,
    MinusEqualss,
    StarEqualss,
    SlashEqualss,
    PercentEqualss,
    PipeEqualss,
    AmpersandEqualss,
    CaretEqualss,
    LeftShiftEqualss,
    LeftShifts,
    LessEqualss,
    RightShiftEqualss,
    RightShifts,
    GreaterEqualss,
    NotEqualss,
    DoubleEqualss,
    Structs,
    Ends,
    Strings,
    Ints,
    Floats,
    Chars,
    Trues,
    Iss,
    Falses,
    Vars,
    Lets,
    Consts,
    Locals,
    Ins,
    Nots,
    Enums,
    Identifier,
} LexerTokenTag;

typedef struct {
    const char** data;
    size_t len;
    size_t cap;
} LineStarts;

static inline size_t get_line_num(const LineStarts* starts, uintptr_t tgt) {
    if (!starts || !starts->data || starts->len < 2) {
        return (size_t)-1;
    }

    uintptr_t first = (uintptr_t)starts->data[0];
    uintptr_t end   = (uintptr_t)starts->data[starts->len - 1];

    if (tgt < first || tgt >= end) {
        return (size_t)-1;
    }

    size_t lo = 0;
    size_t hi = starts->len - 1;

    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        uintptr_t mid_start = (uintptr_t)starts->data[mid];

        if (tgt < mid_start) {
            hi = mid;
        } else {
            lo = mid;
        }
    }

    return lo;
}


typedef struct {
    const char* start;
    const char* end;
    size_t file_id;
} SourceRange;

typedef struct {
    SourceRange first;
    SourceRange second;
} EnumField;

typedef union {
    uint64_t value_int;
    float value_float;
    char value_char;
    char* s;
} LexerTokenData;

typedef struct {
    LexerTokenTag tag;
    SourceRange range;
    LexerTokenData data;
} LexerToken;

typedef struct {
    const char* source;
    const char* cur;
    size_t file_id;
    LexerToken top;
    LineStarts line_starts;
} Lexer;

typedef enum {
    VarMode_Value,
    VarMode_Ref,
    VarMode_Borrow,
} VarModeTag;

typedef enum {
    Mutability_Mutable,
    Mutability_Immutable,
} Mutability;

typedef struct {
    VarModeTag tag;
    Mutability mutability;
} VarMode;

typedef enum {
    Pattern_Wildcard,
    Pattern_LiteralInt,
    Pattern_LiteralStr,
    Pattern_LiteralBool,
    Pattern_Variant,
    Pattern_VariantTuple,
    Pattern_Struct,
    Pattern_Guard,
    Pattern_Binding,
} PatternTag;

typedef struct Pattern {
    PatternTag tag;
    union {
        int value_int;
        char* value_str;
        bool value_bool;
        struct { char* name; char* inner; } variant;
        char* binding;
        struct { StringView name; StringView* bindings; size_t bindings_count; } variant_tuple;
        struct { StringView name; SourceRange* fields; size_t fields_count; } struct_pattern;
        struct { bool is_var; StringView binding; struct Exprs* expr; struct Pattern* pattern; } guard;
    } data;
    
} Pattern;

typedef struct {
    SourceRange name;
    SourceRange c_type;
    VarMode mode;
} Param;

typedef struct {
    LexerTokenTag op;
    SourceRange function;
} Operation;

typedef struct {
    SourceRange name;
    SourceRange return_type;
    Param* params;
    size_t params_count;
    Stmts* body;
    size_t body_count;
    bool is_pub;
    bool is_unsafe;
    Operation operation;
    SourceRange range;
} FunctionMethod;

typedef struct {
    SourceRange name;
    SourceRange return_type;
    Param* params;
    size_t params_count;
    Stmts* body;
    size_t body_count;
    bool is_pub;
    SourceRange range;
} TraitMethod;

typedef struct {
    SourceRange name;
    EnumField* fields; 
    size_t fields_count;
    SourceRange range;
} EnumVariant;

typedef struct {
    char* name;
    TraitMethod* methods;
    size_t methods_count;
    bool is_pub;
    char* file;
    int lines;
    size_t col;
} TraitNode;

typedef struct {
    SourceRange name;
    SourceRange c_type;
    VarMode mode;
    SourceRange range;
} StructParam;

typedef struct {
    char* name;
    StructParam* fields;
    size_t fields_count;
    char** generic_params;
    size_t generic_params_count;
    bool is_pub;
    char* file;
    int lines;
    size_t col;
} StructMethod;

typedef struct {
    char* name;
    EnumVariant* variants;
    size_t variants_count;
    bool is_pub;
    char* file;
    int lines;
    size_t col;
} EnumNode;

typedef struct {
    char* name;
    char* parent;
    StructParam* fields;
    size_t fields_count;
    FunctionMethod* methods;
    size_t methods_count;
    bool is_abstract;
    bool is_pub;
    char** traits;
    size_t traits_count;
    char* file;
    int lines;
    size_t col;
} ClassMethod;

typedef struct {
    char* name;
    char* field;
} EnumParam;

typedef enum {
    Expr_Function,
    Expr_Class_Calls,
    Expr_Struct_Calls,
    Expr_Enum_Calls,
    Expr_BinaryOps,
    Expr_Literals,
    Expr_Vars,
    Expr_Identifiers,
    Expr_MethodCalls,
} ExprsTag;

struct Exprs {
    ExprsTag tag;
    union {
        struct { SourceRange name; SourceRange* generic_params; size_t generic_params_count; Param* param; size_t param_count; SourceRange range; } function_call;
        struct { SourceRange name; SourceRange function; SourceRange* generic_params; size_t generic_params_count; Param* param; size_t param_count; SourceRange range; } class_calls;
        struct { SourceRange name; SourceRange function; SourceRange* generic_params; size_t generic_params_count; Param* param; size_t param_count; SourceRange range; } struct_calls;
        struct { SourceRange name; SourceRange field; SourceRange* generic_params; size_t generic_params_count; Param* param; size_t param_count; SourceRange range; } enum_calls;
        struct { Exprs* left; LexerTokenTag op; Exprs* right; SourceRange range; } binary_ops;
        struct { SourceRange range; } literals;
        struct { SourceRange name; SourceRange range; } vars;
        struct { SourceRange name; SourceRange range; } identifiers;
        struct { Exprs* object; SourceRange method; Exprs* args; size_t args_count; SourceRange range; } method_calls;
    } data;
};

typedef struct {
    Pattern pattern;
    Stmts* body;
    size_t body_count;
    size_t lines;
    size_t col;
} MatchArm;

typedef struct {
    SourceRange name;
    SourceRange return_type;
    Param* params;
    size_t params_count;
    SourceRange ffi_type;
} ExternFunction;

typedef struct {
    SourceRange abi;
    ExternFunction* funcs;
    size_t funcs_count;
    SourceRange range;
} ExternBlock;

typedef enum {
    Stmt_Functions,
    Stmt_Classes,
    Stmt_Traits,
    Stmt_Structs,
    Stmt_Enums,
    Stmt_Matchs,
    Stmt_Unsafes,
    Stmt_Whiles,
    Stmt_Ifs,
    Stmt_Externs,
    Stmt_Fors,
    Stmt_Returns,
    Stmt_Vars,
    Stmt_Lets,
    Stmt_Locals,
    Stmt_Consts,
    Stmt_ExprStmt,
    Stmt_Assigns,
    Stmt_Continues,
} StmtsTag;

typedef enum {
    ClassAttach_None,
    ClassAttach_Struct,
    ClassAttach_Enum,
} ClassAttachTag;

#define ARR(T) struct { T* data; size_t len; size_t cap; }

typedef struct { Param* data; size_t len, cap; } ParamArr;
typedef struct { StructParam* data; size_t len, cap; } StructParamArr;
typedef struct { Stmts* data; size_t len, cap; } StmtsArr;
typedef struct { SourceRange* data; size_t len, cap; } RangeArr;
typedef struct { FunctionMethod* data; size_t len, cap; } MethodArr;
typedef struct { EnumVariant* data; size_t len, cap; } VariantArr;
typedef struct { EnumField* data; size_t len, cap; } EnumFieldArr;
typedef struct { TraitMethod* data; size_t len, cap; } TraitMethodArr;
typedef struct { MatchArm* data; size_t len, cap; } MatchArmArr;

struct Stmts {
    StmtsTag tag;
    union {
        struct { Exprs target; LexerTokenTag op; Exprs value; SourceRange range; } assigns;
        struct { SourceRange name; SourceRange* generic_params; size_t generic_params_count; Param* params; size_t params_count; SourceRange return_type; Stmts* body; size_t body_count; bool is_pub; bool is_unsafe; SourceRange range; } functions;
        struct { SourceRange name; SourceRange* generic_params; size_t generic_params_count; Param* class_params; size_t class_params_count; StructParam* fields; size_t fields_count; FunctionMethod* methods; size_t methods_count; SourceRange parent; SourceRange* traits; size_t traits_count; bool is_pub; SourceRange range; ClassAttachTag attached_tag; StructParam* attached_fields; size_t attached_fields_count; } classes;
        struct { SourceRange name; TraitMethod* methods; size_t methods_count; bool is_pub; SourceRange range; } traits;
        struct { SourceRange name; SourceRange* generic_params; size_t generic_params_count; StructParam* fields; size_t fields_count; bool is_pub; SourceRange range; } structs;
        struct { SourceRange name; SourceRange* generic_params; size_t generic_params_count; EnumVariant* variants; size_t variants_count; bool is_pub; SourceRange range; } enums;
        struct { Exprs expr; MatchArm* cases; size_t cases_count; Stmts* default_body; size_t default_body_count; SourceRange range; } matchs;
        struct { Stmts* body; size_t body_count; SourceRange range; } unsafes;
        struct { Exprs cond; Stmts* body; size_t body_count; Stmts* else_body; size_t else_body_count; SourceRange range; Pattern guard_pattern;} ifs;
        struct { Exprs cond; Stmts* body; size_t body_count; SourceRange range; } whiles;
        struct { SourceRange _var; Exprs iter; Stmts* body; size_t body_count; SourceRange range; } fors;
        struct { Exprs expr; SourceRange range; } returns;
        struct { SourceRange name; SourceRange c_type; Exprs value; VarMode mode; SourceRange range; } vars;
        struct { SourceRange name; SourceRange c_type; Exprs value; VarMode mode; SourceRange range; } lets;
        struct { SourceRange name; SourceRange c_type; bool is_pub; SourceRange range; } locals;
        struct { SourceRange name; SourceRange c_type; Exprs value; bool is_pub; SourceRange range; } consts;
        struct { Exprs expr; } expr_stmt;
        struct { SourceRange abi; SourceRange ffi; ExternFunction* funcs; size_t funcs_count; bool is_pub; } extern_;
        struct { ExternBlock block; SourceRange ffi; SourceRange range; } externs;
    } data;
};

typedef enum {
    Type_Int,
    Type_Float,
    Type_Char,
    Type_Str,
    Type_Bool,
    Type_Void,
    Type_Array,
    Type_Ptr,
    Type_RawPtr,
    Type_FnPtr,
    Type_Custom,
} TypeTag;


struct Type {
    TypeTag tag;
    union {
        struct { int bits; } int_t;
        struct { int bits; } float_t;
        struct { Type* inner; size_t len; } array_t;
        struct { Type* inner; } ptr;
        struct { Type* inner; } raw_ptr;
        struct { SourceRange name; } custom;
        struct {
            Type* ret;
            Type* params;
            size_t params_count;
        } fn_ptr;
    } data;
};

#endif
