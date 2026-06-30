#include "ast.h"
#include "import.h"
#include "error.h"

#define C_RED       "\033[1;31m"
#define C_BOLD_RED  "\033[1;31m"
#define C_GREEN     "\033[1;32m"
#define C_YELLOW    "\033[0;33m"
#define C_BLUE      "\033[1;34m"
#define C_CYAN      "\033[0;36m"
#define C_BOLD      "\033[1m"
#define C_DIM       "\033[2m"
#define C_RESET     "\033[0m"

static FileTable g_table = {0};

typedef struct {
    size_t line;
    size_t col;
    const char* line_start;
    size_t line_len;
} SourcePos;

typedef struct {
    bool enabled;
    SourceRange line_range;
    SourceRange highlight_range;
    const char* annotation_prefix;
    StringView annotation_name;
    const char* annotation_suffix;
    const char* replacement_from;
    const char* replacement_to;
} RelatedSnippet;

typedef struct {
    CheckerErrTag error_id;
    SourceRange primary_range;
    const char* title;
    const char* caret_prefix;
    StringView caret_name;
    RelatedSnippet related;
    const char* help_label;
    const char* help_hint;
    const char* note_label;
    const char* note_detail;
    const char* note_detail_2;
} DiagnosticRenderSpec;

static SourcePos resolve_pos(size_t file_id, const char* source, const char* ptr) {
    SourcePos pos = { .line = 1, .col = 1, .line_start = source };

    if (!source || !ptr) return pos;

    if (g_table.line_starts && file_id < g_table.file_count) {
        LineStarts starts = g_table.line_starts[file_id];
        size_t line_index = get_line_num(&starts, (uintptr_t)ptr);

        if (line_index != (size_t)-1) {
            pos.line = line_index + 1;
            pos.line_start = starts.data[line_index];
            pos.col = (size_t)(ptr - pos.line_start) + 1;

            const char* end = pos.line_start;
            while (*end && *end != '\n') end++;
            pos.line_len = (size_t)(end - pos.line_start);
            return pos;
        }
    }

    for (const char* p = source; p < ptr; p++) {
        if (*p == '\n') {
            pos.line++;
            pos.col = 1;
            pos.line_start = p + 1;
        } else {
            pos.col++;
        }
    }

    const char* end = pos.line_start;
    while (*end && *end != '\n') end++;
    pos.line_len = (size_t)(end - pos.line_start);
    return pos;
}


static const char* file_name(size_t file_id) {
    if (file_id < g_table.file_count) {
        return g_table.filenames ? g_table.filenames[file_id] : NULL;
    }
    return NULL;
}

static void repeat_char(char ch, size_t n) { for (size_t i = 0; i < n; i++) putchar(ch); }
static void print_snippet_line(size_t line_no, const char* line_start, size_t line_len) { printf("%zu | %.*s\n", line_no, (int)line_len, line_start); }
static void print_snippet_body(const char* line_start, size_t line_len) { printf("%.*s", (int)line_len, line_start); }

static const char* find_substr_n(const char* hay, size_t hay_len, const char* needle, size_t needle_len) {
    if (!hay || !needle) return NULL;
    if (needle_len == 0) return hay;
    if (hay_len < needle_len) return NULL;

    for (size_t i = 0; i + needle_len <= hay_len; i++) { if (memcmp(hay + i, needle, needle_len) == 0) return hay + i; }

    return NULL;
}

static void print_string_view(StringView view) {
    if (view.ptr && view.len) {
        printf("%.*s", (int)view.len, view.ptr);
    }
}

static bool has_string_view(StringView view) {
    return view.ptr != NULL && view.len != 0;
}

static void print_related_annotation(const RelatedSnippet* related) {
    if (!related) return;
    if (related->annotation_prefix) printf("%s", related->annotation_prefix);
    if (has_string_view(related->annotation_name)) {
        if (related->annotation_prefix) putchar(' ');
        print_string_view(related->annotation_name);
    }
    if (related->annotation_suffix) {
        if (related->annotation_prefix || has_string_view(related->annotation_name)) putchar(' ');
        printf("%s", related->annotation_suffix);
    }
}

static bool print_replaced_line(
    const char* line_start,
    size_t line_len,
    const char* from_kw,
    const char* to_kw
) {
    if (!line_start || !to_kw) return false;
    if (!from_kw) {
        print_snippet_body(line_start, line_len);
        return true;
    }

    size_t from_len = strlen(from_kw);
    const char* kw = find_substr_n(line_start, line_len, from_kw, from_len);
    if (!kw) return false;

    size_t prefix_len = (size_t)(kw - line_start);
    size_t suffix_off = prefix_len + from_len;
    size_t suffix_len = line_len - suffix_off;

    printf("%.*s%s%.*s", (int)prefix_len, line_start, to_kw, (int)suffix_len, line_start + suffix_off);
    return true;
}

