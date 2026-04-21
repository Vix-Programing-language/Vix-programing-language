#include "file_manager.h" //for SourceRange

typedef struct {
    SourceRange range;
    const char* var_name;
    const char* expected_type;
    const char* actual_type;
} Err_VMV;

typedef struct {
    SourceRange range;
    const char* var_name;
} Err_VSF;

typedef struct {
    SourceRange range;
    const char* var_name;
    const char* binding_kind;
} Err_VNM;

typedef struct {
    SourceRange range;
    const char* var_name;
    const char* type_name;
} Err_VPT;

typedef struct {
    SourceRange range;
    const char* type_name;
} Err_TNF;

typedef struct {
    SourceRange range;
    const char* type_name;
    const char* actual_kind;
} Err_TNC;

typedef struct {
    SourceRange range;
    const char* class_name;
    const char* op;
} Err_OUD;

typedef struct {
    SourceRange range;
    const char* class_name;
    const char* method_name;
    const char* op;
} Err_OMP;

typedef struct {
    SourceRange range;
    const char* class_name;
    const char* method_name;
    const char* op;
    const char* expected_type;
    const char* actual_type;
} Err_OMM;

typedef struct {
    SourceRange range;
} Err_LHS;

typedef struct {
    SourceRange range;
    const char* var_name;
} Err_CVN;

typedef struct {
    SourceRange range;
    const char* var_name;
} Err_RDL;


typedef enum {
    Err_Tag_VMV,
    Err_Tag_VSF,
    Err_Tag_VNM,
    Err_Tag_VPT,
    Err_Tag_TNF,
    Err_Tag_TNC,
    Err_Tag_OUD,
    Err_Tag_OMP,
    Err_Tag_OMM,
    Err_Tag_LHS,
    Err_Tag_CVN,
    Err_Tag_RDL,
} CheckerErrTag;

typedef struct {
    CheckerErrTag tag;
    union {
        Err_VMV vmv;
        Err_VSF vsf;
        Err_VNM vnm;
        Err_VPT vpt;
        Err_TNF tnf;
        Err_TNC tnc;
        Err_OUD oud;
        Err_OMP omp;
        Err_OMM omm;
        Err_LHS lhs;
        Err_CVN cvn;
        Err_RDL rdl;
    } data;
} CheckerErr;

typedef struct {
    CheckerErr* errors;
    size_t      count;
    size_t      cap;
} CheckerErrList;

static inline void checker_err_push(CheckerErrList* list, CheckerErr err) {
    if (list->count >= list->cap) {
        list->cap = list->cap == 0 ? 8 : list->cap * 2;
        list->errors = realloc(list->errors, list->cap * sizeof(CheckerErr));
    }
    list->errors[list->count++] = err;
}
