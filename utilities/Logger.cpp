#include "Logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <cctype>

Logger::Logger(const std::string& logFolder) : logFolder_(logFolder), currentLogLevel_(LogLevel::INFO) {
    std::error_code ec;
    std::filesystem::create_directories(logFolder_, ec);
    
    // Check for LOG_LEVEL environment variable
    const char* logLevelEnv = std::getenv("LOG_LEVEL");
    if (logLevelEnv) {
        currentLogLevel_ = parseLogLevel(std::string(logLevelEnv));
    }
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logFile_.is_open()) {
        logFile_ << "=== Log ended at " << getTimestamp() << " ===\n";
        logFile_.close();
    }
}

void Logger::setLogFile(const std::string& filenameBase) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logFile_.is_open()) {
        logFile_ << "=== Log switched at " << getTimestamp() << " ===\n";
        logFile_.close();
    }
    const std::string path = makePath(filenameBase);
    logFile_.open(path, std::ios::out | std::ios::app);
    if (logFile_.is_open()) {
        logFile_ << "=== Log started at " << getTimestamp() << " ===\n";
        logFile_.flush();
    } else {
        std::cerr << "[ERR] Could not open log file: " << path << "\n";
    }
}

void Logger::setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentLogLevel_ = level;
}

LogLevel Logger::getLogLevel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentLogLevel_;
}

void Logger::info(const std::string& sessionUuid, const std::string& message) {
    if (currentLogLevel_ > LogLevel::INFO) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string uuidPart = sessionUuid.empty() ? "system" : sessionUuid;
    const std::string line = getTimestamp() + " | INFO | " + uuidPart + " | " + message + "\n";
    //std::cout << line;
    if (logFile_.is_open()) { logFile_ << line; logFile_.flush(); }
}

void Logger::debug(const std::string& sessionUuid, const std::string& message) {
    if (currentLogLevel_ > LogLevel::DEBUG) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string uuidPart = sessionUuid.empty() ? "system" : sessionUuid;
    const std::string line = getTimestamp() + " | DEBUG | " + uuidPart + " | " + message + "\n";
    //std::cout << line;
    if (logFile_.is_open()) { logFile_ << line; logFile_.flush(); }
}

void Logger::error(const std::string& sessionUuid, const std::string& message) {
    if (currentLogLevel_ > LogLevel::ERROR) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string uuidPart = sessionUuid.empty() ? "system" : sessionUuid;
    const std::string line = getTimestamp() + " | ERR | " + uuidPart + " | " + message + "\n";
    //std::cerr << line;
    if (logFile_.is_open()) { logFile_ << line; logFile_.flush(); }
}

std::string Logger::getTimestamp() const {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto t = clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    
    // Get milliseconds
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string Logger::makePath(const std::string& base) const {
    return logFolder_ + "/" + base + ".log";
}

LogLevel Logger::parseLogLevel(const std::string& levelStr) const {
    std::string upperLevel = levelStr;
    std::transform(upperLevel.begin(), upperLevel.end(), upperLevel.begin(), ::toupper);
    
    if (upperLevel == "DEBUG") {
        return LogLevel::DEBUG;
    } else if (upperLevel == "INFO") {
        return LogLevel::INFO;
    } else if (upperLevel == "ERROR") {
        return LogLevel::ERROR;
    } else {
        // Default to INFO if invalid value
        std::cerr << "[WARN] Invalid LOG_LEVEL value: " << levelStr << ". Using INFO level." << std::endl;
        return LogLevel::INFO;
    }
}


