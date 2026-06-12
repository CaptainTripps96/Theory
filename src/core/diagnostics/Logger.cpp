#include "core/diagnostics/Logger.h"

#include "core/diagnostics/PerformanceTrace.h"

#include <fstream>
#include <iostream>

namespace tsq::core::diagnostics
{
std::string_view logLevelName (LogLevel level) noexcept
{
    switch (level)
    {
        case LogLevel::debug: return "debug";
        case LogLevel::info: return "info";
        case LogLevel::warning: return "warning";
        case LogLevel::error: return "error";
    }

    return "unknown";
}

std::string formatLogEntry (const LogEntry& entry)
{
    std::string formatted;
    formatted.reserve (entry.message.size() + 12);
    formatted += '[';
    formatted += logLevelName (entry.level);
    formatted += "] ";
    formatted += entry.message;
    return formatted;
}

Logger::Logger (ConsoleOutput consoleOutput)
    : consoleOutput_ (consoleOutput)
{
}

bool Logger::setFileOutput (std::filesystem::path filePath, bool truncate)
{
    try
    {
        if (filePath.empty())
        {
            fileOutputPath_.reset();
            return true;
        }

        if (filePath.has_parent_path())
            std::filesystem::create_directories (filePath.parent_path());

        std::ofstream stream { filePath, truncate ? std::ios::trunc : std::ios::app };
        if (! stream)
            return false;

        fileOutputPath_ = std::move (filePath);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string Logger::fileOutputPath() const
{
    return fileOutputPath_.has_value() ? fileOutputPath_->string() : std::string {};
}

void Logger::log (LogLevel level, std::string_view message)
{
    ScopedPerformanceTimer timer { "Logger::log" };

    LogEntry entry { level, std::string { message } };
    entries_.push_back (entry);
    const auto formatted = formatLogEntry (entry);

    if (consoleOutput_ == ConsoleOutput::enabled)
        std::clog << formatted << '\n';

    if (fileOutputPath_.has_value())
    {
        try
        {
            std::ofstream stream { *fileOutputPath_, std::ios::app };
            if (stream)
                stream << formatted << '\n';
        }
        catch (...)
        {
        }
    }
}

void Logger::debug (std::string_view message)
{
    log (LogLevel::debug, message);
}

void Logger::info (std::string_view message)
{
    log (LogLevel::info, message);
}

void Logger::warning (std::string_view message)
{
    log (LogLevel::warning, message);
}

void Logger::error (std::string_view message)
{
    log (LogLevel::error, message);
}

std::vector<LogEntry> Logger::entries() const
{
    return entries_;
}

std::vector<std::string> Logger::formattedEntries() const
{
    std::vector<std::string> formatted;
    formatted.reserve (entries_.size());

    for (const auto& entry : entries_)
        formatted.push_back (formatLogEntry (entry));

    return formatted;
}

void Logger::clear()
{
    entries_.clear();
}
}
