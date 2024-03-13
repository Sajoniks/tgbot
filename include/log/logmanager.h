#pragma once

#include <memory>
#include <vector>
#include <mutex>

#include "log/types.h"
#include "configuration/configuration.h"

namespace mylog {

class Logger;

class LogManager {

    LogManager() = default;

    void configure_instance(const config::Store& config);

public:

    static LogManager& get();
    static void configure(const config::Store& config);

    Logger* create_logger(std::string name);

    LogManager& operator<<(const LogEvent&);

private:

    std::unordered_map<std::string_view, std::unique_ptr<Logger>> _loggers;
    std::vector<std::string_view> _ignoreCategories;

    static std::recursive_mutex _mutex;
    static std::unique_ptr<LogManager> _instance;
};

}