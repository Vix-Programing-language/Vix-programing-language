Update Alpha 2.2.50 - update 7

## Summary

Update 9.25 is a full remake of the MIT system, covering the **register**, **lower**, and **codegen** stages. This rewrite takes a fundamentally different approach to how the compiler tracks identity and scope, redefining the core of the compiler.

## Register System

The register system now assigns every registered AST node a unique, monotonically increasing `EntityID` so it can be tracked and retrieved later. Registration is driven by walking the AST: `register_stmt` and `register_expr` recursively visit every node, calling `register_insert` (or `register_insert_child`) to record it.

Each call to `register_insert` does two things:

1. Assigns a fresh id by reading and incrementing the shared counter (`reg->counter->next_id++`).
2. Inserts the entry into the current scope's hashmap. Once under its declared name (if it has one), and once under its id (stringified), so the entry can be looked up either by name or by id later.

```
[AST] -----> [register_stmt / register_expr] -----> [register_insert]
                                                       |--> assign id (counter++)
                                                       |--> hashmap insert: by name
                                                       |--> hashmap insert: by id
```

### Parent/child scoping

Scoping is handled by a tree of `Register` structs, not by pointers between individual entries. Each `Register` is its own hashmap, and `make_child(reg)` creates a new child `Register` whose `.parent` field points back to the scope that created it. Constructs that introduce a new scope functions, `if`/`while`/`for` bodies, `extern` blocks, and similar call `make_child` to build that scope, register everything that lives inside it (parameters, body statements, etc.) into the child, and then store a pointer to that child scope on their own entry (in a `child_reg`-style field, named differently depending on the construct e.g. `then_child`, `body_child`).

```
register_function(reg, stmt)
 ├── child = make_child(reg) // new scope, child.parent = reg
 ├── ...register params and body INTO child...
 └── register_insert(reg, entry { child_reg: child }) // entry now points down to child
```

Two distinct, independent links make up the hierarchy:

- **Scope → scope (upward):** every child `Register` holds a `.parent` pointer to the scope it was created from. `register_get` (lookup by name) uses this to walk upward through enclosing scopes until it finds a match or runs out of parents.
- **Entry → scope (downward):** an entry whose construct introduces scope stores a pointer to that child scope. `register_get_child` follows this pointer (after first resolving an entry by id) to descend into it.

There is no entry-to-entry parent link, and ids do not carry any information about which scope they belong to they're assigned from one global counter shared across every scope in the tree. To look something up by id, you need a handle to the specific scope it was registered into; `register_get_by_id` does not search through child or parent scopes.

### Helper functions

- `register_get` - look up an entry by name, searching the current scope and walking up through parent scopes until found.
- `register_get_by_id` - look up an entry by id within a single scope (no parent walking).
- `register_get_child` - resolve an entry's id to the child scope it owns, if its tag has one.
- `register_body` - register every statement in a list by calling `register_stmt` on each.

## Lowering / IR System

Lowering builds on the register system rather than introducing a hashmap of its own. `lower_stmt` and `lower_expr` walk the registered entries and use the register's helper functions primarily `register_get_by_id` and `register_get_child` to retrieve entries and descend into their child scopes as needed.

```
[AST] -----> [Register] ------> [IR]
```

```c
IR_Stmts* lower_something(Register* reg, uint32_t id) {
    RegisterEntry* entry = register_get_by_id(reg, id);   // look up the entry by id
    Register* child = register_get_child(reg, id);        // resolve its child scope, if any
    // ...
}
```

## Notes / Open Items

- Verify whether a separate hashmap or tracking structure exists anywhere in the lowering/codegen path beyond what's described above not confirmed either way in this pass.
- Confirm naming consistency for child-scope fields (`child_reg`, `then_child`, `body_child`, etc.) across entry types, since downstream code (like `register_get_child`) has to switch on tag to know which field to read.

> Update Alpha 2.2.50 - Release Date: 6/9/2026