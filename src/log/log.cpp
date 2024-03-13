#include "log/logger.h"
#include "log/logmanager.h"
#include "log/types.h"


namespace mylog {

LogEvent::LogEvent(Verbosity verbosity, std::string_view name, std::string message)
        : _v{ verbosity }
        , _name{ name }
        , _message{ std::move(message) }
{}

std::string_view LogEvent::name() const { return _name; }
std::string_view LogEvent::message() const { return _message; }
Verbosity LogEvent::verbosity() const { return _v; }


Logger::Logger(std::string name, LogManager& manager)
    : _name{ std::move(name) }
    , _lm{ manager }
{}

void Logger::log_impl(Verbosity verbosity, std::string_view message) {
    _lm << LogEvent{ verbosity, _name, message };
}


}