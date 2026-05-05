#include "ir.h"
#include "import.h"
#include "register.h"

RegisterEntry* register_get(Register* reg, StringView name);

Type ir_type(const Type* t, Register* reg) {
    if (!t) return (Type){ .tag = Type_Void };

    if (t->tag == Type_Custom) {
        StringView key = {
            .ptr = t->data.custom.name.start,
            .len = (size_t)(t->data.custom.name.end - t->data.custom.name.start)
        };
        RegisterEntry* entry = register_get(reg, key);
        return entry ? entry->type : (Type){ .tag = Type_Void };
    }

    return *t;
}