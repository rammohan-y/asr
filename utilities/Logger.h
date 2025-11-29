#pragma once

#include <string>
#include <fstream>
#include <mutex>

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    ERROR = 2
};

class Logger {
public:
    explicit Logger(const std::string& logFolder);
    ~Logger();

    void setLogFile(const std::string& filenameBase); // e.g., "vosk_asr" or "main"
    void setLogLevel(LogLevel level);
    LogLevel getLogLevel() const;

    void info(const std::string& sessionUuid, const std::string& message);
    void debug(const std::string& sessionUuid, const std::string& message);
    void error(const std::string& sessionUuid, const std::string& message);

private:
    std::string getTimestamp() const;
    std::string makePath(const std::string& base) const;
    LogLevel parseLogLevel(const std::string& levelStr) const;

    std::string logFolder_;
    std::ofstream logFile_;
    mutable std::mutex mutex_;
    LogLevel currentLogLevel_;
};


