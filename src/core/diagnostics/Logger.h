#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <optional>

namespace tsq::core::diagnostics
{
enum class LogLevel
{
    debug,
    info,
    warning,
    error
};

struct LogEntry
{
    LogLevel level {};
    std::string message;
};

std::string_view logLevelName (LogLevel level) noexcept;
std::string formatLogEntry (const LogEntry& entry);

class Logger final
{
public:
    enum class ConsoleOutput
    {
        enabled,
        disabled
    };

    explicit Logger (ConsoleOutput consoleOutput = ConsoleOutput::enabled);

    bool setFileOutput (std::filesystem::path filePath, bool truncate = true);
    std::string fileOutputPath() const;
    void log (LogLevel level, std::string_view message);
    void debug (std::string_view message);
    void info (std::string_view message);
    void warning (std::string_view message);
    void error (std::string_view message);

    std::vector<LogEntry> entries() const;
    std::vector<std::string> formattedEntries() const;
    void clear();

private:
    ConsoleOutput consoleOutput_ {};
    std::optional<std::filesystem::path> fileOutputPath_;
    std::vector<LogEntry> entries_;
};
}
