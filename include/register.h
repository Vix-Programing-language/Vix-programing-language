#ifndef REGISTER_H
#define REGISTER_H

#include "third-party/khashl.h"
#include "ast.h"
#include "import.h"
#include "error.h"

typedef struct Register Register;

typedef enum {
    FieldOwner_Unknown,
    FieldOwner_Struct,
    FieldOwner_Class,
    FieldOwner_Enum,
} FieldOwnerKind;

typedef enum {
    Reg_Var, 
    Reg_Let, 
    Reg_Const, 
    Reg_Local,
    Reg_Param,
    Reg_Function, 
    Reg_Class, 
    Reg_Struct, 
    Reg_Enum,
    Reg_Trait, 
    Reg_Extern,
    Reg_ExternFunc,
    Reg_If,
    Reg_Elif,
    Reg_While,
    Reg_For,
    Reg_Match,
    Reg_Return,
    Reg_ExprStmt,
    Reg_Module,
    Reg_Atomic,
    Reg_ExprFunctionCall, 
    Reg_ExprClassCall,
    Reg_ExprStructCall, 
    Reg_ExprEnumCall,
    Reg_ExprMethodCall, 
    Reg_ExprBinaryOp,
    Reg_ExprIdentifier, 
    Reg_ExprLiteral, 
    Reg_ExprVar,
    Reg_ExprIdx,
    Reg_ExprArray,
    Reg_ExprUnary,
    Reg_ExprCast,
    Reg_Assign,
    Reg_ExprField,
    Reg_ExprAddrOf,
} RegisterEntryTag;

khint_t string_hash(StringView view) { return kh_hash_bytes((int)view.len, (const unsigned char*)view.ptr); }
int string_eq(StringView a, StringView b) { return a.len == b.len && memcmp(a.ptr, b.ptr, a.len) == 0; }

typedef struct {
    SourceRange param;
    Type bound;
} GenericBinding;

typedef struct {
    char* mangled_name;
    GenericBinding*  bindings;
    size_t bindings_count;
    RegisterEntryTag def_kind;
    StringView def_name;
} GenericInstance;

KHASHL_MAP_INIT(KH_LOCAL, GenericInstanceTable, mono_table, StringView, GenericInstance, string_hash, string_eq)

typedef struct {
    GenericInstanceTable* table;
} GenericRegistry;

typedef struct { uint32_t id; uint16_t file_id; RegisterEntryTag kind; } EntityID;
typedef struct { uint32_t next_id; } IDCounter;
typedef struct RegisterEntry RegisterEntry;

