#include "logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::init(const std::string& log_path, LogLevel min_level) {
    std::lock_guard<std::mutex> lock(mu_);
    min_level_ = min_level;
    if (!log_path.empty()) {
        log_file_.open(log_path, std::ios::app);
    }
    initialized_ = true;
}

std::string Logger::level_str(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        default:              return "?????";
    }
}

std::string Logger::timestamp_str() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t   = system_clock::to_time_t(now);
    auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::ostringstream oss;
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

void Logger::log(LogLevel level, const std::string& msg) {
    if (level < min_level_) return;
    std::lock_guard<std::mutex> lock(mu_);
    std::string line = "[" + timestamp_str() + "] [" + level_str(level) + "] " + msg;
    if (log_file_.is_open()) {
        log_file_ << line << '\n';
        log_file_.flush();
    }
    if (level >= LogLevel::Error) {
        std::cerr << line << '\n';
    }
}
