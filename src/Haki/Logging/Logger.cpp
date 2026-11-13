#include "Logger.h"

ILogger* Logger::s_logger = nullptr;

void Logger::SetLogger(ILogger* logger)
{
    s_logger = logger;
}

LogStream Logger::Debug()
{
    return LogStream(s_logger, LogLevel::Debug);
}

LogStream Logger::Info()
{
    return LogStream(s_logger, LogLevel::Info);
}

LogStream Logger::Warning()
{
    return LogStream(s_logger, LogLevel::Warning);
}

LogStream Logger::Error()
{
    return LogStream(s_logger, LogLevel::Error);
}