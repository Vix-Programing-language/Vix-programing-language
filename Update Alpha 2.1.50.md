Update Alpha 2.1.50 - update 3

## Summary
Update the compiler with alot new stuff. Including file managing/lines/cons tracking for error handling also adding a registering system adding everything into it's own hashmap using [khashl](https://github.com/attractivechaos/klib/tree/master) for so good performance. Adding full typechecker system. 

### File manager - cons/lines/files
Added full system that gives every file a specifc id connecting to it and and storing lines and cons for error handling too using FileManager structer. Fields like path_to_id/slots and files storing all informations required for tracking lines/files and cons. Storing everything into SourceRange automaticlly for the typechecker usage.

```c
typedef struct {
    FilePathMap* path_to_id;
    ARR(ManagedFile) slots;
    ARR(FileId) files;
} FileManager;
```
Also including alot new APIs ( functions ) to call in FileManager to help tracking anything required. Here a list of all functions:

- file_manager_new
- file_manager_has
- file_manager_new
- file_manager_has
- file_manager_free
- file_manager_add
- file_manager_get
- file_manager_get_const
- file_manager_get_location
- file_manager_set_line_starts

### Register & Typechecker
Register is a tracking hashmap system that assign for everything a specfic ID. Everything from the AST like a function be checked and registered with a id and a specifc scope! A Scope asigned by a entry aka a pointer. And contain also children like param/returntype etc... all assigend with a id/key. Everything be stored with a specifc tag like `Reg_Function`.

Typechecker also be done in reistering time and return a signel to the register to not continue compiling after registering everything. After register done all errors automaticlly be printed by the main file.

- First checking if everything is good by the typechecker system
```c
    if (register_get(reg, key)) { // If it already exists then
        checker_err_push(errors, (CheckerErr){ // Push a error
            .tag      = Err_Tag_RDL, // Deplcation issue
            .data.rdl = { //
                .file     = stmt->data.lets.range.pos.file, // Get the file
                .line     = stmt->data.lets.range.pos.line, // Get the line
                .col      = stmt->data.lets.range.pos.col, // Get the col
                .var_name = key, // Get the variale name
            }
        });
        free(key); return false; // Return false to the register telling it to stop compiling beacuse of an issue
    }
```
- Second assign the type by changing from SourceRange to a Type using `resolve_type`
```c
Type resolve_type(SourceRange r, Register* reg) {
    size_t len = r.end - r.start; 
    // Using memcmp automaticlly check if it match i32 then return Type_Int with 32 bits. This be done to all other types.
    if (len == 3 && memcmp(r.start, "i32",  3) == 0) return (Type){ .tag = Type_Int, .data.int_t.bits = 32 };
```
- Third if everything is alright. Register everything.

```c
    register_insert(reg, key, (RegisterEntry){ // the Scope aka 'reg' 
        .tag = Reg_Let, // Tag
        .name = key, // Name
        .type = t, // Type
        .data.let = { .type = t, .mode = stmt->data.lets.mode }
    });
```
#### Typechecker reports
Typechecker automaticlly uses a `CheckerErrTag` enum to set the kind of the error example Err_Tag_VMV presient a specifc type of errors. Structer of this specifc error tag track file/line/col and informations like for this error it's var_name/expected_type and actual_type presinting a error message with this specifc informations. All set at [type.c](src/register/checker/type.c) for advance errors and [register.c](src/register/checker/register.c) for specifc errors can be catched there

```c
typedef struct {
    const char* file; // file
    size_t line; // line
    size_t col; //  col
    const char* var_name; // variable name 
    const char* expected_type; // expected type
    const char* actual_type; // actual type
} Err_VMV;
```

### Main - compiler starter file
Added `main.c` file for starting the compiler with a temp compiling command `vix run yourfile.vix`. Also with a debugger of AST for debugging! With full managing of the compiler automaticlly setting everything for a smooth compilion

> Update Alpha 2.1.50 - Release Date: 4/14/2026