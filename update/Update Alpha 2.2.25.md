Update Alpha 2.2.25 - update 6

## Summary
Added full [footprint]() ( cache ) system to prevent low performance of recompiling everything that has been unchanged. This optimization be done by storing all unchanged elements into '.uint/.pack' files in the footprint folder. With 'config.toml' has been added file used for configering and customizating the compiler. Also added a secret '.project' file that store informations about the footprint location and default config file settings.

### Footprint - Caching system
[Footprint]() system is used for overall storing already-compiled data e.g files that has never being changed, functions that haven't been touched yet, libraries that already compiled etc.. This what makes footprint system boost the compiler's performance so much mostly in mid or large builds, this makes vix compiler ideal for it's compiling times.

Footprint system works by storing 2 different type of files in a folders inside them a folder presenting your build. First type of files is '.pack' contains chuncks of data like funcitons/files etc. Unlike pack files, uint files store a single element mostly be `ExprStmt or stmt inside a body`. Multi uint files generate a single pack file. Mostly with limited amount of data can be stored into all these uint until it become a single pack. 

```vix
func main() // this overall '.pack' file
    var a: int = 10 // this a .uint file
end
```

In the next compiling, compiler automaticlly check all footprint files that already turned into a binary and set everything into structers, exactly like config.toml systems. Using binary also boost up performance or read and writing instead of raw text, it uses metadata files. These all uses 'flatcc' and '[dir](include/third-party/dir/dir.h) ( this library made by the vix developement team )'.

### Config - Config.toml
Config.toml files are file that used for full customization over the compiler from changing and adding libraries to modifing optimizations levels and linker commands. Alot of coming commands are also gonna be added too. There is also tables in toml like '[Information]' as example

```toml
[Information]
name = "my_build"
version = "2.2.25"
```

Toml library has been used is [toml99](include/third-party/toml/toml.h) For all configs check out our docs: [configs](https://vixlanguage.github.io/docs/configs).

> Update Alpha 2.2.25 - Release Date: 5/19/2026