Update Alpha 2.1.25 - update 2

## Summary
Updated the compiler with cleaner code and new functions to the parser. Also adding a new macros designed and made by the manager [Neva](https://github.com/nevakrien) for array and new checked allocating functions too. This changes makes the compiler alot usable for parsering and tokenizing.

### Parser - changes
Added new functions that helps the parsering. List of all functions are used/Added:
- parser_new(LexerToken*, counter) < Start the parser 
- parser_advance(self) < Advancing exactly 1 token
- parser_current(self) < Returning the current token
- parser_peek(self) < Peeking the next token
- parser_free(self) < Free everything in the parser

### Array - import.h
Array library made for auto dynamic allocating also with checking for any `NULL` may be caused by mallocating fail for a reason may happen like out-of-memory as an example. These functions are `checked_malloc/checked_realloc`.

```c
static inline void* checked_malloc(size_t size) {
    void* ptr = malloc(size);
    assert(ptr != NULL);
    return ptr;
}

static inline void* checked_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    assert(new_ptr != NULL);
    return new_ptr;
}
```
> Also with alot of helper macros. List of all functions and macros:
- ARR(type) < Make a array with a specifc type. Structer are also works with it
- ARR_PUSH(arr, x) < Push anything to a specifc array, automaticlly be reallocate it self 
- ARR_EXTEND(arr, src, x) < Extend the array ( aka allocating ) a specifc space
- ARR_POP(arr) < Pop any array
- ARR_PEEK(arr) < Peek at any array
- ARR_AT(arr, i) < Getting an index of any position in the array

> Update Alpha 2.1.25 - Release date: 4/8/2026