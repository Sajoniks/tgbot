#pragma once

#include <memory>
#include "log/types.h"

namespace mylog {

class LogManager;

class Logger final : public std::enable_shared_from_this<Logger> {

    friend LogManager;

    explicit Logger(std::string name, LogManager& lm);

    void log_impl(Verbosity verbosity, std::string_view message);

public:

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void info(std::string_view message) {
        log_impl(Verbosity::Log, message);
    }
    void warn(std::string_view message) {
        log_impl(Verbosity::Warning, message);
    }
    void error(std::string_view message) {
        log_impl(Verbosity::Error, message);
    }

    template<typename... Args>
    void info(std::string_view message, Args&&... args) {
        log_impl(Verbosity::Log, fmt::format(message, std::forward<Args>(args)...));
    }
    template<typename... Args>
    void warn(std::string_view message, Args&&... args) {
        log_impl(Verbosity::Warning, fmt::format(message, std::forward<Args>(args)...));
    }
    template<typename... Args>
    void error(std::string_view message, Args&&... args) {
        log_impl(Verbosity::Error, fmt::format(message, std::forward<Args>(args)...));
    }

private:
    LogManager& _lm;
    std::string _name;
};
}