### What is struct?
Struct is a way to group related data together under one name. Think of it as container that contains mutltiple pices of information about something. Each piece of information isndie the struct called a **filed**

- **Information** There is 3 type of function "public/private/Mutable" they have the same task. Same work just one private for the current **file** and one public to whole files. 

```ruby
    struct Example:
        mut filed1 = int32
        mut filed2 = str
    end

    example_struct: = Example(filed1: 0, filed2: "")
    example_struct.filed1 = "Hello world"
    example_struct.filed2 = 100
```

## Deep explainion:
- Fileds who hold the data ( information about something ) example:
    ```ruby
        struct Example:
            mut filed1 = int32 # Here there is filed1 as int32 mean allow storing only number ( not "." float ) data
        end
        
        # make them a variable for using it.
        # by defulat now filed1 contain "0"
        example_struct = Example(filed1: 0)

        # Accessing filed1 and making it contain "100"
        example_struct.filed1 = 100 
    ```
- Instead of having:
    ```ruby
        title = "The hobbit" 
        author = "J.R.R. Tolkien"
        pages = 310
    ```
    - Struct Group them all togather to "Book" group ( struct ).
    ```ruby
        struct Book:
            title = str
            aithor = str
            pages = int
        end

        # Use them like this:
        my_blook = Book(
            title: "The hobbit" 
            author: "J.R.R. Tolkien"
            pages: 310
        )

        print(my_book.title)
        print(my_book.author)
        print(my_book.pages)
    ```
    - But in this case everything is mutable can't be changed about doing:
    ```ruby
            my_blook = Book(
            title: "The hobbit" 
            author: "J.R.R. Tolkien"
            pages: 310
        )
    ```
    - That why "mut" keyword exits so make the filed changable:
        ```ruby
        struct Book:
            mut title = str
            mut aithor = str
            mut pages = int
        end

        # Use them like this:
        mut my_blook = Book(
            title: "The hobbit" 
            author: "J.R.R. Tolkien"
            pages: 310
        )

        my_book.title = "Something new"
        my_book.author = "Somethin else"
        my_book.pages = 310 + 50
    ```

- Struct can be used as a types too example:
    ```ruby
        struct Custom_Return_Type: 
            string = str
            number = (int | float)
        end


        # example usage:
        func example(): Custom_Return_Type
            i: Custom_Return_Type = Custom_Return_Type("Hello world", 105.3)
            return Custom_Return_Type("Hello world", 100)
        end
    ```
- There is differents between public/private structer like function/enum have.
    - Example public struct
    - main.vix
    ```ruby
        public struct Example:
            pub mut name = str
            pub mut age = 0
        end
    ```
    - example.vix
    ```ruby
        import Example from "src/main.vix"

        example = Example(name = "", age = 0)
        example.name = "Hello world"
        example.age = 100
    ```
    **Information:** Make sure that filed is public by adding "pub"
    
