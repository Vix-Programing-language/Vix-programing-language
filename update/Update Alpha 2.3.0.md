Update Alpha 2.3.0 - update 9

# Summary
Including new syntaxs and features to the compiler. Added new mising features and changed how some features work. New APIs in the register and depth tracking. Cleaning up the parser lowering lines from 3000~ to 2500~ lines. Changing how parser works. Adding new missing features & operations to the codegen. Adding alot of tests for indexs/arrays

## Syntaxs & Features
### New syntaxs
Changed syntax for both fixed-array and Generic for more readable and understood design. Lowering also chances of parser get confused between generic and other stuff like fixed-array. Changes are 'func some_function[T]()' to new simpiler and more understood design 'func some_function<T>()'. For fixed-array used more syntax to lower confusion between indexs and fixed-array. From 'Type[count][dem]' to '[type, count, dem]'

- For generic: 'func some_function[T]' to 'func some_function<T>'
- For array: 'type[count][dem]' to '[type, count, dem]'

> **Examples:**
```vix
func my_function<T>(a: T, b: T): T
                --- new generic syntax
    return a + b
end

func main()
    var a: [int, 20] = []
            |--  -- count
            | type
    for i in a do
        print(my_function<T>(a, 10))
    end
end
```
> **Note:** New generic syntax is made for all the Stmts and Exprs all together.
---
### New functionality
#### Self functionality
Changed how self works by making it automaticlly fatch the class name and insert it into it's type with adding it in the function's first param automaticlly. This be done by the register not the parser anymore for more readable version.

After insert the method. Automaticlly loop throught the param checking for self and insert the type in the self.
```c
    for (size_t j = 0; j < params_count; j++) { // Loop throught param
        if (!range_eq(custom_params[j].name, "self")) continue; // If it's not a self, continue ( skip )
        custom_params[j].type = (Type){ // If it's a self, automaticlly set the self in
            .tag = Type_Custom,
            .data.custom = { 
                .name = c.name, // class name ( type )
                ...
            }
        };
    }
```

#### Missing codegen features
Supporting alot of features in codegen. E.g Array, binop was not supported in codegen, this has been fixed and now all will be generated successfully. Automaticlly managing types in the codegen by changing this system at coming updates to the register side!

```c
Parser -> AST -> Register -> IR -> Codegen
                                   ------- Array and some binop was missing
```

Also changed how array works by not only using the type only for generating correct llvm IR code. The array required to be generated same type for the expr and type also. This in vix be done by register. Automaticlly featch the array type and expr from the variable and set 3 informations in the expr array: `element, type, empty`. Empty is the empty spots on the array. For codegen to make correct IR it require to make a empty slots in the array example:

```llvm
define void @main() {
entry:
  %shift = alloca [256 x i32], align 4 ; generating an array inlcuding to the type and element count
  store [256 x i32] zeroinitializer, ptr %shift, align 4 ; if all slots are empty. Automaticlly add 'zeroinitializer' otherwise set slots that used to corect type and expr in. And add 'type default' for others example:

  store [256 x i32] [i32 1, i32 2, i32 0, ... repeat 'i32 0' for all other slots]
}
```

> **Warning:** This is a alpha-test version. Please don't use this version of the system. It's under testing and may not be stable. Or missing from other types of variables that still has not been tested!

Binop was missing some operations has been added also adding operations for indexs and arrays too. Including tests to pressure the compiler and find any crashes.

Improving the index crashes by changing it from using pointers to full pure ids. Then using `register_from_global` to search the global table instead of a current scope. Using global and stored IDs to get the entry safely without issues.

```c
    RegisterEntry* left_entry  = register_from_global(entry->data.expr_binary_op.left_id);
    RegisterEntry* right_entry = register_from_global(entry->data.expr_binary_op.right_id);
```

> **Note:** Informations & list of everything recored in tests before fixing and after fixing. Havely tested with 'bioup.vix'. Using error messages from the codegen to know the issues and fix them directly

| Tests | Crash | Errors | Fail Rate | Fail rate after fixes |
|-------|-------|--------|-----------|-----------------------|
| Binop |   0   |   20   |    70%    |  Errors  |  Fail rate |  
| index |   3   |   5    |    40%    |    0     |     0%     |
|       |       |        |           |    0     |     0%     |

### New Features
#### Chain of fields & methods
Added full method chains and methods to the syntax and automaticlly set it's functionality in the register. Example syntax `self.field1.field2.some_function()`. This being done by the register by smart system. Where the last field's struct being tracked and set to `structer.field.method()` directly example:

```vix
struct Example1
    b: int
end

struct Example2
    a: Example1
end

struct Example3
    a: Example2
end


ex3.a.a.b = 10
// Find final field 'ex3.a.a.b' is 'b'. Find struct pointing to b by scanning 'a -> a -> struct'
// Lower it as:
ex1.b = 10
```
#### Register Depth system
Register has gotten new apis to be used by the compilers to track features and fixing some credical bugs. Called depth system where everything be tracked by depth automaticlly using 'register_insert' that cals 'depth_map_insert' Inserting in a hashmap `[id, depth, entry, [orginal_depth, id, entry]]`. Everything be tracked using a id and entry of the scope. With orginal_scope is the function that scope started on example:

```c
func my_function()
    scope
        scope
            scope
                var a: int = 10 // depth: 3, id: 5, entry: ptr, orginal_depth: entry to the function 'my_function' 
            end
        end
    end
end
```

Using ids/entries it's possible to get everything automaticlly using the APis that already register has to get everything. These are core functions can be called using APIs:

- `depth_map_init` > Start the whole depth system - important to call first before doing anything, otherwise crash!
- `depth_map_insert` > insert new depth including to param of `uint32_t id, Register* original_scope`
- `depth_map_lookup` > search the whole hashmap for the depthinfo using 'id'
- `calculate_register_depth` > calculate the depth. Need Register and send back uint32_t for the depth

These are used functions for everything. No need to code just call them:

- `depth_from_id` > require id to give back the depth using 'uint32_t' for calling and output
- `depth_from_scope` > require entry to give the depth using `Register*` for entry and uint32 output for the depth
- `find_entry_by_id` > require id to give the entry using `RegisterEntry`
- `find_entry_by_scope` > require scope ( reg using Register* ) to give `RegisterEntry*`
- `find_original_parent_scope` > find orginal parent scope using Register ( reg )
> **Note:** APIs may go public with dynamic library files soon. Can be used by any compilers and completely open source to be used.

## Bug fixed
Found 3 bugs has been fixed successfully. 1 credical. This fixes alot of problems and improves how the register systems works! Bug ( credical ). While can't have anything in it's scope this has been fixed by adding `child_reg` directly to the while scope. Same deal with for loop.


> Update Alpha 2.3.0 - Release Date: 7/21/2026