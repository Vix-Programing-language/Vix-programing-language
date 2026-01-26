## What is the function?
The function is like recipe inside your prgrogram. It is a named block of code performs a specific task. Instead of writing the same logic over and over again, you can write once. Give it the name and then "call" it that name whenever and whereever you need to use it

- **Information** There is 3 type of function "public/private/unsafe" they have the same task. Same work just one private for the current **file** and one public to whole files. Unsafe function are explained on "memory safety" documation. **Function main()** is overall optional but if you going to use it then it's will be as first function will be called automaticly no need for using "main()"

```ruby
    func function_name(parameter_one: Type, paramete_two: Type): ReturnType
        # your code goes here
        return something # return anything you need
    end

    # call the function:
    function_name(Type, Type)
```
## Deep explainion:
- Parameters are who hold the input
    - Example:
    ```ruby

        func example_function(input1: str, input2: int)
                              |^^^^^^^^^^^^^^^^^^^^^^^
                              |- # parameter 1 allow only to "str" to pass
                              |- # parameter 2 allow only to "int" to pass
        end # close the function block with "end" 
    ```
    - Example of correct function call:

    ```ruby
        # this a correct example:
        example_function("Hello", 67)
                        |^^^^^^^^^^^
                        |- # "Hello" is str ( string ) so it's allowed
                        |- # 67 is int ( number without "." ) so it's allowed
    ```

    - Example of incorrect function call:
    ```ruby
        # this wrong:
          example_function(103, "Hello")
                        |^^^^^^^^^^^
                        |- parameter 1: in the function it's allowing in parameter 1 only "str" but here it's passing "int", Error! this wrong
                        |- parameter 2: in the function it's allowing in parameter 2 only "int" but here it's passing "str", Error! this wrong
    ```
    - Here will be wrong and compiler will return an error:
    ```
        [Error]: Wrong param:
            |
        1   | func example_function(input1: str, input2: int)
            |                       ^^^^^^^^^^^^^^^^^^^^^^^^
            |
        3   | example_function(103, "Hello")
            | ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
            -> Information:
                | Calling a function with wrong input:
                    func example_function(input1: str, input2: int)
                | the parameter contain in input1: str and in input2: int. The input is wrong:
                | example_function(103, "Hello")
                | Calling the function with "(int, str)" correct is "(str, int)"
                -> Help:
                    example_function("Hello", 103)
                -> Note:
                    By modifing example_function("Hello", 103) make sure that "input1/input2" inside the function are fixed too.
                
    ```
