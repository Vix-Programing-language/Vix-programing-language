#include "import.h"

typedef struct Stmts Stmts;
typedef struct Exprs Exprs;

typedef enum {
    Plus=1,
    Minuss,
    Stars,
    Slashs,
    Percents,
    Lesses,
    Greaters,
    NEqs,
    Equalss,
    Ampersands,
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

typedef union {
    char* s;
    uint64_t value_int;
    float value_float;
    char value_char;
} LexerTokenData;

typedef struct {
    const char* start;
    const char* end;
}SourceRange;

typedef struct {
    const char** data;//last line is array end ie \0
    size_t len;
    size_t cap;
} LineStarts;

static inline size_t get_line_num(const LineStarts* starts, uintptr_t tgt) {
    if (!starts || !starts->data || starts->len < 2) {
        return (size_t)-1;
    }

    uintptr_t first = (uintptr_t)starts->data[0];
    uintptr_t end   = (uintptr_t)starts->data[starts->len - 1];

    // Outside the covered buffer range.
    if (tgt < first || tgt >= end) {
        return (size_t)-1;
    }

    // Search among actual line starts: [0, len-2]
    size_t lo = 0;
    size_t hi = starts->len - 1; // exclusive upper bound for line index + 1

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
    LexerTokenTag tag;
    LexerTokenData data;
    SourceRange debug;
} LexerToken;

typedef struct {
    LexerToken top;
    const char* source;
    const char* cur;
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
    Pattern_Binding,
} PatternTag;

typedef struct {
    PatternTag tag;
    union {
        int value_int;
        char* value_str;
        bool value_bool;
        struct { char* name; char* inner; } variant;
        char* binding;
    } data;
} Pattern;



typedef struct {
    char* name;
    char* c_type;
    VarMode mode;
} Param;


typedef struct {
    char* name;
    char* return_type;
    Param* params;
    size_t params_count;
    Stmts* body;
    size_t body_count;
    bool is_pub;
    bool is_unsafe;
    char* file;
    int lines;
    size_t col;
} FunctionMethod;

 

typedef struct {
    char* name;
    char* return_type;
    Param* params;
    size_t params_count;
    Stmts* body;
    size_t body_count;
    char* file;
    int lines;
    bool is_pub;
    size_t col;
} TraitMethod;

 

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
    char* name;
    char* c_type;
    VarMode mode;
    int lines;
    size_t col;
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
     
    struct { char* first; char* second; }* fields;
    size_t fields_count;
    int lines;
    size_t col;
} EnumVariant;

 

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
        struct { char* name; FunctionMethod function; char** input_calls; size_t input_calls_count; char* current_scope; bool insta_gen; size_t lines; size_t col; } class_calls;
        struct { char* name; StructParam param; char* target_field; char* input_field; char* insta_name; bool insta_gen; size_t lines; size_t col; } struct_calls;
        struct { char* name; EnumParam param; char* target_field; char* input_field; bool insta_gen; size_t lines; size_t col; } enum_calls;
        struct { Exprs* left; LexerTokenTag op; Exprs* right; size_t lines; size_t col; } binary_ops;
        struct { char* value; size_t lines; size_t col; } literals;
        struct { char* name; size_t lines; size_t col; } vars;
        struct { char* name; size_t lines; size_t col; } identifiers;
        struct { Exprs* object; char* method; Exprs* args; size_t args_count; size_t lines; size_t col; } method_calls;
    } data;
};

 

typedef struct {
    Pattern pattern;
    Stmts* body;
    size_t body_count;
    size_t lines;
    size_t col;
} MatchArm;

 

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
    Stmt_Fors,
    Stmt_Returns,
    Stmt_Vars,
    Stmt_Lets,
    Stmt_Locals,
    Stmt_Consts,
    Stmt_ExprStmt,
} StmtsTag;

struct Stmts {
    StmtsTag tag;
    union {
        struct { char* name; char** generic_params; size_t generic_params_count; Param* params; size_t params_count; char* return_type; Stmts* body; size_t body_count; bool is_pub; bool is_unsafe; size_t lines; size_t col; } functions;
        struct { char* name; char** generic_params; size_t generic_params_count; Param* class_params; size_t class_params_count; StructParam* fields; size_t fields_count; FunctionMethod* methods; size_t methods_count; char* parent; char** traits; size_t traits_count; bool is_pub; size_t lines; size_t col; } classes;
        struct { char* name; TraitMethod* methods; size_t methods_count; bool is_pub; size_t lines; size_t col; } traits;
        struct { char* name; StructParam* fields; size_t fields_count; bool is_pub; size_t lines; size_t col; } structs;
        struct { char* name; EnumVariant* variants; size_t variants_count; bool is_pub; size_t lines; size_t col; } enums;
        struct { Exprs expr; MatchArm* cases; size_t cases_count; Stmts* default_body; size_t default_body_count; size_t lines; size_t col; } matchs;
        struct { Stmts* body; size_t body_count; size_t lines; size_t col; } unsafes;
        struct { Exprs cond; Stmts* body; size_t body_count; size_t lines; size_t col; } whiles;
        struct { Exprs cond; Stmts* body; size_t body_count; Stmts* else_body; size_t else_body_count; size_t lines; size_t col; } ifs;
        struct { char* _var; Exprs iter; Stmts* body; size_t body_count; size_t lines; size_t col; } fors;
        struct { Exprs expr; size_t lines; size_t col; } returns;
        struct { char* name; char* c_type; bool has_value; Exprs value; VarMode mode; size_t lines; size_t col; } vars;
        struct { char* name; char* c_type; bool has_value; Exprs value; VarMode mode; size_t lines; size_t col; } lets;
        struct { char* name; char* c_type; bool is_pub; size_t lines; size_t col; } locals;
        struct { char* name; char* c_type; bool has_value; Exprs value; bool is_pub; size_t lines; size_t col; } consts;
        struct { Exprs expr; } expr_stmt;
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
    Type_Option,
    Type_Custom,
} TypeTag;

typedef struct Type Type;

struct Type {
    TypeTag tag;
    union {
        struct { uint64_t bits; } int_t;
        struct { uint64_t bits; } float_t; 
        struct { Type* inner; } array_t;
        struct { char* name; } custom;
    } data;
};