static void report_error_visual(const DiagnosticRenderSpec* spec, const char* source) {
    if (!spec || !source || !spec->primary_range.start || !spec->primary_range.end) return;

    SourcePos pos = resolve_pos(spec->primary_range.file_id, source, spec->primary_range.start);
    size_t range_len = (size_t)(spec->primary_range.end - spec->primary_range.start);
    const char* filename = file_name(spec->primary_range.file_id);

    if (range_len == 0) range_len = 1;
    if (!filename) filename = "<unknown>";

    bool has_related = spec->related.enabled && spec->related.line_range.start && spec->related.line_range.end;
    bool has_help_content = has_related;

    size_t max_line = pos.line;

    if (has_related) {
        SourcePos rp = resolve_pos(spec->related.line_range.file_id, source, spec->related.line_range.start);
    
        if (rp.line > max_line) max_line = rp.line;
    }

    int gutter = 1;
    for (size_t n = max_line; n >= 10; n /= 10) gutter++;
    gutter += 1;

    printf("\n" C_BOLD_RED "[!] Error %d" C_RESET ": " C_BOLD "%s\n" C_RESET, (int)spec->error_id, spec->title ? spec->title : "unknown error");
    printf(C_BLUE " --> " C_RESET "%s:%zu:%zu\n", filename, pos.line, pos.col);
    printf(C_BLUE "%*s|\n" C_RESET, gutter + 1, "");

    if (has_related) {
        SourcePos related_pos = resolve_pos(spec->related.line_range.file_id, source, spec->related.line_range.start);

        printf(C_BLUE "%*zu  |" C_RESET " %.*s\n", gutter, related_pos.line, (int)related_pos.line_len, related_pos.line_start);

        size_t underline_col = 1;
        size_t underline_len = related_pos.line_len;
    
        if (spec->related.highlight_range.start && spec->related.highlight_range.end) {
            SourcePos hp = resolve_pos(spec->related.highlight_range.file_id, source, spec->related.highlight_range.start);

            underline_col = hp.col;
            underline_len = (size_t)(spec->related.highlight_range.end - spec->related.highlight_range.start);
        }

        if (underline_len == 0) underline_len = 1;

        printf(C_BLUE "%*s  |" C_RESET " ", gutter, ""); repeat_char(' ', underline_col - 1);
        printf(C_DIM); repeat_char('-', underline_len);

        if (spec->related.annotation_prefix || has_string_view(spec->related.annotation_name)) {
            putchar(' ');
            print_related_annotation(&spec->related);
            printf(C_RESET "\n");
        } else printf(C_RESET "\n");

        printf(C_BLUE "%*s  |\n" C_RESET, gutter, "");
    }

    printf(C_BLUE "%*zu  |" C_RESET " %.*s\n", gutter, pos.line, (int)pos.line_len, pos.line_start);

    printf(C_BLUE "%*s  |" C_RESET " ", gutter, ""); repeat_char(' ', pos.col - 1);
    printf(C_BOLD_RED); repeat_char('^', range_len);
    if (spec->caret_prefix || has_string_view(spec->caret_name)) {
        putchar(' ');
        if (has_string_view(spec->caret_name)) {
            print_string_view(spec->caret_name);
            if (spec->caret_prefix) putchar(' ');
        }
        if (spec->caret_prefix) printf("%s", spec->caret_prefix);
        printf(C_RESET "\n");
    } else printf(C_RESET "\n");

    printf(C_BLUE "%*s  |\n" C_RESET, gutter, "");

    if (spec->help_label && has_help_content && has_related) {
        SourcePos related_pos = resolve_pos(spec->related.line_range.file_id, source, spec->related.line_range.start);

        printf(C_GREEN "help" C_RESET " - %s " C_GREEN "->" C_RESET "\n", spec->help_hint ? spec->help_hint : "");
        printf(C_BLUE "%*s  |\n" C_RESET, gutter, "");
        printf(C_BLUE "%*s  |" C_RESET " " C_RED "-" C_RESET " ", gutter, "");
        print_snippet_body(related_pos.line_start, related_pos.line_len);
        printf("\n");

        if (spec->related.replacement_to) {
            printf(C_BLUE "%*s  |" C_RESET " " C_GREEN "+" C_RESET " ", gutter, "");
            if (!print_replaced_line(
                    related_pos.line_start,
                    related_pos.line_len,
                    spec->related.replacement_from,
                    spec->related.replacement_to)) {
                print_snippet_body(related_pos.line_start, related_pos.line_len);
            }
            printf("\n");
        }

        printf(C_BLUE "%*s  |\n" C_RESET, gutter, "");
    }

    if (spec->note_label) {
        printf(C_YELLOW "note" C_RESET ": ");
        if (spec->note_detail)   printf("%s\n",       spec->note_detail);
        if (spec->note_detail_2) printf("      %s\n", spec->note_detail_2);
    }
}