struct RegisterEntry {
    RegisterEntryTag tag;
    char* name;
    Type type;
    EntityID eid;
    SourceRange decl_range;
    SourceRange decl_name_range;
    uint32_t flat_id;
    uint32_t flat_scope_id;
    uint32_t flat_first_child_id;
    uint32_t flat_next_sibling_id;
    union { 
        struct { Type type; bool is_pub; } local;
        struct { Param* params; size_t params_count; Type return_type; bool is_pub; bool is_unsafe; Register* child_reg; SourceRange* generic_params; size_t generic_params_count; GenericParam* generic_param_nodes; Stmts* body; size_t body_count; } function;
        struct { SourceRange name; Type return_type; Param* params; size_t params_count; bool is_pub; } extern_func;
        struct { StructParam* fields; size_t fields_count; bool is_pub; SourceRange* generic_params; size_t generic_params_count; GenericParam* generic_param_nodes; } strct;
        struct { EnumVariant* variants; size_t variants_count; bool is_pub; SourceRange* generic_params; size_t generic_params_count; GenericParam* generic_param_nodes; } enm;
        struct { StructParam* fields; size_t fields_count; char** traits; size_t traits_count; char* parent; bool is_pub; FunctionMethod* methods; size_t methods_count; ClassAttachTag attached_tag; StructParam* attached_fields; size_t attached_fields_count; SourceRange* generic_params; size_t generic_params_count; GenericParam* generic_param_nodes; Register* child_reg; } _class;
        struct { TraitMethod* methods; size_t methods_count; bool is_pub; } trait;
        struct { SourceRange name; Param* params; size_t params_count; Type return_type; SourceRange* generic_args; size_t generic_args_count; Register* child_reg; uint32_t* arg_ids; size_t arg_ids_count; } expr_function_call;
        struct { SourceRange name; SourceRange function; Param* params; size_t params_count; Type return_type; SourceRange* generic_args; size_t generic_args_count; } expr_class_call;
        struct { SourceRange name; SourceRange function; Param* params; size_t params_count; Type return_type; SourceRange* generic_args; size_t generic_args_count; } expr_struct_call;
        struct { SourceRange name; SourceRange field; Type resolved_type; SourceRange* generic_args; size_t generic_args_count; } expr_enum_call;
        struct { Type left_type; LexerTokenTag op; Type right_type; Type resolved_type; uint32_t left_id; uint32_t right_id; } expr_binary_op;
        struct { LexerTokenTag op; RegisterEntry* operand; Type resolved_type; } expr_unary;
        struct { uint32_t base_id; uint32_t index_id; SourceRange range; Type elem_ty; } idx; 
        struct { RegisterEntry** elems; size_t elems_count; } array;
        struct { uint32_t expr_id; Type* ty; SourceRange range; } expr_cast;
        struct { RegisterEntry* object; SourceRange method; RegisterEntry** args; size_t args_count; SourceRange range; } expr_method_call; 
        struct { SourceRange name; Type resolved_type; } expr_identifier;
        struct { Type resolved_type; } expr_literal;
        struct { SourceRange abi; SourceRange ffi; ExternFunction* funcs; size_t funcs_count; bool is_pub; Register* child_reg; } extern_;
        struct { SourceRange name; Type resolved_type; } expr_var;
        struct { LexerTokenTag op; uint32_t target_id; uint32_t value_id; } assign;
        struct { uint32_t first_child_id; uint32_t next_sibling_id; uint32_t scope_id; } flat;
        struct { Type type; VarMode mode; bool is_mut; RegisterEntry* init; } var;
        struct { Type type; VarMode mode; RegisterEntry* init; } let;
        struct { Type type; bool is_pub; RegisterEntry* init; } const_;

        struct { uint32_t cond_id; Register* cond_child; Register* then_child; Register* else_child; Stmts* body; size_t body_count; Stmts* else_body; size_t else_body_count; } if_;
        struct { uint32_t cond_id; Register* cond_child; Register* body_child; Stmts* body; size_t body_count; } while_;
        struct { SourceRange var; uint32_t iter_id; Register* iter_child; Register* body_child; Stmts* body; size_t body_count; } for_;
        struct { uint32_t expr_id; Register* expr_child; Register* default_child; Register** arm_children; MatchArm* cases; size_t cases_count; Stmts* default_body; size_t default_body_count; } match_;

        struct { EntityID expr; bool has_expr; } return_;
        struct { uint32_t expr_id; } expr_stmt_;

        struct { Stmts* body; size_t body_count; } unsafe_;
        struct { SourceRange name; Stmts* body; size_t body_count; bool is_pub; } module_;
        struct { SourceRange target; AtomicOpTag op; size_t args_count; OrderingTag ordering; OrderingTag ordering2; } atomic_;
        struct { ImportKind kind; SourceRange name; SourceRange path; } import_; 
        struct { RegisterEntry* object; SourceRange field; SourceRange range; FieldOwnerKind kind; EntityID type_eid; } expr_field;
    } data;
};

KHASHL_MAP_INIT(KH_LOCAL, RegisterTable, register_table, StringView, RegisterEntry, string_hash, string_eq)
KHASHL_MAP_INIT(KH_LOCAL, PendingTable, pending_table, StringView, EntityID, string_hash, string_eq)

typedef struct Register {
    RegisterTable* table;
    struct Register* parent;
    PendingTable* pending;
    IDCounter* counter;
    GenericRegistry* mono;
    SourceRange owner_class;
} Register;

void register_free(Register* reg);

typedef struct { char* func_name; uint32_t func_chunk; Register* body_reg; } FuncBody;

typedef struct {
    FuncBody* data; size_t len; size_t cap; uint32_t func_counter;
} FuncBodyList;

