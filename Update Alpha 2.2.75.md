Update Alpha 2.2.75 - update 8

# Summary
Updating register with optimizations and Improved register performance by approximately 30% in typical compiler workloads. on average for most usage. Fixing ton of bugs and using ARR instead of malloc for codegen and Lowering for safety and not causing crash on NULL. Adding alot new stuff in register for more easier usage and less bugs. Including full generic system with 'if Ok()/match Ok()' usage for struct/enums.

## Optimizations & Performance
- Improved register performance by ~30% on average.
- Reduced `register_get` calls by using cached register IDs.
- Added a global register hashmap for O(1) lookups by ID or name.
- Introduced arena allocation for register storage.

Optimizing the register for boosting the performance for 30% on average require full new system. Reducing usage of 'register_get' to lower as possible and using 'register's id' for better performance. Added a global register hashmap that stores every registered symbol. This eliminates repeated parent's scope traversal when looking up symbols by ID or name. to store all inserted elements without any scopes limits. This allow accesing all registered items without looping and searching the parents, and adding new helper functions for that kind of stuff like 'register_get_by_id' for getting using id and 'register_get_by_name' for getting using the name. Example usage:

```c
#include "register.h"

void main() {
    // call register using the id
    RegisterEntry* my_enum = register_get_by_id(10); // using id: 10
    RegisterEntry* my_enum_by_name = register_get_by_name(sv_from_range("...")); // make sure to change source Range into StringView

    // now i can use both!
}
```

Register/Compiler benchmarks. Compiler 9.25-remake VS 9.5 updates with performance in Registering. Including overall memory usage and performance and time taken for every operation. With overall compilion speed for different tests




### Register API

New helper functions:

- `register_get_by_id`
- `register_get_by_name`
- `register_get_function`
- `register_get_name`
- `register_get_id`

Added Few helper functions to increse performance and reduce any slow or not clean code. Added `register_get_function` used to get child's functions. Used for stuff like return stmts. Uses the child's id and return the function's id back. Including new other functions like `register_get_name` get the name by the id and return a StringView. And finally `register_get_id` get id using the name of any elemenet.

> **Note:** Added full arena system for allocations, boosting the registering speed!

## Safety & Bugs
Used full ARR instead of malloc in codegen and lowering ( ir.c ) for more safe usage, by preventing any NULL can cause any bugs or compiler to crash or memory data corrption. This is a safety checks. And Removing alot of NULL checks around of the compiler beacuse alot of parts now are stable and doesn't require any extra checks! This is increses performance by a bit but make the code acually more readable.

> **Note:** Fixing alot of bugs in whole compiler and deep testing it. Fixing linker problem to not cause any type of problems, like linking issues. Added Tests for register and fixing some memory leaks found.

## Features
Including alot of new features like generic/pad in if and match stmts. This allow you to do stuff like `case Ok(a)` or `case { a, b }` with struct and enum inside if stmt or match. Or Stuff like declaring a variable e.g `if var a = ...` or `if let a = ...` Example usage:

```vix
enum Option[T]:
    Some(T)
    None
end

func example(): Option[int]
    return Some(10)
end

func main()
    // usage for match
    match example():
        case Some(i) do print(i)
        case None do return
    end

    // usage for if
    if Some(i) = example() then
        return
    end

    // usage of variable in if stmt
    if var i = example() then
        return i
    end

    return
end
```
Including full generic support for all stmts ( `functions/enums/structers/classes` ) using `[Generic1, Generic2]`. And automaticlly make a new copy and delete the orginal ungenertic one using LLVM optimization system. Generic in classes are allowed to be used inside functions. Simple examples:

```vix
enum generic_enum[T]:
    Some(i: T)
    None
end

struct generic_struct[R, L]:
    left: L
    right: R
end

func my_generic_function[A, B](a: A, b: B): (A, B)
    return (a, b)
end

class my_class[A, B]()
    func my_function(): A // using class generic as the function's return type
        ...
    end
end

```

> **Note:** Usage for generic:
- Function call - `my_function[int, int]()`
- Class call - `my_class[int, int].my_function()`
- Enum call - `My_Enum[int].Variant`
- Struct call - `My_Struct[int] { fields... }`

Support enum/struct variant/fields calls in the return type e.g `return Ok("Something")` and `return { .a = "Something }` and for the return you can may set generic `my_function(): Result[str, int]`. Example

```vix
func my_function(): MyEnum[int]
    return Ok(1)
end
```

> Update Alpha 2.2.75 - Release Date: 6/30/2026