- Vix is a memory safe programing language that what make it have so many types of "parameter":
    - Owned: Function take ownership of the data
    - Immutable: Read only and take the ownership
    - Mutable: Can be changed and mutable only inside the function scope ( block )
    - Borrow: Can borrow the information but will give it back not going to take whole ownership but immutable
    - Mutable Borrow: Can borrow the information but will give it back not going to take whole ownership but mutable
    - Reference: You're passing a immutable data can be read only without taking the ownership
    - Mutable Reference: You're passing a mutable reference and can change the data from ouside the function too
        - Example & explaion about every types of paramater:
            - Owned: Function take the ownership of the data mean the data are owned by the function that means function can only use the data beacuse it took the ownership of it. Inside the function as immutable mean inside the function scope ( block ) the data all just read only can't be modified. Example:
        ```ruby
            # will take the ownership of the data
            func example(input1: str)
                print(input1)

                input1 += "world" # Error: input1 is immutable input.
            end

            data: str = "Hello world"

            example(data) # call the function and send "data" var that make it take the whole ownership of the variable.

            print(data) # Error "data" are owned by the "example" function.  
        ```
        - Mutable: Function take the ownership of the data mean the data are owned by the function that means function can only use the data beacuse it took the ownership of it. Inside the function as mutable mean inside the function scope ( block ) the data all can be modified.
        ```ruby
            # mutable "mut" mean "input1" is now mutable
            func example(mut input1: str):
                input1 += "world" # will be modifed to "Hello world world"

                print(input1)
            end

            data: str = "Hello world"

            example(data) # call the function and send "data" var that make it take the whole ownership of the variable.

            print(data) # Error "data" are owned by the "example" function.  
        ```

        - Reference: In reference function don't take the ownership but it take the data without own it. In immutable reference it's immutable data. Mean can't be changed. But unlike the "borrow" this simple take a quick reads to the input
        ```ruby
            # Don't will take the ownership of the data
            func example(input1: ref str)
                print(input1)

                input1 += "world" # Error: input1 is immutable input.
            end

            data: str = "Hello world"

            example(data) # call the function and send "data" var that make it just take a look at the data without taking any ownership

            print(data) # will print it without any problems.
        ```
         - Mutable Reference: In reference function don't take the ownership but it take the data without own it. In mutable reference it's immutable data. Mean can change the data. 
         - **Information:** Mutable reference can change the data that from outside the function too.
         ```ruby
            # Don't will take the ownership of the data .
            func example(input1: mut ref str)
                input1 += "world" # will add it without a problem

                print(input1) # will print "hello world world"
            end

            data: str = "Hello world"

            example(data) # call the function and send "data" var that make it just take a look at the data without taking any ownership

            print(data) # will print "hello world" without any problems
         ```
        - Borrow: In borrow the function borrow the information but promise to give it back without taking any ownership of it.
        ```ruby
            # Don't will take the ownership of the data. But will borrow it
            func example(input1: brw str) # or "&str"
                print(input1) 

                input1 += "world" # Error: Borrow data are immutable
            end

            data: str = "Hello world"

            example(data) # call the function and send "data" var that make it just take a look at the data without taking any ownership

            print(data) # will print "hello world" without any problems
         ```
        - Mutable Borrow: In borrow the function borrow the information but promise to give it back without taking any ownership of it. But can change the data too
        ```ruby
            # Don't will take the ownership of the data. But will borrow it
            func example(input1: brw str) # or "&str"
                print(input1) 

                input1 += "world" # will change it.
            end

            data: str = "Hello world"

            example(data) # call the function and send "data" var that make it just take a look at the data without taking any ownership

            print(data) # will print "hello world world" without any problems
         ```


- Return Type. Return type is what your function allow to return:
    - Example:
        ```ruby
            func example(): int32 # return type require to add ":". Example here return type is "int32"
                return 10 # return 10 is correct
                return "Hello world" # wrong return type is int not str
            end
        ```
    - with this "int32" return type you can return only numbers
    - **Information** By defualt all functions have "int32" return type. If you not going to write return type then it's will be locked to "int32".
        - If you did array or tuple in a return automaticly compiler will change return type to array/tuple exampe:
        ```ruby
            func example()
                return (10, 50)
            end
        ```
        - Automaticlly will compile to:
        ```ruby
            func example(): []
                return (10, 50)
            end
        ```
    - Return type can be used with "Result" buildin function and it's so recommanded.
    - You need to use "Ok()" function to return if your using "Result" as return type
    ```ruby
        func example(): Result[int32, str]
            return Ok((10, "Hello world"))
        end
    ```
    - Recommanded to use "Option[]" for int too but it's optional. Use if the data have possibilty of return "None".

- Public/Private functions are different things. Public function is the function visible for all other scripts inside the src example:
    - main.vix
    ```ruby
        public func example(i: int32): int
            return i
        end
    ```
    - another_script.vix
    ```ruby
        import example from "src/main.vix"

        example(10)
    ```
    - In **another_script.vix** the function is simple visible using "import". But in private function this not possible:
    - another_main.vix
    ```ruby
        func example(i: int32): int
            return i
        end
    ```
    - another_script.vix
    ```ruby
        import example from "src/another_main.vix"
        ----------------------------------------- # Function is private.
        example(10)
        ---------- # Error unknow function.
    ```
