Update Alpha 2.2.0 - update 5

## Summary
Updated the compiler with alot more optimizations and mostly reduice cloning for performance. By removing 'const char*' and usign pure StringView in the entire compiler and adding `sv_from_cstr` helper function to lower a c string into a pure stringView. Also reduice usage of allocating using `null_term_view_alloc`. Register uses pure StringView for registering, also this for all APIs. Set typechecker into files presienting every use like `stmt/expr/operations` and added alot of helper functions also in [helper.c](src/checker/helper.c). Added Full IR support for lowering part

### Optimizations - Full StringView
Compiler now uses a full `StringView` adding another layer of optimization by reducing cloning. For using `const char*` this reduice both cloneing and allocating using `null_term_view_alloc`. we made a new function is `sv_from_cstr` that automaticlly convert one into a StringView.

```c
StringView sv_from_cstr(const char* s) {
    return (StringView){ 
        .ptr = s,  // Set the const char* as a ptr
        .len = s ? strlen(s) : 0  // Get the length
    };
}
```

Added new helper functions in [helper.c](src/checker/helper.c) for StringView and SourceRange too, Aslo functions to compare char*. Here list of all new helper functions.

- sv_equal < Compare between 2 string view and return bool 
- str_equal < Compare between 2 const char* and return bool
- range_eq < Compare between 2 SourceRange and return bool
- range_eq_sv < Compare SourceRange to a StringView and return bool

- string < Make a new StringView 
- string_range < Make a new SourceRange

### Lowering IR
IR now lower from register into IR codegen can use to compile using LLVM Backended compiler. IR loop throught the register checking for the tag of every element. After that it lower_expr/lower_stmt everything and turning it into a pure pointer. After that it ARR_PUSH using the IR_EXPR/IR_Stmt example usage:

```c
    IR_Expr *cond = lower_expr_alloc(&s->data.ifs.cond, reg); // first expr using allocating

    ARR(IR_Stmt) then_arr = {0};
    lower_body(s->data.ifs.body, s->data.ifs.body_count, reg, &then_arr, fn_ret); // lower a body using lower_stmt

    ARR(IR_Stmt) else_arr = {0};
    lower_body(s->data.ifs.else_body, s->data.ifs.else_body_count, reg, &else_arr, fn_ret);

    return (IR_Stmt){
        .tag = IR_Stmt_If, // Set the tag
        .origin = s->data.ifs.range, // set the range ( source range )
        .data.if_ = { // lower the conditon
            /* All other stuff be lowred */
            .cond = cond,
            .guard_pattern = s->data.ifs.guard_pattern,
            .body = then_arr.data,
            .body_count = then_arr.len,
            .else_body = else_arr.data,
            .else_body_count = else_arr.len,
        },
    };
```

> Update Alpha 2.2.0 - Release Date: 4/28/2026