#pragma once

#include "Logger.h"
#include <iostream>
#include <iomanip>
#include <chrono>

namespace Colors
{
    constexpr const char *RESET = "\033[0m";
    constexpr const char *WHITE = "\033[37m";
    constexpr const char *LIGHT_BLUE = "\033[94m";
    constexpr const char *YELLOW = "\033[33m";
    constexpr const char *RED = "\033[31m";
}

class ConsoleLogger : public ILogger
{
public:
    void Log(LogLevel level, const std::string &message) override
    {
        const char *color = Colors::WHITE;

        switch (level)
        {
        case LogLevel::Debug:
            color = Colors::WHITE;
            break;

        case LogLevel::Info:
            color = Colors::LIGHT_BLUE;
            break;

        case LogLevel::Warning:
            color = Colors::YELLOW;
            break;

        case LogLevel::Error:
            color = Colors::RED;
            break;
        }

        std::cout
            << color;

        switch (level)
        {
        case LogLevel::Debug:
            std::cout << "[DEBUG] ";
            break;

        case LogLevel::Info:
            std::cout << "[INFO ] ";
            break;

        case LogLevel::Warning:
            std::cout << "[WARN ] ";
            break;

        case LogLevel::Error:
            std::cout << "[ERROR] ";
            break;
        }

        std::cout
            << message
            << Colors::RESET
            << std::endl;
    }
};