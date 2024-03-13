#pragma once

#include <string>
#include <fmt/core.h>

namespace mylog {

class Logger;
class LogManager;

enum class Verbosity : unsigned char {
    Log,
    Warning,
    Error
};

class LogEvent final {
public:

    LogEvent(Verbosity verbosity, std::string_view name, std::string message);

    template<typename... Args>
    LogEvent(Verbosity verbosity, std::string_view name, std::string_view message, Args&&... args)
            : LogEvent(verbosity, name, fmt::format(message, std::forward<Args>(args)...))
    {}

    [[nodiscard]] std::string_view name() const;
    [[nodiscard]] std::string_view message() const;
    [[nodiscard]] Verbosity verbosity() const;

private:
    std::string_view _name;
    std::string _message;
    Verbosity _v;
};

using LoggerPtr = Logger*;

}