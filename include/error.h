#include "ast.h"
#include "file_manager.h"


typedef struct {
    SourceRange range;
    StringView var_name;
    StringView expected_type;
    StringView actual_type;
} Err_VMV;

typedef struct {
    SourceRange range;
    StringView var_name;
} Err_VSF;

typedef struct {
    SourceRange range;
    StringView var_name;
    StringView binding_kind;
    SourceRange decl_range;
    SourceRange decl_name_range;
} Err_VNM;

typedef struct {
    SourceRange range;
    StringView var_name;
    StringView type_name;
} Err_VPT;

typedef struct {
    SourceRange range;
    StringView type_name;
} Err_TNF;

typedef struct {
    SourceRange range;
    StringView type_name;
    StringView actual_kind;
} Err_TNC;

typedef struct {
    SourceRange range;
    StringView class_name;
    StringView op;
} Err_OUD;

typedef struct {
    SourceRange range;
    StringView class_name;
    StringView method_name;
    StringView op;
} Err_OMP;

typedef struct {
    SourceRange range;
    StringView class_name;
    StringView method_name;
    StringView op;
    StringView expected_type;
    StringView actual_type;
} Err_OMM;

typedef struct {
    SourceRange range;
} Err_LHS;

typedef struct {
    SourceRange range;
    StringView var_name;
} Err_CVN;

typedef struct {
    SourceRange range;
    StringView var_name;
} Err_RDL;

typedef struct {
    SourceRange range;
    StringView cond_type;
} Err_NBC;

typedef struct {
    SourceRange range;
} Err_AIC;

typedef struct {
    SourceRange range;
    bool is_always_true;
} Err_DCB;

typedef struct {
    SourceRange range;
    bool is_always_true;
} Err_TAU;

typedef struct {
    SourceRange range;
} Err_RNC;

typedef struct {
    SourceRange range;
} Err_EMB;

typedef struct {
    SourceRange range;
} Err_DEC;

typedef struct {
    SourceRange range;
} Err_URC;

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
    Err_Tag_NBC,  
    Err_Tag_AIC,  
    Err_Tag_DCB,  
    Err_Tag_TAU,  
    Err_Tag_RNC,  
    Err_Tag_EMB,  
    Err_Tag_DEC,  
    Err_Tag_URC,  
    Err_Tag_PTM,  
    Err_Tag_SFF,  
    Err_Tag_WFC,  
    Err_Tag_DCP,  
    Err_Tag_MLM,  
    Err_Tag_MBM,  
    Err_Tag_NDF,  
    Err_Tag_WTC,  
    Err_Tag_WLC,  
    Err_Tag_NIT,  
    Err_Tag_ITR,  
    Err_Tag_ITV,  
    Err_Tag_BSV,  
    Err_Tag_ULV,  
    Err_Tag_DFN,  
    Err_Tag_UFT,  
    Err_Tag_VFT,  
    Err_Tag_EST,  
    Err_Tag_DVN,  
    Err_Tag_DFV,  
    Err_Tag_EEN,  
    Err_Tag_SVE,  
    Err_Tag_GPU,  
    Err_Tag_NCB,  
    Err_Tag_MRT,  
    Err_Tag_TMI,
    Err_Tag_Parse,
} CheckerErrTag;

typedef struct {
    CheckerErrTag tag;
    SourceRange range;    
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
        Err_NBC nbc;
        Err_AIC aic;
        Err_DCB dcb;
        Err_TAU tau;
        Err_RNC rnc;
        Err_EMB emb;
        Err_DEC dec;
        Err_URC urc;
        struct {
            SourceRange range;
            bool is_always_true;
        } wlc;
        struct {
            SourceRange range;
            const char* message;
            int expected;
            int got;
            int kind;
        } parse;
        struct {
            SourceRange range;
            StringView iter_name;
        } itv;  
        struct {
            SourceRange range;
            StringView iter_name;
        } itr;  
        struct {
            SourceRange range;
            StringView iter_name;
            StringView iter_type;
        } nit;  
        struct {
            SourceRange range;
            StringView var_name;
        } bsv;  
        struct {
            SourceRange range;
            StringView var_name;
        } ulv;  
        struct {
            SourceRange range;
            StringView type_name;
        } ncb;  
        struct {
            SourceRange range;
            StringView type_name;
        } est;  
        struct {
            SourceRange range;
            StringView type_name;
        } een;  
        struct {
            SourceRange range;
            StringView type_name;
        } sve;  
        struct {
            SourceRange range;
            StringView field_name;
            StringView type_name;
        } vft;  
        struct {
            SourceRange range;
            StringView field_name;
            StringView type_name;
        } uft;  
        struct {
            SourceRange range;
            StringView field_name;
            StringView type_name;
        } dfn;  
        struct {
            SourceRange range;
            StringView variant_name;
            StringView type_name;
        } dvn;  
        struct {
            SourceRange range;
            StringView field_name;
            StringView variant_name;
            StringView type_name;
        } dfv;  
        struct {
            SourceRange range;
            StringView param_name;
            StringView type_name;
        } gpu;  
        struct {
            SourceRange range;
            StringView func_name;
            StringView return_type;
        } mrt;
        struct {
            SourceRange range;
            StringView class_name;
            StringView trait_name;
            StringView method_name;
        } tmi;

        struct { SourceRange range; StringView expected_type; StringView actual_type; } ptm;
        struct { SourceRange range; StringView field_name; StringView type_name; } sff;
        struct { SourceRange range; StringView variant_name; size_t expected_count; size_t actual_count; } wfc;
        struct { SourceRange range; } dcp;
        struct { SourceRange range; } mlm;
        struct { SourceRange range; } mbm;
        struct { SourceRange range; StringView variant_name; StringView type_name; } ndf;
    } data;
} CheckerErr;

typedef struct CheckerErrList {
    CheckerErr* errors;
    size_t count;
    size_t cap;
    CheckerErrTag tag;
    SourceRange range;
} CheckerErrList;

typedef struct {
    const char** filenames;     
    const char** sources;       
    LineStarts* line_starts;
    size_t file_count;
} FileTable;

void checker_err_push(CheckerErrList* list, CheckerErr err);
void checker_set_file_table(FileTable table);
