# tgbot
**TgBot** is a C++ (C++17) library for implementing Telegram bots in interaction-style way. 

> Note: This library is not ready for production and can be used as learning resource how to do (or not to do) things.

This library utilizes `boost-beast`, `boost-asio` as workload libraries. 

Currently, only [Long-Polling](https://core.telegram.org/bots/api#getupdates) method for gettings updates is supported. [Webhooks](https://core.telegram.org/bots/api#setwebhook) 
may be implemented in the future.

# Building

## CMake

This library provides `CMakeLists` file. To generate project files, run the following in your terminal:
```sh
> cmake -B [build directory] -S . "-DCMAKE_TOOLCHAIN_FILE=[path to vcpkg]/scripts/buildsystems/vcpkg.cmake"
```

### vcpkg 

This library utilizes `vcpkg` as a dependency manager, and provides files for working in `manifest` mode. If you are unfamiliar how to setup vcpkg, 
follow the instructions provided on its [GitHub](https://github.com/microsoft/vcpkg).


## Docker

Dockerfile is also provided for building and running your bot in the container. 

To build the container, run in the terminal:

```sh
> sudo docker build -t tgbot .
```

To run your built image, use the following:

```sh
> sudo docker run --rm -n tgbot\run tgbot
```

## Running
 
To run this bot, you must provide `Token`. `Token` is obtained using Telegram's `BotFather`. 

Once you have obtained the token, provide a configuration file entry:

```json
{
  "Telegram": {
    "Token": "your-token",
    "Gateway": "api.telegram.org",
    /* you can also set long polling interval */
    
    "LongPolling": {
      "Interval": 4 /* in seconds */
    }
  }
}
```

You can learn about configuration in the [Configuration](#configuration) section.

## Features

This library provides multiple modules for building your bots, but it is planned to split them into different libraries.

### SQLite integration

There is support for querying `SQLite` databases in convenient way:

#### Create and query database
```cpp
#include <sqlite/sqlite.h>

try { 
    sqlite::Database db(path_to_db);
    db.open();
    
    db.prepare("SELECT * FROM Table WHERE Something=?") // this will fetch one row as tuple
        .with_value(0, some_value)
        .fetch_one<Type1, Type2>();
    
    auto reader = db.prepare("SELECT * FROM Table").fetch(); // this will open the reader
    while(reader.read()) { 
        auto row = reader.fetch<Type1, Type2>();
        // access the values
    }
}
catch (const sqlite::Error& e) { 
    // handle the error
}
```

#### Inserting values
```cpp
#include <sqlite/sqlite.h>

try { 
    sqlite::Database db(path_to_db);
    db.open();
    
    db.prepare("INSERT INTO Table VALUES(?, ?)")
        .with_value(0, some_value1)
        .with_value(1, some_value2)
        .execute();
}
catch (const sqlite::Error& e) { 
    // handle the error
}
```

#### Transactions
```cpp
#include <sqlite/sqlite.h>

try { 
    sqlite::Database db(path_to_db);
    db.open();
    
    var tr = db.transaction();
    
    // do work 
    tr.commit(); // or tr.rollback()
                 //
                 // note that if transaction left the scope uncommited
                 // it will be rolled back automatically
}
catch (const sqlite::Error& e) { 
    // handle the error
}
```

### Configuration

Reading and accessing configuration is also possible. 

Configuration is stored as json object file.

```json
{
  "Calendar": { 
    "DaysInYear": 365
  },
  "Nested": {
    "Configuration": {
      "Possible": true
    }
  }
}
```

To access this configuration in code:

```cpp
// create configuration
auto config = config::Store::from_json(path_to_config);

// accessing 
auto value1 = config["Calendar::DaysInYear"];            // returns "365"
auto value2 = condif["Nested::Configuration::Possible"]; // returns "true"
```

Note that configuration is immutable after created, and can be copied around safely: it is shared between objects 
using shared pointers.

### Logging

Simple console logging is provided.

```cpp

// myLog::LogManager::configure(config) - can be configured using Configuration library 

const auto appLogger = myLog::LogManager::get().create_logger("App");
appLogger->info("Logging string literal");
appLogger->info("Logging formatted string = {}", some_value);

```

This `appLogger` object is alive until program exit, once created. 

Formatting is implemented using beautiful [fmt](https://github.com/fmtlib/fmt) library. 