const char* get_source(size_t file_id) { if (file_id < g_table.file_count) { return g_table.sources[file_id]; } return NULL; }
void checker_set_file_table(FileTable table) { g_table = table; }

static DiagnosticRenderSpec build_render_spec(CheckerErr err) {
    DiagnosticRenderSpec spec = {
        .error_id = err.tag,
        .title = "unknown error",
        .note_label = NULL,
    };

    switch (err.tag) {
        case Err_Tag_VNM: {
            spec.primary_range = err.data.vnm.range;
            spec.title = err.data.vnm.binding_kind.ptr && strcmp(err.data.vnm.binding_kind.ptr, "let") == 0 ? "pushing data to immutable variable" : "cannot assign to immutable binding";
            spec.related.enabled = err.data.vnm.decl_range.start && err.data.vnm.decl_range.end;
            spec.related.line_range = err.data.vnm.decl_range;
            spec.related.highlight_range = err.data.vnm.decl_name_range.start && err.data.vnm.decl_name_range.end ? err.data.vnm.decl_name_range : err.data.vnm.decl_range;
            spec.related.annotation_prefix = "declare variable";
            spec.related.annotation_name = err.data.vnm.var_name;
            spec.related.annotation_suffix = "as immutable";
            spec.help_label = "help";
            spec.help_hint = "change to var";
            spec.note_label = "note";
            spec.note_detail = "immutable is unlike mutable variables, cannot change their data after declaring it";
            spec.note_detail_2 = "immutable variables that doesn't use 'var'";
            spec.caret_name = err.data.vnm.var_name;
            spec.caret_prefix = "is immutable variable";

            if (spec.related.enabled && err.data.vnm.binding_kind.ptr &&
                strcmp(err.data.vnm.binding_kind.ptr, "let") == 0) {
                spec.related.replacement_from = "let";
                spec.related.replacement_to = "var";
            }
            break;
        }         
        case Err_Tag_UKT:
            spec.primary_range = err.data.ukt.range;
            spec.title = "cannot infer type";
            spec.caret_name = err.data.ukt.var_name;
            spec.caret_prefix = "type could not be inferred";
            spec.note_label = "note";
            spec.note_detail = "add an explicit type annotation";
            break;           
        case Err_Tag_Parse:
            spec.primary_range = err.data.parse.range;
            spec.title = err.data.parse.message ? err.data.parse.message : "syntax error";
            break;
        case Err_Tag_VMV:
            spec.primary_range = err.data.vmv.range;
            spec.title = "type mismatch";
            spec.caret_name = err.data.vmv.var_name;
            spec.caret_prefix = "has incompatible type";
            if (err.data.vmv.expected_type.ptr && err.data.vmv.actual_type.ptr) {
                spec.note_label = "note";
                spec.note_detail = "expected type and actual type differ";
            }
            break;
        case Err_Tag_VSF:
            spec.primary_range = err.data.vsf.range;
            spec.title = "unknown variable";
            spec.caret_name = err.data.vsf.var_name;
            spec.caret_prefix = "is not defined";
            break;
        case Err_Tag_VPT:
            spec.primary_range = err.data.vpt.range;
            break;
        case Err_Tag_TNF:
            spec.primary_range = err.data.tnf.range;
            break;
        case Err_Tag_TNC:
            spec.primary_range = err.data.tnc.range;
            break;
        case Err_Tag_OUD:
            spec.primary_range = err.data.oud.range;
            break;
        case Err_Tag_OMP:
            spec.primary_range = err.data.omp.range;
            break;
        case Err_Tag_OMM:
            spec.primary_range = err.data.omm.range;
            break;
        case Err_Tag_LHS:
            spec.primary_range = err.data.lhs.range;
            break;
        case Err_Tag_CVN:
            spec.primary_range = err.data.cvn.range;
            break;
        case Err_Tag_RDL:
            spec.primary_range = err.data.rdl.range;
            spec.title = "duplicate declaration";
            spec.caret_name = err.data.rdl.var_name;
            spec.caret_prefix = "is declared more than once";
            break;
        case Err_Tag_NBC:
            spec.primary_range = err.data.nbc.range;
            break;
        case Err_Tag_AIC:
            spec.primary_range = err.data.aic.range;
            break;
        case Err_Tag_DCB:
            spec.primary_range = err.data.dcb.range;
            break;
        case Err_Tag_TAU:
            spec.primary_range = err.data.tau.range;
            break;
        case Err_Tag_RNC:
            spec.primary_range = err.data.rnc.range;
            break;
        case Err_Tag_EMB:
            spec.primary_range = err.data.emb.range;
            break;
        case Err_Tag_DEC:
            spec.primary_range = err.data.dec.range;
            break;
        case Err_Tag_URC:
            spec.primary_range = err.data.urc.range;
            break;
        case Err_Tag_PTM:
            spec.primary_range = err.data.ptm.range;
            break;
        case Err_Tag_SFF:
            spec.primary_range = err.data.sff.range;
            break;
        case Err_Tag_WFC:
            spec.primary_range = err.data.wfc.range;
            break;
        case Err_Tag_DCP:
            spec.primary_range = err.data.dcp.range;
            break;
        case Err_Tag_MLM:
            spec.primary_range = err.data.mlm.range;
            break;
        case Err_Tag_MBM:
            spec.primary_range = err.data.mbm.range;
            break;
        case Err_Tag_NDF:
            spec.primary_range = err.data.ndf.range;
            break;
        case Err_Tag_WTC:
            spec.primary_range = err.data.emb.range;
            break;
        case Err_Tag_WLC:
            spec.primary_range = err.data.wlc.range;
            break;
        case Err_Tag_NIT:
            spec.primary_range = err.data.nit.range;
            break;
        case Err_Tag_ITR:
            spec.primary_range = err.data.itr.range;
            break;
        case Err_Tag_ITV:
            spec.primary_range = err.data.itv.range;
            break;
        case Err_Tag_BSV:
            spec.primary_range = err.data.bsv.range;
            break;
        case Err_Tag_ULV:
            spec.primary_range = err.data.ulv.range;
            spec.title = "unused variable";
            spec.caret_name = err.data.ulv.var_name;
            spec.caret_prefix = "is never used";
            break;
        case Err_Tag_DFN:
            spec.primary_range = err.data.dfn.range;
            break;
        case Err_Tag_UFT:
            spec.primary_range = err.data.uft.range;
            spec.title = "unknown field type";
            spec.caret_name = err.data.uft.field_name;
            spec.caret_prefix = "has unknown type";
            spec.note_label = "note";
            spec.note_detail = "the type of this field could not be resolved";
            break;
        case Err_Tag_VFT:
            spec.primary_range = err.data.vft.range;
            break;
        case Err_Tag_EST:
            spec.primary_range = err.data.est.range;
            break;
        case Err_Tag_DVN:
            spec.primary_range = err.data.dvn.range;
            break;
        case Err_Tag_DFV:
            spec.primary_range = err.data.dfv.range;
            break;
        case Err_Tag_EEN:
            spec.primary_range = err.data.een.range;
            break;
        case Err_Tag_SVE:
            spec.primary_range = err.data.sve.range;
            break;  
        case Err_Tag_GPU:
            spec.primary_range = err.data.gpu.range;
            break;
        case Err_Tag_NCB:
            spec.primary_range = err.data.ncb.range;
            break;
        case Err_Tag_MRT:
            spec.primary_range = err.data.mrt.range;
            break;
        case Err_Tag_TMI:
            spec.primary_range = err.data.tmi.range;
            break;
        default:
            printf("DEBUG: unhandled error tag = %d\n", (int)err.tag);
            spec.primary_range = err.range;
            break;
    }

    return spec;
}

void checker_err_push(CheckerErrList* list, CheckerErr err) {
    DiagnosticRenderSpec spec = build_render_spec(err);

    
    const char* source = get_source(spec.primary_range.file_id);
    report_error_visual(&spec, source);

    if (list->count >= list->cap) {
        list->cap = list->cap == 0 ? 8 : list->cap * 2;
        list->errors = realloc(list->errors, list->cap * sizeof(CheckerErr));
    }

    list->errors[list->count++] = err;
}
