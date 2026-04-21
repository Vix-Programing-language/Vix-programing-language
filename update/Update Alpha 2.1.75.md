Update Alpha 2.1.75 - Update 4

## Summary
Updated the compiler with alot new features to imporve stablity of the overall, language. Adding also alot of optimizating by reducing the clone usage, this by adding a [StringView](https://vixlanguage/github.io/docs/stringview) and using functions instead of `memcpy/strup` Also Changing how lines/cons being stored by using [SourceRange](https://vixlanguage/github.io/docs/sourcerange) instead. Tests for parser and overall system and reducing bugs and crashes.

### Parser - Error handling & Feature
Added full error handling in the parser by connecting it to the main [Error reporter](https://vixlanguage/github.io/docs/error/parser). Using [error.c](src/helper/error.c) file. List of all functions used for error handling in the parser:

- parse_error < Report any error 
- parser_expect < Expect a token, if doesn't match automaticlly report it as a error
- parser_get_errors < Get all errors from the parser
- parser_reset_errors < Reset the errors ( free )

Errors be sent by the parser using mainly `parser_expect` or `parser_error`. It require kind, message and expected tag to report a error. Example usage:


```c
    // For parser_error:
    if (parser_current(self).tag != Equalss) {
        parser_error(self, Parser_Error_Unexpected, "Error! expected a 'equal'", Equalss);
    } else {
        parser_advance(self);
        var_value = parser_expr(self);
        has_value = true;
    }
    // For parser_expect
    parser_expect(self, Equalss); // Ask for the tag and 'self' doesn't require a error
```

> **Note:** Parser now uses full Array instead of fixed-array or any other aproutch.

### Typechecker - Error handling
Instead of using raw fields like `.line = stmt->data.locals.range.pos.line,` we now use SourceRange. Line and col be set automaticlly by the lexer

```cpp
    self->top.range.start = self->cur; // set a start
    self->top.range.file_id=self->file_id; // set a file_id

    self->top.range.end = self->cur; // set the end
```
Using the end/start and file id in the SourceRange. We able to track down the error line easily using error_report_preflix helper function.

```c

static void print_error_prefix(const FileManager* files, SourceRange range) {
    const ManagedFile* file = file_manager_get_const(files, range.file_id); // get the file, using the ID
    size_t line = 0;
    size_t col = 0;

    const char* path = file ? file->path : "<unknown>"; // If the file is unknow set '<unknow>
    if (!file_manager_get_location(files, range.file_id, range.start, &line, &col)) { // If location doesn't exists, set to 0
        line = 0;
        col = 0;
    }

    // Print the path/line and col

    printf("[%s:%zu:%zu]", path, line, col);
}
```

### Optimizations - StringView
Compiler now uses StringView instead of just const char* or sourcerange. This increse performance beacuse alot less cloning. Mostly in register when registering anything. Example:  `StringView key = string_view_from_range(stmt->data.locals.name);` this automaticlly set SourceRange into a usable StringView. By converting the `start and end` into a pointer and adding the length. Then usage can be so simple without any issues or cloning SourceRange just for typechecking. Example usage of **StringView**:

```c
bool register_local(Stmts* stmt, Register* reg, CheckerErrList* errors) {
    StringView key = string_view_from_range(stmt->data.locals.name); // Make stringView using a sourcerange
    RegisterEntry* existing = register_get(reg, key); // Still able to use the StringView. No cloning for SourceRange

    if (existing) {
        checker_err_push(errors, (CheckerErr){
            .tag      = Err_Tag_RDL,
            .data.rdl = {
                .range               = stmt->data.locals.range, // Take the sourceRange normally
                .var_name            = existing->name,
            }
        });
        return false;
    }

    Type t = resolve_type(stmt->data.locals.c_type, reg);
    if (t.tag == Type_Void) {
        char* name = null_term_view_alloc(key);
        checker_err_push(errors, (CheckerErr){
            .tag = Err_Tag_TNF,
            .data.tnf = {
                .range     = stmt->data.locals.range,
                .type_name = name,
            }
        });
        return false;
    }

    register_insert(reg, key, (RegisterEntry){
        .tag        = Reg_Local,
        .name       = NULL, 
        .type       = t,
        .data.local = { .type = t, .is_pub = stmt->data.locals.is_pub }
    });
    return true;
}
```

> Update Alpha 2.1.75 - Release Date: 4/21/2026