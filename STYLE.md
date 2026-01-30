# Vix Programming Language Style Guide

## Table of Contents
1. [Introduction](#introduction)
2. [Comments](#comments)
3. [Naming Conventions](#naming-conventions)
4. [Whitespace and Formatting](#whitespace-and-formatting)
5. [Control Flow](#control-flow)
6. [Functions](#functions)
7. [Best Practices Summary](#best-practices-summary)

---

## Introduction

This style guide defines the recommended coding conventions for the Vix programming language. Following these guidelines will help maintain consistency, readability, and maintainability across Vix codebases.

**Conventions Used:**
- ✅ **Good** - Recommended practice
- ⚠️ **Warning** - Discouraged but not forbidden
- ❌ **Bad** - Should be avoided

---

## Comments

Vix supports single-line comments using the `//` syntax.

### Syntax

```vix
// This is a single-line comment
/Comment/  // Alternative comment style
```

### Best Practices

✅ **Good:**
```vix
// Calculate user age based on birth year
var age = current_year - birth_year

// Process payment transaction
func process_payment(amount) then
    // Validate amount before processing
    if amount > 0 then
        return true
    end
end
```

**Guidelines:**
- Use comments to explain *why*, not *what* the code does
- Keep comments concise and relevant
- Update comments when code changes
- Avoid obvious comments

---

## Naming Conventions

Proper naming conventions improve code readability and help distinguish between different types of identifiers.

### Constants

Constants should use `UPPER_SNAKE_CASE`.

✅ **Good:**
```vix
const MAX_BUFFER_SIZE = 1024
const API_TIMEOUT = 30
const USER_NAME = "admin"
```

⚠️ **Warning (Avoid for regular variables):**
```vix
var USER_NAME = "john"  // This looks like a constant but isn't
```

### Static Variables

Static variables should use `UPPER_SNAKE_CASE`.

✅ **Good:**
```vix
static INSTANCE_COUNT = 0
static GLOBAL_CONFIG = {}
```

### Regular Variables

Regular variables should use `snake_case` (lowercase with underscores).

✅ **Good:**
```vix
var username = "john_doe"
var user_count = 42
var is_valid = true
var total_price = 99.99
```

⚠️ **Warning:**
```vix
var USER_NAME = "john"  // Looks like a constant
var userName = "john"   // camelCase not recommended
```

### Summary Table

| Type | Convention | Example |
|------|------------|---------|
| Constants | `UPPER_SNAKE_CASE` | `const MAX_SIZE = 100` |
| Static Variables | `UPPER_SNAKE_CASE` | `static COUNTER = 0` |
| Regular Variables | `snake_case` | `var user_name = "john"` |
| Functions | `snake_case` | `func calculate_total()` |

---

## Whitespace and Formatting

Consistent whitespace improves readability and prevents errors.

### Operators

Always use spaces around operators.

✅ **Good:**
```vix
x = 5
y = x + 10
result = a * b - c
is_equal = x == y
```

❌ **Bad:**
```vix
x=5          // No spaces around assignment
y=x+10       // Cramped operators
result=a*b-c // Hard to read
```

### Function Calls

Use consistent spacing in function calls.

✅ **Good:**
```vix
calculate(x, y, z)
print("Hello, World!")
result = max(10, 20)
```

### Commas and Separators

Use a space after commas, not before.

✅ **Good:**
```vix
func example(a, b, c) then
    return a + b + c
end

var list = [1, 2, 3, 4, 5]
```

❌ **Bad:**
```vix
func example(a,b,c) then  // No spaces after commas
    return a+b+c
end
```

---

## Control Flow

Proper formatting of control flow statements enhances readability.

### If Statements

Vix supports both single-line and multi-line `if` statements.

#### Single-Line Format

✅ **Good:**
```vix
if i == 50 then return 1 end
if is_valid then print("Valid") end
if x > 0 then y = x * 2 end
```

#### Multi-Line Format

✅ **Good:**
```vix
if i == 50 then
    return 1
end

if score >= 90 then
    grade = "A"
    print("Excellent!")
end

if user_authenticated then
    show_dashboard()
    log_activity()
end
```

❌ **Bad (Inconsistent Indentation):**
```vix
if i == 50 then
return 1    // Body not indented
end

if x > 0 then
        y = 10  // Over-indented
end
```

### Indentation Rules

- Use **4 spaces** for indentation (or consistent tabs)
- Align the statement body with the conditional
- Keep `end` keyword at the same level as `if`

✅ **Good:**
```vix
if condition then
    // 4 spaces indentation
    action()
end
```

### Nested Conditions

✅ **Good:**
```vix
if user_exists then
    if user_active then
        grant_access()
    end
end

if x > 0 then
    if y > 0 then
        result = x + y
    else
        result = x
    end
end
```

### Else Clauses

```vix
if temperature > 30 then
    print("Hot")
else
    print("Cool")
end
```

---

## Functions

Function definitions should follow consistent naming and formatting conventions.

### Naming

Functions should use `snake_case` (lowercase with underscores).

✅ **Good:**
```vix
func calculate_total() then
    // function body
end

func process_user_input(input) then
    // function body
end

func is_valid_email(email) then
    // function body
end
```

⚠️ **Warning (Avoid):**
```vix
func CALCULATE_TOTAL() then  // Looks like a constant
    // function body
end

func CalculateTotal() then  // PascalCase not recommended
    // function body
end
```

### Function Declaration Format

✅ **Good:**
```vix
func greet(name) then
    print("Hello, " + name)
end

func add(a, b) then
    return a + b
end

func process_data(data, options) then
    // Multi-line body
    var result = transform(data)
    validate(result, options)
    return result
end
```

### Function Calls

✅ **Good:**
```vix
result = calculate_total(items, tax_rate)
user = get_user_by_id(user_id)
print_report(data, format, include_header)
```

### Return Statements

✅ **Good:**
```vix
func get_discount(amount) then
    if amount > 100 then return 10 end
    return 0
end

func validate_input(input) then
    if input == null then
        return false
    end
    return true
end
```

---

## Best Practices Summary

### DO ✅

- **Use `UPPER_SNAKE_CASE`** for constants and static variables
- **Use `snake_case`** for regular variables and functions
- **Add spaces around operators** (`x = 5`, not `x=5`)
- **Indent code blocks consistently** (4 spaces recommended)
- **Write clear, meaningful names** (`user_count` not `uc`)
- **Keep single-line conditionals on one line** when simple
- **Use multi-line format** for complex conditionals
- **Add comments** to explain complex logic

### DON'T ❌

- **Don't use `UPPER_SNAKE_CASE` for regular variables** (looks like constants)
- **Don't omit spaces around operators** (`x=5` is hard to read)
- **Don't skip indentation** in multi-line blocks
- **Don't mix indentation styles** (spaces vs tabs)
- **Don't use `PascalCase` or `camelCase`** (stick to `snake_case`)
- **Don't write cryptic variable names** (`x1`, `tmp`, `data`)

---

## Code Examples

### ✅ Well-Formatted Code

```vix
// Configuration constants
const MAX_USERS = 100
const API_KEY = "abc123"
static INSTANCE_COUNT = 0

// Calculate user's total score
func calculate_score(base_points, bonus_points) then
    var total = base_points + bonus_points
    
    if total > 1000 then
        total = 1000  // Cap at maximum
    end
    
    return total
end

// Main program
var user_name = "Alice"
var user_score = calculate_score(850, 200)

if user_score >= 500 then
    print("High score achieved!")
end
```

### ❌ Poorly-Formatted Code

```vix
// Bad formatting example
const maxUsers=100  // No spaces
var USER_NAME="Alice"  // Wrong case for variable
static instance_count=0  // Wrong case for static

func CalculateScore(basePoints,bonusPoints) then  // Wrong case, no spaces
var total=basePoints+bonusPoints  // No spaces

if total>1000 then
total=1000  // Not indented
end

return total
end

var userName="Alice"  // camelCase
var UserScore=calculate_score(850,200)  // Mixed cases

if UserScore>=500 then print("High score!") end  // Inconsistent style
```

---

## Conclusion

Following this style guide will help you write clean, consistent, and maintainable Vix code. While some rules are flexible, consistency within a project or team is paramount.

**Remember:** 
- Readability matters
- Consistency is key
- When in doubt, favor clarity over brevity

Happy coding in Vix! 🚀
