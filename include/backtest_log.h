/**
 * Backtest Logging Framework
 *
 * Simple logging system for backtest engine with configurable verbosity levels.
 * Thread-safe and lightweight - no external dependencies.
 */

#ifndef BACKTEST_LOG_H
#define BACKTEST_LOG_H

#include <iostream>
#include <sstream>
#include <string>
#include <mutex>
#include <ctime>
#include <iomanip>

namespace backtest {

/**
 * Log level enumeration
 */
enum class LogLevel : uint8_t {
    TRACE = 0,   // Very detailed debugging
    DEBUG = 1,   // Debugging information
    INFO = 2,    // General information
    WARN = 3,    // Warnings
    ERROR = 4,   // Errors
    FATAL = 5,   // Fatal errors
    OFF = 6      // Disable logging
};

/**
 * Convert log level to string
 */
inline const char* LogLevelStr(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

/**
 * Simple thread-safe logger
 */
class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    void SetLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
    }

    LogLevel GetLevel() const { return level_; }

    void SetOutputStream(std::ostream* stream) {
        std::lock_guard<std::mutex> lock(mutex_);
        output_ = stream;
    }

    void EnableTimestamp(bool enable) { show_timestamp_ = enable; }
    void EnableLevel(bool enable) { show_level_ = enable; }

    template<typename... Args>
    void Log(LogLevel level, const char* file, int line, Args&&... args) {
        if (level < level_) return;

        std::lock_guard<std::mutex> lock(mutex_);

        std::ostringstream ss;

        // Timestamp
        if (show_timestamp_) {
            auto now = std::time(nullptr);
            ss << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << " ";
        }

        // Level
        if (show_level_) {
            ss << "[" << LogLevelStr(level) << "] ";
        }

        // File and line (for DEBUG and below)
        if (level <= LogLevel::DEBUG && file) {
            // Extract just filename from path
            const char* filename = file;
            const char* p = file;
            while (*p) {
                if (*p == '/' || *p == '\\') filename = p + 1;
                p++;
            }
            ss << filename << ":" << line << " ";
        }

        // Message
        AppendArgs(ss, std::forward<Args>(args)...);

        ss << "\n";
        *output_ << ss.str();
        output_->flush();
    }

private:
    Logger() : level_(LogLevel::INFO), output_(&std::cerr),
               show_timestamp_(true), show_level_(true) {}

    void AppendArgs(std::ostringstream& ss) {}

    template<typename T, typename... Args>
    void AppendArgs(std::ostringstream& ss, T&& first, Args&&... rest) {
        ss << std::forward<T>(first);
        AppendArgs(ss, std::forward<Args>(rest)...);
    }

    LogLevel level_;
    std::ostream* output_;
    std::mutex mutex_;
    bool show_timestamp_;
    bool show_level_;
};

} // namespace backtest

// ============ Logging Macros ============

#define BACKTEST_LOG(level, ...) \
    backtest::Logger::Instance().Log(level, __FILE__, __LINE__, __VA_ARGS__)

#define BACKTEST_LOG_TRACE(...) BACKTEST_LOG(backtest::LogLevel::TRACE, __VA_ARGS__)
#define BACKTEST_LOG_DEBUG(...) BACKTEST_LOG(backtest::LogLevel::DEBUG, __VA_ARGS__)
#define BACKTEST_LOG_INFO(...)  BACKTEST_LOG(backtest::LogLevel::INFO, __VA_ARGS__)
#define BACKTEST_LOG_WARN(...)  BACKTEST_LOG(backtest::LogLevel::WARN, __VA_ARGS__)
#define BACKTEST_LOG_ERROR(...) BACKTEST_LOG(backtest::LogLevel::ERROR, __VA_ARGS__)
#define BACKTEST_LOG_FATAL(...) BACKTEST_LOG(backtest::LogLevel::FATAL, __VA_ARGS__)

// ============ Error Codes ============

namespace backtest {

/**
 * Error codes for backtest operations
 */
enum class ErrorCode : int {
    SUCCESS = 0,

    // Configuration errors (100-199)
    ERR_INVALID_CONFIG = 100,
    ERR_INVALID_SYMBOL = 101,
    ERR_INVALID_BALANCE = 102,
    ERR_INVALID_LEVERAGE = 103,
    ERR_INVALID_LOT_SIZE = 104,
    ERR_INVALID_PRICE = 105,
    ERR_INVALID_SL_TP = 106,
    ERR_INVALID_DATE_RANGE = 107,

