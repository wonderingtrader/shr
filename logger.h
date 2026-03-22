#pragma once

#include <string>
#include <fstream>
#include <mutex>

enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    static Logger& instance();

    void init(const std::string& log_path, LogLevel min_level = LogLevel::Info);
    void log(LogLevel level, const std::string& msg);

    void debug(const std::string& msg) { log(LogLevel::Debug, msg); }
    void info(const std::string& msg)  { log(LogLevel::Info,  msg); }
    void warn(const std::string& msg)  { log(LogLevel::Warn,  msg); }
    void error(const std::string& msg) { log(LogLevel::Error, msg); }

private:
    Logger() = default;
    std::ofstream log_file_;
    LogLevel      min_level_ = LogLevel::Info;
    std::mutex    mu_;
    bool          initialized_ = false;

    std::string level_str(LogLevel l);
    std::string timestamp_str();
};

#define LOG_DEBUG(msg) Logger::instance().debug(msg)
#define LOG_INFO(msg)  Logger::instance().info(msg)
#define LOG_WARN(msg)  Logger::instance().warn(msg)
#define LOG_ERROR(msg) Logger::instance().error(msg)