void func_body_list_push(FuncBodyList* fl, FuncBody fb) { ARR_PUSH(*fl, fb); }

void func_body_list_free(FuncBodyList* fl) {
    for (size_t i = 0; i < fl->len; i++) {
        free(fl->data[i].func_name);
        register_free(fl->data[i].body_reg);
        free(fl->data[i].body_reg);
    }
    ARR_FREE(*fl);
}

typedef struct { EntityID* ids; size_t count; RegisterEntryTag* kinds; } ID;
typedef struct { StringView type_name; Type type; } GenericArg;

typedef __typeof__(((Stmts*)0)->data.functions) FunctionData;
typedef __typeof__(((Stmts*)0)->data.classes)   ClassData;
typedef __typeof__(((Stmts*)0)->data.traits)    TraitData;
typedef __typeof__(((Stmts*)0)->data.structs)   StructData;
typedef __typeof__(((Stmts*)0)->data.enums)     EnumData;
typedef __typeof__(((Stmts*)0)->data.vars)      VarData;
typedef __typeof__(((Stmts*)0)->data.lets)      LetData;
typedef __typeof__(((Stmts*)0)->data.consts)    ConstData;
typedef __typeof__(((Stmts*)0)->data.locals)    LocalData;
typedef __typeof__(((Stmts*)0)->data.ifs)       IfData;
typedef __typeof__(((Stmts*)0)->data.whiles)    WhileData;
typedef __typeof__(((Stmts*)0)->data.fors)      ForData;
typedef __typeof__(((Stmts*)0)->data.matchs)    MatchData;
typedef __typeof__(((Stmts*)0)->data.unsafes)   UnsafeData;
typedef __typeof__(((Stmts*)0)->data.assigns)   AssignData;
typedef __typeof__(((Stmts*)0)->data.modules)   ModuleData;
typedef __typeof__(((Stmts*)0)->data.atomic_op) AtomicData;
typedef __typeof__(((Stmts*)0)->data.externs)   ExternData;
typedef __typeof__(((Stmts*)0)->data.imports)   ImportData;

void register_stmt(Register* reg, Stmts* stmt, SourceRange class_name);
RegisterEntry* register_expr(Register* reg, Exprs* expr, SourceRange class_name);
EntityID register_insert(Register* reg, RegisterEntry entry);
RegisterEntry* register_lookup(Register* reg, StringView key);
RegisterEntry* register_get_by_id(Register* reg, uint32_t id);
RegisterEntry* register_get(Register* reg, StringView name);
uint32_t register_get_id(Register* reg, StringView name);
StringView register_get_name(Register* reg, uint32_t id);
Register register_new(Register* parent, IDCounter* counter);
void register_free(Register* reg);
StringView sv_from_range(SourceRange r);
FuncBodyList register_body(Stmts* body, size_t count, Register* reg, CheckerErrList* errors);



#define REG_FOREACH(reg, entry_var, body) \
    for (khint_t _i = 0; _i != kh_end((reg)->table); ++_i) { \
        if (!kh_exist((reg)->table, _i)) continue; \
        RegisterEntry* entry_var = &kh_val((reg)->table, _i); \
        body \
    }

#define REG_STMTS(reg, arr, n, class_name) \
    for (size_t _i = 0; _i < (n); _i++) register_stmt((reg), &(arr)[_i], (class_name))

#define REG_GET_CLASS_NAME(...) REG_GET_CLASS_NAME_(__VA_ARGS__, (SourceRange){0})
#define REG_GET_CLASS_NAME_(first, ...) first

#define REG_EXPRS(reg, arr, n, ...) \
    for (size_t _i = 0; _i < (n); _i++) register_expr((reg), &(arr)[_i], REG_GET_CLASS_NAME(__VA_ARGS__))

#define REG_PARAM_EXPRS(reg, arr, n, ...) \
    for (size_t _i = 0; _i < (n); _i++) register_expr((reg), &(arr)[_i].value, REG_GET_CLASS_NAME(__VA_ARGS__))

#define DEREF_TYPE(ptr, fallback_range) \
    ((ptr) ? *(ptr) : type_from_range(fallback_range))

#endif