    // Data errors (200-299)
    ERR_DATA_FILE_NOT_FOUND = 200,
    ERR_DATA_PARSE_ERROR = 201,
    ERR_DATA_INVALID_TICK = 202,
    ERR_DATA_GAP_DETECTED = 203,
    ERR_DATA_OUTLIER_DETECTED = 204,

    // Trading errors (300-399)
    ERR_INSUFFICIENT_MARGIN = 300,
    ERR_LOT_SIZE_TOO_SMALL = 301,
    ERR_LOT_SIZE_TOO_LARGE = 302,
    ERR_LOT_SIZE_INVALID_STEP = 303,
    ERR_SL_TOO_CLOSE = 304,
    ERR_TP_TOO_CLOSE = 305,
    ERR_POSITION_NOT_FOUND = 306,
    ERR_ORDER_NOT_FOUND = 307,
    ERR_NETTING_CONFLICT = 308,
    ERR_MAX_POSITIONS_REACHED = 309,

    // System errors (400-499)
    ERR_OUT_OF_MEMORY = 400,
    ERR_INTERNAL_ERROR = 401,
    ERR_STOP_OUT = 402
};

/**
 * Get error message for error code
 */
inline const char* GetErrorMessage(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS: return "Success";
        case ErrorCode::ERR_INVALID_CONFIG: return "Invalid configuration";
        case ErrorCode::ERR_INVALID_SYMBOL: return "Invalid symbol";
        case ErrorCode::ERR_INVALID_BALANCE: return "Invalid initial balance";
        case ErrorCode::ERR_INVALID_LEVERAGE: return "Invalid leverage";
        case ErrorCode::ERR_INVALID_LOT_SIZE: return "Invalid lot size";
        case ErrorCode::ERR_INVALID_PRICE: return "Invalid price";
        case ErrorCode::ERR_INVALID_SL_TP: return "Invalid stop loss or take profit";
        case ErrorCode::ERR_INVALID_DATE_RANGE: return "Invalid date range";
        case ErrorCode::ERR_DATA_FILE_NOT_FOUND: return "Tick data file not found";
        case ErrorCode::ERR_DATA_PARSE_ERROR: return "Failed to parse tick data";
        case ErrorCode::ERR_DATA_INVALID_TICK: return "Invalid tick data (bid/ask)";
        case ErrorCode::ERR_DATA_GAP_DETECTED: return "Data gap detected";
        case ErrorCode::ERR_DATA_OUTLIER_DETECTED: return "Price outlier detected";
        case ErrorCode::ERR_INSUFFICIENT_MARGIN: return "Insufficient margin";
        case ErrorCode::ERR_LOT_SIZE_TOO_SMALL: return "Lot size below minimum";
        case ErrorCode::ERR_LOT_SIZE_TOO_LARGE: return "Lot size above maximum";
        case ErrorCode::ERR_LOT_SIZE_INVALID_STEP: return "Lot size not a valid step";
        case ErrorCode::ERR_SL_TOO_CLOSE: return "Stop loss too close to entry";
        case ErrorCode::ERR_TP_TOO_CLOSE: return "Take profit too close to entry";
        case ErrorCode::ERR_POSITION_NOT_FOUND: return "Position not found";
        case ErrorCode::ERR_ORDER_NOT_FOUND: return "Order not found";
        case ErrorCode::ERR_NETTING_CONFLICT: return "Position already exists (netting mode)";
        case ErrorCode::ERR_MAX_POSITIONS_REACHED: return "Maximum positions reached";
        case ErrorCode::ERR_OUT_OF_MEMORY: return "Out of memory";
        case ErrorCode::ERR_INTERNAL_ERROR: return "Internal error";
        case ErrorCode::ERR_STOP_OUT: return "Margin stop out occurred";
        default: return "Unknown error";
    }
}

/**
 * Result wrapper with error handling
 */
template<typename T>
struct Result {
    T value;
    ErrorCode error;
    std::string message;

    Result() : error(ErrorCode::SUCCESS) {}
    Result(T val) : value(val), error(ErrorCode::SUCCESS) {}
    Result(ErrorCode err, const std::string& msg = "")
        : error(err), message(msg.empty() ? GetErrorMessage(err) : msg) {}

    bool IsOk() const { return error == ErrorCode::SUCCESS; }
    bool IsError() const { return error != ErrorCode::SUCCESS; }
    operator bool() const { return IsOk(); }
};

} // namespace backtest

#endif // BACKTEST_LOG_H
