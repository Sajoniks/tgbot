#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include "log/logmanager.h"
#include "log/logger.h"

namespace mylog {

std::recursive_mutex LogManager::_mutex;
std::unique_ptr<LogManager> LogManager::_instance;

auto timestamp() {
    auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto* gm = std::gmtime(&time);
    return std::put_time(gm, "%Y.%m.%d-%H:%M.%S");
}

LogManager& LogManager::operator<<(const LogEvent& e) {

    if (std::find(_ignoreCategories.begin(), _ignoreCategories.end(), e.name()) != _ignoreCategories.end()) {
        return *this;
    }

    namespace clock = std::chrono;
    auto time = clock::duration_cast<clock::milliseconds>( clock::high_resolution_clock::now().time_since_epoch() ) % 1000;

    std::ostringstream oss;

    oss << '[' << mylog::timestamp() << '.' << std::setfill('0') << std::setw(3) << time.count() << std::setfill(' ') << ']';

    {
        oss << std::left << std::setw(10);
        switch (e.verbosity()) {
            case Verbosity::Log:
                oss << "[LOG]";
                break;

            case Verbosity::Warning:
                oss << "[WARNING]";
                break;

            case Verbosity::Error:
                oss << "[ERROR]";
                break;
        }
        oss << std::right;
    }

    {
        oss << std::left << std::setw(15) << std::string{ e.name() } + ":" << std::right;
    }

    oss << e.message() << '\n';

    std::cout << oss.str() << std::flush;
    return *this;
}

Logger* LogManager::create_logger(std::string name) {
    std::unique_lock<std::recursive_mutex> lock{ _mutex };

    auto it = _loggers.find(name);
    if (it != _loggers.end()) {
        return it->second.get();
    } else {
        std::unique_ptr<Logger> logger;
        logger.reset(new Logger{ std::move(name), *this });
        std::string_view key = logger->_name;
        auto insertIt = _loggers.insert_or_assign(key, std::move(logger));
        return insertIt.first->second.get();
    }
}

LogManager& LogManager::get() {
    std::unique_lock<std::recursive_mutex> lock{ _mutex };

    if (_instance == nullptr) {
        _instance.reset(new LogManager());
    }
    return *_instance;
}

void LogManager::configure(const config::Store& config) {
    std::unique_lock<std::recursive_mutex> lock { _mutex };
    get().configure_instance(config);
}

void LogManager::configure_instance(const config::Store& config) {
    {
        auto ignores = config.values("Log::IgnoreNames");
        std::copy(ignores.begin(), ignores.end(), std::back_inserter(_ignoreCategories));
    }
}


}


