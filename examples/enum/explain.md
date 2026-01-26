### What is Enum?:
---
enum is a type contains a limited number of constant, each with a different name.
- **Information** There is 2 type of function "public/private" they have the same task. Same work just one private for the current file and one public to whole files.

- Example basic enum:
```ruby
    enum Color:
        Red,
        Green,
        Blue,
    end 

    color = Color.Red
```

Instead of using raw numbers or string like this:
    ```ruby
        stauts = 1
        stauts = 2
        stauts = 3
    ```
You can using Enum do like:
    ```ruby
        status = Status.Loading
        status = Status.Success
        status = Status.Error
    ```
- Using Enum you get:
    - Cleaner code, easier to read and so much better overall.
    - Safer to maintain.

#### Why use Enum?
If enums doesn't exits things like this could happen and make your code so much less cleaner:
- Example using Enum for Changing a player Information:
    - Each variant can store different data types example:
    ```ruby
        enum Player:
            Move(x: int32, y: int32),
            Speed(speed: usize),
            Position(x: int32, y: int32)
            Avatar(skin_color: (int32, int32, int32), hegith: usize, wight: usize),
        end

        move = Player.Move(x: 10, y: 20)
        speed = Player.Speed(speed: 10)
        position = Player.Position(0, 0)
    ```
    - Simple variants don't store different data and types:
    ```ruby
        enum Color:
            Red,
            Green,
            Blue,
        end

        blue = Color.Blue
        green = Color.Green
        red = Color.Red
    ```
    - Enum is so useful in matching too you can do like:
    ```ruby
        enum Action:
            Quit,
            Join(game: str),
            Read(data: any),
            Write(data: any),
        end

        match action:
            case Action.Quit do print("Player: Quitted") end
            case Action.Join(game) do print("Player: Joined") end
            case Action.Read(data) do print("Player: Read Data") end
            case Action.Write(data) do print("Player: Write Data") end
        end
    ```
    - enum can be public using "public" keyword Example:
    - in main.vix you can do like example:
    ```ruby
        public enum Action:
            Quit,
            Join(game: str),
            Read(data: any),
            Write(data: any),
        end
    ```
    - in example.vix you can call that enum and use it like:
    ```ruby
        import Actions from "src/main.vix"
        
        match action:
            case Action.Quit do print("Player: Quitted") end
            case Action.Join(game) do print("Player: Joined") end
            case Action.Read(data) do print("Player: Read Data") end
            case Action.Write(data) do print("Player: Write Data") end
        end
    ```
