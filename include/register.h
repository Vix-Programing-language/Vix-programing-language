#ifndef REGISTER_H
#define REGISTER_H

#include "third-party/khashl.h"
#include "token/ast/ast.h"

typedef enum {
    Reg_Var,
    Reg_Let,
    Reg_Const,
    Reg_Local,
    Reg_Function,
    Reg_Class,
    Reg_Struct,
    Reg_Enum,
    Reg_Trait,
    Reg_ExprFunctionCall,
    Reg_ExprClassCall,
    Reg_ExprStructCall,
    Reg_ExprEnumCall,
    Reg_ExprMethodCall,
    Reg_ExprBinaryOp,
    Reg_ExprIdentifier,
    Reg_ExprLiteral,
    Reg_ExprVar,
} RegisterEntryTag;

typedef struct {
    RegisterEntryTag tag;
    char* name;
    Type  type;
    union {
        struct { Type type; VarMode mode; bool is_mut; } var;
        struct { Type type; VarMode mode; } let;
        struct { Type type; bool is_pub; } const_;
        struct { Type type; bool is_pub; } local;
        struct { Param* params; size_t params_count; Type return_type; bool is_pub; bool is_unsafe; } function;
        struct { StructParam* fields; size_t fields_count; char** traits; size_t traits_count; char* parent; bool is_pub; FunctionMethod* methods; size_t methods_count; bool has_attached; ClassAttachTag attached_tag; StructParam* attached_fields; size_t  attached_fields_count; } class;
        struct { StructParam* fields; size_t fields_count; bool is_pub; } strct;
        struct { EnumVariant* variants; size_t variants_count; bool is_pub; } enm;
        struct { TraitMethod* methods; size_t methods_count; bool is_pub; } trait;
        struct { SourceRange name; Param* params; size_t params_count; Type return_type; } expr_function_call;
        struct { SourceRange name; SourceRange function; Param* params; size_t params_count; Type return_type; } expr_class_call;
        struct { SourceRange name; SourceRange function; Param* params; size_t params_count; Type return_type; } expr_struct_call;
        struct { SourceRange name; SourceRange field; Type resolved_type; } expr_enum_call;
        struct { Type obj_type; SourceRange method; Param* params; size_t params_count; Type return_type; } expr_method_call;
        struct { Type left_type; LexerTokenTag op; Type right_type; Type resolved_type; } expr_binary_op;
        struct { SourceRange name; Type resolved_type; } expr_identifier;
        struct { Type resolved_type; } expr_literal;
        struct { SourceRange name; Type resolved_type; } expr_var;
    } data;
} RegisterEntry;

static inline khint_t string_view_hash(StringView view) {
    return kh_hash_bytes((int)view.len, (const unsigned char*)view.ptr);
}

static inline int string_view_eq(StringView a, StringView b) {
    return a.len == b.len && memcmp(a.ptr, b.ptr, a.len) == 0;
}

KHASHL_MAP_INIT(KH_LOCAL, RegisterTable, register_table, StringView, RegisterEntry, string_view_hash, string_view_eq)

typedef struct Register {
    RegisterTable*  table;
    struct Register* parent;
} Register;

typedef struct {
    char* type_name;
    Type  type;
} GenericArg;

typedef struct {
    StringView  func_name;
    GenericArg* args;
    size_t      args_count;
    Type        return_type;
    Param*      params;
    size_t      params_count;
} GenericInstance;

KHASHL_MAP_INIT(KH_LOCAL, GenericInstanceTable, generic_instance_table, StringView, GenericInstance, string_view_hash, string_view_eq)

typedef struct {
    GenericInstanceTable* table;
} GenericRegistry;

#endif
