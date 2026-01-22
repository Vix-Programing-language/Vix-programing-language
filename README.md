# Vix Programming Language

A fast, memory-safe programming language designed for both low-level and high-level applications. Vix combines the speed of C with the safety of Rust, while remaining simple and easy to learn.

## How to install & Use
> From website: Go to [offical website](https://vixlanguage.github.io/install) and then select your OS and press **Install**.
To make a new project in vix:
```toml
-- project_folder
|- myfile.vix
|- config.toml # optional
```
To compile your file:
```powershell
vix myfile.vix -o myapplication.exe
```

## ✨ Features

- **Fast**: Performance comparable to C/C++ using Clang compiler
- **Memory Safe**: Built in safety without garbage collection overhead and using borrow checker and ownership system
- **Simple Syntax**: Easy to read and write, quick to learn and develop with. No using of "{}" only "end"
- **Flexible**: Write low level system code or high level applications fast and easily
- **Error handling & Help**: Easy to read & know the issues from the error msg

## Example helpful message:
```toml
[Warning]: Warning, Unexpected type:
| main.vix:10
|
| create input = input("ask me something")
| ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
| > Unkowen type cannot be knowen in compilion time
|--------------------------------------------------
  |
  |-> help:
|
| create input: Type = input("ask me something")
|-> Example types:
| "word" -> str/String
| number -> int/float for 3.14
| bool -> true/false
| ... learn more about types: https://vixlanguage.github.io/help/types.html
```
## 🚀 Quick Hello world
```vix
func main()
  print("Hello, world")
end
```

## > Status

**Alpha** - Active development. More features coming soon!

## 💬 Community

Join our Discord for help and updates: https://discord.gg/CAemjRc4ya

## 🎯 Why Vix?

## 🎯 Language Comparison

## 🎯 Language Comparison

| Feature | Python | Lua | Rust | Zig | Nim | C | Vix |
|---------|--------|-----|------|-----|-----|---|-----|
| Speed | ❌ Slow | ⚠️ Medium | ✅ Fast | ✅ Fast | ✅ Fast | ✅ Fast | ✅ Fast |
| Memory Safety | ⚠️ GC | ⚠️ GC | ✅ Borrow Checker | ⚠️ Manual | ⚠️ GC | ❌ Manual | ✅ Safe |
| Easy to Learn | ✅ Simple | ✅ Simple | ❌ Complex | ⚠️ Medium | ⚠️ Medium | ⚠️ Medium | ✅ Simple |
| Development Speed | ✅ Fast | ✅ Fast | ⚠️ Slow | ⚠️ Medium | ✅ Fast | ❌ Slow | ✅ Fast |
| Compile Time | N/A | N/A | ❌ Slow | ✅ Fast | ⚠️ Medium | ✅ Fast | ✅ Fast |
| Low-Level Control | ❌ No | ❌ No | ✅ Yes | ✅ Yes | ⚠️ Limited | ✅ Yes | ✅ Yes |
| High-Level Features | ✅ Rich | ✅ Yes | ⚠️ Medium | ⚠️ Limited | ✅ Yes | ❌ No | ✅ Rich |
---

**Built for developers who want speed without complexity.**
