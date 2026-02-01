/**
 * MQL5 Compatibility Layer for C++
 *
 * This header provides MQL5-compatible types, functions, and structures
 * for porting MQL5 Expert Advisors to native C++.
 *
 * Usage:
 *   #include "mql5_compat.h"
 *   using namespace mql5;
 */

#ifndef MQL5_COMPAT_H
#define MQL5_COMPAT_H

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <ctime>
#include <algorithm>
#include <limits>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <map>
#include <deque>

namespace mql5 {

//=============================================================================
// MQL5 Basic Types
//=============================================================================

using datetime = int64_t;   // Unix timestamp in seconds
using ulong = uint64_t;
using uint = uint32_t;
using ushort = uint16_t;
using uchar = uint8_t;
using color = uint32_t;     // ARGB color

// Type constants
constexpr double EMPTY_VALUE = std::numeric_limits<double>::max();
constexpr int INVALID_HANDLE = -1;
constexpr int WRONG_VALUE = -1;

// Boolean constants (MQL5 style)
constexpr bool True = true;
constexpr bool False = false;

//=============================================================================
// MQL5 Enumerations
//=============================================================================

// Timeframes (ENUM_TIMEFRAMES)
enum ENUM_TIMEFRAMES {
    PERIOD_CURRENT = 0,
    PERIOD_M1 = 1,
    PERIOD_M2 = 2,
    PERIOD_M3 = 3,
    PERIOD_M4 = 4,
    PERIOD_M5 = 5,
    PERIOD_M6 = 6,
    PERIOD_M10 = 10,
    PERIOD_M12 = 12,
    PERIOD_M15 = 15,
    PERIOD_M20 = 20,
    PERIOD_M30 = 30,
    PERIOD_H1 = 16385,
    PERIOD_H2 = 16386,
    PERIOD_H3 = 16387,
    PERIOD_H4 = 16388,
    PERIOD_H6 = 16390,
    PERIOD_H8 = 16392,
    PERIOD_H12 = 16396,
    PERIOD_D1 = 16408,
    PERIOD_W1 = 32769,
    PERIOD_MN1 = 49153
};

// Order types
enum ENUM_ORDER_TYPE {
    ORDER_TYPE_BUY = 0,
    ORDER_TYPE_SELL = 1,
    ORDER_TYPE_BUY_LIMIT = 2,
    ORDER_TYPE_SELL_LIMIT = 3,
    ORDER_TYPE_BUY_STOP = 4,
    ORDER_TYPE_SELL_STOP = 5,
    ORDER_TYPE_BUY_STOP_LIMIT = 6,
    ORDER_TYPE_SELL_STOP_LIMIT = 7,
    ORDER_TYPE_CLOSE_BY = 8
};

// Order type filling
enum ENUM_ORDER_TYPE_FILLING {
    ORDER_FILLING_FOK = 0,
    ORDER_FILLING_IOC = 1,
    ORDER_FILLING_RETURN = 2,
    ORDER_FILLING_BOC = 3
};

// Order type time
enum ENUM_ORDER_TYPE_TIME {
    ORDER_TIME_GTC = 0,
    ORDER_TIME_DAY = 1,
    ORDER_TIME_SPECIFIED = 2,
    ORDER_TIME_SPECIFIED_DAY = 3
};

// Trade action types
enum ENUM_TRADE_REQUEST_ACTIONS {
    TRADE_ACTION_DEAL = 1,
    TRADE_ACTION_PENDING = 5,
    TRADE_ACTION_SLTP = 6,
    TRADE_ACTION_MODIFY = 7,
    TRADE_ACTION_REMOVE = 8,
    TRADE_ACTION_CLOSE_BY = 10
};

// Position type
enum ENUM_POSITION_TYPE {
    POSITION_TYPE_BUY = 0,
    POSITION_TYPE_SELL = 1
};

// Deal type
enum ENUM_DEAL_TYPE {
    DEAL_TYPE_BUY = 0,
    DEAL_TYPE_SELL = 1,
    DEAL_TYPE_BALANCE = 2,
    DEAL_TYPE_CREDIT = 3,
    DEAL_TYPE_CHARGE = 4,
    DEAL_TYPE_CORRECTION = 5,
    DEAL_TYPE_BONUS = 6,
    DEAL_TYPE_COMMISSION = 7,
    DEAL_TYPE_COMMISSION_DAILY = 8,
    DEAL_TYPE_COMMISSION_MONTHLY = 9,
    DEAL_TYPE_COMMISSION_AGENT_DAILY = 10,
    DEAL_TYPE_COMMISSION_AGENT_MONTHLY = 11,
    DEAL_TYPE_INTEREST = 12,
    DEAL_TYPE_BUY_CANCELED = 13,
    DEAL_TYPE_SELL_CANCELED = 14,
    DEAL_DIVIDEND = 15,
    DEAL_DIVIDEND_FRANKED = 16,
    DEAL_TAX = 17
};

// Deal entry
enum ENUM_DEAL_ENTRY {
    DEAL_ENTRY_IN = 0,
    DEAL_ENTRY_OUT = 1,
    DEAL_ENTRY_INOUT = 2,
    DEAL_ENTRY_OUT_BY = 3
};

// MA methods
enum ENUM_MA_METHOD {
    MODE_SMA = 0,
    MODE_EMA = 1,
    MODE_SMMA = 2,
    MODE_LWMA = 3
};

// Applied price
enum ENUM_APPLIED_PRICE {
    PRICE_CLOSE = 0,
    PRICE_OPEN = 1,
    PRICE_HIGH = 2,
    PRICE_LOW = 3,
    PRICE_MEDIAN = 4,
    PRICE_TYPICAL = 5,
    PRICE_WEIGHTED = 6
};

// Symbol info integer
enum ENUM_SYMBOL_INFO_INTEGER {
    SYMBOL_SELECT,
    SYMBOL_VISIBLE,
    SYMBOL_SESSION_DEALS,
    SYMBOL_SESSION_BUY_ORDERS,
    SYMBOL_SESSION_SELL_ORDERS,
    SYMBOL_VOLUME,
    SYMBOL_VOLUMEHIGH,
    SYMBOL_VOLUMELOW,
    SYMBOL_TIME,
    SYMBOL_DIGITS,
    SYMBOL_SPREAD_FLOAT,
    SYMBOL_SPREAD,
    SYMBOL_TRADE_CALC_MODE,
    SYMBOL_TRADE_MODE,
    SYMBOL_START_TIME,
    SYMBOL_EXPIRATION_TIME,
    SYMBOL_TRADE_STOPS_LEVEL,
    SYMBOL_TRADE_FREEZE_LEVEL,
    SYMBOL_TRADE_EXEMODE,
    SYMBOL_SWAP_MODE,
    SYMBOL_SWAP_ROLLOVER3DAYS,
    SYMBOL_MARGIN_HEDGED_USE_LEG,
    SYMBOL_EXPIRATION_MODE,
    SYMBOL_FILLING_MODE,
    SYMBOL_ORDER_MODE,
    SYMBOL_ORDER_GTC_MODE,
    SYMBOL_OPTION_MODE,
    SYMBOL_OPTION_RIGHT
};

// Symbol info double
enum ENUM_SYMBOL_INFO_DOUBLE {
    SYMBOL_BID,
    SYMBOL_BIDHIGH,
    SYMBOL_BIDLOW,
    SYMBOL_ASK,
    SYMBOL_ASKHIGH,
    SYMBOL_ASKLOW,
    SYMBOL_LAST,
    SYMBOL_LASTHIGH,
    SYMBOL_LASTLOW,
    SYMBOL_VOLUME_REAL,
    SYMBOL_VOLUMEHIGH_REAL,
    SYMBOL_VOLUMELOW_REAL,
    SYMBOL_OPTION_STRIKE,
    SYMBOL_POINT,
    SYMBOL_TRADE_TICK_VALUE,
    SYMBOL_TRADE_TICK_VALUE_PROFIT,
    SYMBOL_TRADE_TICK_VALUE_LOSS,
    SYMBOL_TRADE_TICK_SIZE,
    SYMBOL_TRADE_CONTRACT_SIZE,
    SYMBOL_TRADE_ACCRUED_INTEREST,
    SYMBOL_TRADE_FACE_VALUE,
    SYMBOL_TRADE_LIQUIDITY_RATE,
    SYMBOL_VOLUME_MIN,
    SYMBOL_VOLUME_MAX,
    SYMBOL_VOLUME_STEP,
    SYMBOL_VOLUME_LIMIT,
    SYMBOL_SWAP_LONG,
    SYMBOL_SWAP_SHORT,
    SYMBOL_MARGIN_INITIAL,
    SYMBOL_MARGIN_MAINTENANCE,
    SYMBOL_SESSION_VOLUME,
    SYMBOL_SESSION_TURNOVER,
    SYMBOL_SESSION_INTEREST,
    SYMBOL_SESSION_BUY_ORDERS_VOLUME,
    SYMBOL_SESSION_SELL_ORDERS_VOLUME,
    SYMBOL_SESSION_OPEN,
    SYMBOL_SESSION_CLOSE,
    SYMBOL_SESSION_AW,
    SYMBOL_SESSION_PRICE_SETTLEMENT,
    SYMBOL_SESSION_PRICE_LIMIT_MIN,
    SYMBOL_SESSION_PRICE_LIMIT_MAX,
    SYMBOL_MARGIN_HEDGED
};

// Account info integer
enum ENUM_ACCOUNT_INFO_INTEGER {
    ACCOUNT_LOGIN,
    ACCOUNT_TRADE_MODE,
    ACCOUNT_LEVERAGE,
    ACCOUNT_LIMIT_ORDERS,
    ACCOUNT_MARGIN_SO_MODE,
    ACCOUNT_TRADE_ALLOWED,
    ACCOUNT_TRADE_EXPERT,
    ACCOUNT_MARGIN_MODE,
    ACCOUNT_CURRENCY_DIGITS,
    ACCOUNT_FIFO_CLOSE
};

// Account info double
enum ENUM_ACCOUNT_INFO_DOUBLE {
    ACCOUNT_BALANCE,
    ACCOUNT_CREDIT,
    ACCOUNT_PROFIT,
    ACCOUNT_EQUITY,
    ACCOUNT_MARGIN,
    ACCOUNT_MARGIN_FREE,
    ACCOUNT_MARGIN_LEVEL,
    ACCOUNT_MARGIN_SO_CALL,
    ACCOUNT_MARGIN_SO_SO,
    ACCOUNT_MARGIN_INITIAL,
    ACCOUNT_MARGIN_MAINTENANCE,
    ACCOUNT_ASSETS,
    ACCOUNT_LIABILITIES,
    ACCOUNT_COMMISSION_BLOCKED
};

//=============================================================================
// MQL5 Structures
//=============================================================================

// Tick structure
struct MqlTick {
    datetime time;        // Time of the last price update
    double bid;           // Current Bid price
    double ask;           // Current Ask price
    double last;          // Price of the last deal (Last)
    ulong volume;         // Volume for the current Last price
    long time_msc;        // Time in milliseconds
    uint flags;           // Tick flags
};

// OHLCV bar structure
struct MqlRates {
    datetime time;        // Period start time
    double open;          // Open price
    double high;          // High price
    double low;           // Low price
    double close;         // Close price
    long tick_volume;     // Tick volume
    int spread;           // Spread
    long real_volume;     // Trade volume
};

// Date/Time structure
struct MqlDateTime {
    int year;
    int mon;
    int day;
    int hour;
    int min;
    int sec;
    int day_of_week;
    int day_of_year;
};

// Trade request
struct MqlTradeRequest {
    ENUM_TRADE_REQUEST_ACTIONS action;
    ulong magic;
    ulong order;
    std::string symbol;
    double volume;
    double price;
    double stoplimit;
    double sl;
    double tp;
    ulong deviation;
    ENUM_ORDER_TYPE type;
    ENUM_ORDER_TYPE_FILLING type_filling;
    ENUM_ORDER_TYPE_TIME type_time;
    datetime expiration;
    std::string comment;
    ulong position;
    ulong position_by;
};

// Trade result
struct MqlTradeResult {
    uint retcode;
    ulong deal;
    ulong order;
    double volume;
    double price;
    double bid;
    double ask;
    std::string comment;
    uint request_id;
    int retcode_external;
};

// Trade check result
struct MqlTradeCheckResult {
    uint retcode;
    double balance;
    double equity;
    double profit;
    double margin;
    double margin_free;
    double margin_level;
    std::string comment;
};

//=============================================================================
// MQL5 Math Functions
//=============================================================================

inline double MathAbs(double value) { return std::abs(value); }
inline double MathArccos(double value) { return std::acos(value); }
inline double MathArcsin(double value) { return std::asin(value); }
inline double MathArctan(double value) { return std::atan(value); }
inline double MathCeil(double value) { return std::ceil(value); }
inline double MathCos(double value) { return std::cos(value); }
inline double MathExp(double value) { return std::exp(value); }
inline double MathFloor(double value) { return std::floor(value); }
inline double MathLog(double value) { return std::log(value); }
inline double MathLog10(double value) { return std::log10(value); }
inline double MathMax(double a, double b) { return std::max(a, b); }
inline double MathMin(double a, double b) { return std::min(a, b); }
inline double MathMod(double a, double b) { return std::fmod(a, b); }
inline double MathPow(double base, double exp) { return std::pow(base, exp); }
inline double MathRound(double value) { return std::round(value); }
inline double MathSin(double value) { return std::sin(value); }
inline double MathSqrt(double value) { return std::sqrt(value); }
inline double MathTan(double value) { return std::tan(value); }
inline bool MathIsValidNumber(double value) { return std::isfinite(value); }
inline double MathRand() { return static_cast<double>(rand()) / RAND_MAX * 32767; }
inline void MathSrand(uint seed) { srand(seed); }

inline double NormalizeDouble(double value, int digits) {
    double mult = std::pow(10.0, digits);
    return std::round(value * mult) / mult;
}

//=============================================================================
// MQL5 String Functions
//=============================================================================

inline int StringLen(const std::string& s) { return static_cast<int>(s.length()); }
inline int StringFind(const std::string& s, const std::string& sub, int start = 0) {
    size_t pos = s.find(sub, start);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
}
inline std::string StringSubstr(const std::string& s, int start, int length = -1) {
    if (length < 0) return s.substr(start);
    return s.substr(start, length);
}
inline bool StringSetCharacter(std::string& s, int pos, ushort ch) {
    if (pos >= 0 && pos < static_cast<int>(s.length())) {
        s[pos] = static_cast<char>(ch);
        return true;
    }
    return false;
}
inline std::string IntegerToString(long value, int str_len = 0, ushort fill = ' ') {
    std::string result = std::to_string(value);
    while (static_cast<int>(result.length()) < str_len) {
        result = static_cast<char>(fill) + result;
    }
    return result;
}
inline std::string DoubleToString(double value, int digits = 8) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(digits) << value;
    return oss.str();
}
inline long StringToInteger(const std::string& s) {
    return std::stol(s);
}
inline double StringToDouble(const std::string& s) {
    return std::stod(s);
}
inline std::string StringTrimLeft(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    return start == std::string::npos ? "" : s.substr(start);
}
inline std::string StringTrimRight(const std::string& s) {
    auto end = s.find_last_not_of(" \t\n\r");
    return end == std::string::npos ? "" : s.substr(0, end + 1);
}
inline int StringReplace(std::string& s, const std::string& find, const std::string& replace) {
    int count = 0;
    size_t pos = 0;
    while ((pos = s.find(find, pos)) != std::string::npos) {
        s.replace(pos, find.length(), replace);
        pos += replace.length();
        count++;
    }
    return count;
}
inline std::string StringFormat(const char* format, ...) {
    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return std::string(buffer);
}

//=============================================================================
// MQL5 DateTime Functions
//=============================================================================

inline datetime TimeCurrent() {
    return static_cast<datetime>(std::time(nullptr));
}

inline datetime TimeLocal() {
    return static_cast<datetime>(std::time(nullptr));
}

inline datetime TimeGMT() {
    return static_cast<datetime>(std::time(nullptr));
}

inline void TimeToStruct(datetime dt, MqlDateTime& dt_struct) {
    std::time_t time = static_cast<std::time_t>(dt);
    std::tm* tm = std::gmtime(&time);
    if (tm) {
        dt_struct.year = tm->tm_year + 1900;
        dt_struct.mon = tm->tm_mon + 1;
        dt_struct.day = tm->tm_mday;
        dt_struct.hour = tm->tm_hour;
        dt_struct.min = tm->tm_min;
        dt_struct.sec = tm->tm_sec;
        dt_struct.day_of_week = tm->tm_wday;
        dt_struct.day_of_year = tm->tm_yday;
    }
}

inline datetime StructToTime(const MqlDateTime& dt_struct) {
    std::tm tm = {};
    tm.tm_year = dt_struct.year - 1900;
    tm.tm_mon = dt_struct.mon - 1;
    tm.tm_mday = dt_struct.day;
    tm.tm_hour = dt_struct.hour;
    tm.tm_min = dt_struct.min;
    tm.tm_sec = dt_struct.sec;
    return static_cast<datetime>(std::mktime(&tm));
}

inline std::string TimeToString(datetime dt, int mode = 0) {
    std::time_t time = static_cast<std::time_t>(dt);
    std::tm* tm = std::gmtime(&time);
    if (!tm) return "";

    char buffer[32];
    if (mode == 0) {
        std::strftime(buffer, sizeof(buffer), "%Y.%m.%d %H:%M", tm);
    } else if (mode == 1) {
        std::strftime(buffer, sizeof(buffer), "%Y.%m.%d", tm);
    } else {
        std::strftime(buffer, sizeof(buffer), "%H:%M", tm);
    }
    return std::string(buffer);
}

inline int Year(datetime dt = 0) {
    MqlDateTime mdt;
    TimeToStruct(dt == 0 ? TimeCurrent() : dt, mdt);
    return mdt.year;
}

inline int Month(datetime dt = 0) {
    MqlDateTime mdt;
    TimeToStruct(dt == 0 ? TimeCurrent() : dt, mdt);
    return mdt.mon;
}

inline int Day(datetime dt = 0) {
    MqlDateTime mdt;
    TimeToStruct(dt == 0 ? TimeCurrent() : dt, mdt);
    return mdt.day;
}

inline int Hour(datetime dt = 0) {
    MqlDateTime mdt;
    TimeToStruct(dt == 0 ? TimeCurrent() : dt, mdt);
    return mdt.hour;
}

inline int Minute(datetime dt = 0) {
    MqlDateTime mdt;
    TimeToStruct(dt == 0 ? TimeCurrent() : dt, mdt);
    return mdt.min;
}

inline int Seconds(datetime dt = 0) {
    MqlDateTime mdt;
    TimeToStruct(dt == 0 ? TimeCurrent() : dt, mdt);
    return mdt.sec;
}

inline int DayOfWeek(datetime dt = 0) {
    MqlDateTime mdt;
    TimeToStruct(dt == 0 ? TimeCurrent() : dt, mdt);
    return mdt.day_of_week;
}

//=============================================================================
// MQL5 Array Functions
//=============================================================================

template<typename T>
inline int ArraySize(const std::vector<T>& arr) {
    return static_cast<int>(arr.size());
}

template<typename T>
inline int ArrayResize(std::vector<T>& arr, int new_size, int reserve = 0) {
    arr.resize(new_size);
    if (reserve > 0) arr.reserve(reserve);
    return static_cast<int>(arr.size());
}

template<typename T>
inline void ArrayFree(std::vector<T>& arr) {
    arr.clear();
    arr.shrink_to_fit();
}

template<typename T>
inline bool ArraySetAsSeries(std::vector<T>& arr, bool flag) {
    if (flag) {
        std::reverse(arr.begin(), arr.end());
    }
    return true;
}

template<typename T>
inline int ArrayCopy(std::vector<T>& dst, const std::vector<T>& src,
                     int dst_start = 0, int src_start = 0, int count = -1) {
    if (count < 0) count = static_cast<int>(src.size()) - src_start;
    if (dst_start + count > static_cast<int>(dst.size())) {
        dst.resize(dst_start + count);
    }
    std::copy(src.begin() + src_start, src.begin() + src_start + count,
              dst.begin() + dst_start);
    return count;
}

template<typename T>
inline int ArrayInitialize(std::vector<T>& arr, T value) {
    std::fill(arr.begin(), arr.end(), value);
    return static_cast<int>(arr.size());
}

template<typename T>
inline int ArrayMaximum(const std::vector<T>& arr, int start = 0, int count = -1) {
    if (arr.empty()) return -1;
    if (count < 0) count = static_cast<int>(arr.size()) - start;
    auto max_it = std::max_element(arr.begin() + start, arr.begin() + start + count);
    return static_cast<int>(std::distance(arr.begin(), max_it));
}

template<typename T>
inline int ArrayMinimum(const std::vector<T>& arr, int start = 0, int count = -1) {
    if (arr.empty()) return -1;
    if (count < 0) count = static_cast<int>(arr.size()) - start;
    auto min_it = std::min_element(arr.begin() + start, arr.begin() + start + count);
    return static_cast<int>(std::distance(arr.begin(), min_it));
}

template<typename T>
inline void ArraySort(std::vector<T>& arr) {
    std::sort(arr.begin(), arr.end());
}

//=============================================================================
// Technical Indicator Base Classes
//=============================================================================

class IndicatorBuffer {
public:
    std::vector<double> buffer;

    double operator[](int index) const {
        if (index >= 0 && index < static_cast<int>(buffer.size())) {
            return buffer[index];
        }
        return EMPTY_VALUE;
    }

    void resize(int size) { buffer.resize(size, EMPTY_VALUE); }
    int size() const { return static_cast<int>(buffer.size()); }
    void set(int index, double value) {
        if (index >= 0 && index < static_cast<int>(buffer.size())) {
            buffer[index] = value;
        }
    }
};

//=============================================================================
// MQL5 Technical Indicators
//=============================================================================

// Simple Moving Average
inline void iMAOnArray(const std::vector<double>& price, int period, int shift,
                       ENUM_MA_METHOD method, std::vector<double>& result) {
    int total = static_cast<int>(price.size());
    result.resize(total, EMPTY_VALUE);

    if (total < period) return;

    switch (method) {
    case MODE_SMA: {
        double sum = 0;
        for (int i = 0; i < period; ++i) sum += price[i];
        for (int i = period - 1; i < total; ++i) {
            if (i >= period) {
                sum += price[i] - price[i - period];
            }
            result[i] = sum / period;
        }
        break;
    }
    case MODE_EMA: {
        double k = 2.0 / (period + 1);
        result[0] = price[0];
        for (int i = 1; i < total; ++i) {
            result[i] = price[i] * k + result[i-1] * (1 - k);
        }
        break;
    }
    case MODE_SMMA: {
        double sum = 0;
        for (int i = 0; i < period; ++i) sum += price[i];
        result[period - 1] = sum / period;
        for (int i = period; i < total; ++i) {
            result[i] = (result[i-1] * (period - 1) + price[i]) / period;
        }
        break;
    }
    case MODE_LWMA: {
        int weight = 0;
        for (int i = 1; i <= period; ++i) weight += i;
        for (int i = period - 1; i < total; ++i) {
            double sum = 0;
            for (int j = 0; j < period; ++j) {
                sum += price[i - j] * (period - j);
            }
            result[i] = sum / weight;
        }
        break;
    }
    }
}

// RSI
inline void iRSIOnArray(const std::vector<double>& price, int period,
                        std::vector<double>& result) {
    int total = static_cast<int>(price.size());
    result.resize(total, EMPTY_VALUE);

    if (total < period + 1) return;

    double avg_gain = 0, avg_loss = 0;

    // Initial averages
    for (int i = 1; i <= period; ++i) {
        double change = price[i] - price[i-1];
        if (change > 0) avg_gain += change;
        else avg_loss -= change;
    }
    avg_gain /= period;
    avg_loss /= period;

    // First RSI value
    if (avg_loss == 0) result[period] = 100;
    else {
        double rs = avg_gain / avg_loss;
        result[period] = 100 - 100 / (1 + rs);
    }

    // Subsequent values using smoothed averages
    for (int i = period + 1; i < total; ++i) {
        double change = price[i] - price[i-1];
        double gain = change > 0 ? change : 0;
        double loss = change < 0 ? -change : 0;

        avg_gain = (avg_gain * (period - 1) + gain) / period;
        avg_loss = (avg_loss * (period - 1) + loss) / period;

        if (avg_loss == 0) result[i] = 100;
        else {
            double rs = avg_gain / avg_loss;
            result[i] = 100 - 100 / (1 + rs);
        }
    }
}

// ATR (Average True Range)
inline void iATROnArray(const std::vector<double>& high,
                        const std::vector<double>& low,
                        const std::vector<double>& close,
                        int period, std::vector<double>& result) {
    int total = static_cast<int>(close.size());
    result.resize(total, EMPTY_VALUE);

    if (total < 2) return;

    // Calculate True Range
    std::vector<double> tr(total);
    tr[0] = high[0] - low[0];

    for (int i = 1; i < total; ++i) {
        double hl = high[i] - low[i];
        double hc = std::abs(high[i] - close[i-1]);
        double lc = std::abs(low[i] - close[i-1]);
        tr[i] = std::max({hl, hc, lc});
    }

    // Calculate ATR (EMA of TR)
    iMAOnArray(tr, period, 0, MODE_EMA, result);
}

// Stochastic Oscillator
inline void iStochOnArray(const std::vector<double>& high,
                          const std::vector<double>& low,
                          const std::vector<double>& close,
                          int k_period, int d_period, int slowing,
                          std::vector<double>& main,
                          std::vector<double>& signal) {
    int total = static_cast<int>(close.size());
    main.resize(total, EMPTY_VALUE);
    signal.resize(total, EMPTY_VALUE);

    if (total < k_period) return;

    // Calculate %K
    std::vector<double> raw_k(total, EMPTY_VALUE);
    for (int i = k_period - 1; i < total; ++i) {
        double highest = high[i];
        double lowest = low[i];
        for (int j = 1; j < k_period; ++j) {
            highest = std::max(highest, high[i - j]);
            lowest = std::min(lowest, low[i - j]);
        }
        if (highest - lowest > 0) {
            raw_k[i] = (close[i] - lowest) / (highest - lowest) * 100;
        } else {
            raw_k[i] = 50;
        }
    }

    // Apply slowing (SMA of raw %K)
    if (slowing > 1) {
        iMAOnArray(raw_k, slowing, 0, MODE_SMA, main);
    } else {
        main = raw_k;
    }

    // Calculate %D (SMA of %K)
    iMAOnArray(main, d_period, 0, MODE_SMA, signal);
}

// MACD
inline void iMACDOnArray(const std::vector<double>& price,
                         int fast_period, int slow_period, int signal_period,
                         std::vector<double>& macd_main,
                         std::vector<double>& macd_signal) {
    int total = static_cast<int>(price.size());

    std::vector<double> fast_ema, slow_ema;
    iMAOnArray(price, fast_period, 0, MODE_EMA, fast_ema);
    iMAOnArray(price, slow_period, 0, MODE_EMA, slow_ema);

    macd_main.resize(total);
    for (int i = 0; i < total; ++i) {
        macd_main[i] = fast_ema[i] - slow_ema[i];
    }

    iMAOnArray(macd_main, signal_period, 0, MODE_EMA, macd_signal);
}

// Bollinger Bands
inline void iBandsOnArray(const std::vector<double>& price,
                          int period, double deviation,
                          std::vector<double>& upper,
                          std::vector<double>& middle,
                          std::vector<double>& lower) {
    int total = static_cast<int>(price.size());
    upper.resize(total, EMPTY_VALUE);
    middle.resize(total, EMPTY_VALUE);
    lower.resize(total, EMPTY_VALUE);

    iMAOnArray(price, period, 0, MODE_SMA, middle);

    for (int i = period - 1; i < total; ++i) {
        double sum_sq = 0;
        for (int j = 0; j < period; ++j) {
            double diff = price[i - j] - middle[i];
            sum_sq += diff * diff;
        }
        double std_dev = std::sqrt(sum_sq / period);
        upper[i] = middle[i] + deviation * std_dev;
        lower[i] = middle[i] - deviation * std_dev;
    }
}

// CCI (Commodity Channel Index)
inline void iCCIOnArray(const std::vector<double>& high,
                        const std::vector<double>& low,
                        const std::vector<double>& close,
                        int period, std::vector<double>& result) {
    int total = static_cast<int>(close.size());
    result.resize(total, EMPTY_VALUE);

    // Calculate typical price
    std::vector<double> tp(total);
    for (int i = 0; i < total; ++i) {
        tp[i] = (high[i] + low[i] + close[i]) / 3.0;
    }

    // Calculate SMA of typical price
    std::vector<double> tp_sma;
    iMAOnArray(tp, period, 0, MODE_SMA, tp_sma);

    // Calculate mean deviation and CCI
    for (int i = period - 1; i < total; ++i) {
        double mean_dev = 0;
        for (int j = 0; j < period; ++j) {
            mean_dev += std::abs(tp[i - j] - tp_sma[i]);
        }
        mean_dev /= period;

        if (mean_dev > 0) {
            result[i] = (tp[i] - tp_sma[i]) / (0.015 * mean_dev);
        } else {
            result[i] = 0;
        }
    }
}

// ADX (Average Directional Index)
inline void iADXOnArray(const std::vector<double>& high,
                        const std::vector<double>& low,
                        const std::vector<double>& close,
                        int period,
                        std::vector<double>& adx,
                        std::vector<double>& plus_di,
                        std::vector<double>& minus_di) {
    int total = static_cast<int>(close.size());
    adx.resize(total, EMPTY_VALUE);
    plus_di.resize(total, EMPTY_VALUE);
    minus_di.resize(total, EMPTY_VALUE);

    if (total < period + 1) return;

    // Calculate +DM, -DM, TR
    std::vector<double> plus_dm(total), minus_dm(total), tr(total);

    for (int i = 1; i < total; ++i) {
        double up_move = high[i] - high[i-1];
        double down_move = low[i-1] - low[i];

        plus_dm[i] = (up_move > down_move && up_move > 0) ? up_move : 0;
        minus_dm[i] = (down_move > up_move && down_move > 0) ? down_move : 0;

        double hl = high[i] - low[i];
        double hc = std::abs(high[i] - close[i-1]);
        double lc = std::abs(low[i] - close[i-1]);
        tr[i] = std::max({hl, hc, lc});
    }

    // Smooth +DM, -DM, TR
    std::vector<double> smooth_plus_dm, smooth_minus_dm, smooth_tr;
    iMAOnArray(plus_dm, period, 0, MODE_EMA, smooth_plus_dm);
    iMAOnArray(minus_dm, period, 0, MODE_EMA, smooth_minus_dm);
    iMAOnArray(tr, period, 0, MODE_EMA, smooth_tr);

    // Calculate +DI, -DI
    std::vector<double> dx(total, EMPTY_VALUE);
    for (int i = period; i < total; ++i) {
        if (smooth_tr[i] > 0) {
            plus_di[i] = 100 * smooth_plus_dm[i] / smooth_tr[i];
            minus_di[i] = 100 * smooth_minus_dm[i] / smooth_tr[i];
        }

        double di_sum = plus_di[i] + minus_di[i];
        if (di_sum > 0) {
            dx[i] = 100 * std::abs(plus_di[i] - minus_di[i]) / di_sum;
        }
    }

    // Calculate ADX (smoothed DX)
    iMAOnArray(dx, period, 0, MODE_EMA, adx);
}

//=============================================================================
// Trade Result Codes
//=============================================================================

constexpr uint TRADE_RETCODE_REQUOTE = 10004;
constexpr uint TRADE_RETCODE_REJECT = 10006;
constexpr uint TRADE_RETCODE_CANCEL = 10007;
constexpr uint TRADE_RETCODE_PLACED = 10008;
constexpr uint TRADE_RETCODE_DONE = 10009;
constexpr uint TRADE_RETCODE_DONE_PARTIAL = 10010;
constexpr uint TRADE_RETCODE_ERROR = 10011;
constexpr uint TRADE_RETCODE_TIMEOUT = 10012;
constexpr uint TRADE_RETCODE_INVALID = 10013;
constexpr uint TRADE_RETCODE_INVALID_VOLUME = 10014;
constexpr uint TRADE_RETCODE_INVALID_PRICE = 10015;
constexpr uint TRADE_RETCODE_INVALID_STOPS = 10016;
constexpr uint TRADE_RETCODE_TRADE_DISABLED = 10017;
constexpr uint TRADE_RETCODE_MARKET_CLOSED = 10018;
constexpr uint TRADE_RETCODE_NO_MONEY = 10019;
constexpr uint TRADE_RETCODE_PRICE_CHANGED = 10020;
constexpr uint TRADE_RETCODE_PRICE_OFF = 10021;
constexpr uint TRADE_RETCODE_INVALID_EXPIRATION = 10022;
constexpr uint TRADE_RETCODE_ORDER_CHANGED = 10023;
constexpr uint TRADE_RETCODE_TOO_MANY_REQUESTS = 10024;
constexpr uint TRADE_RETCODE_NO_CHANGES = 10025;
constexpr uint TRADE_RETCODE_SERVER_DISABLES_AT = 10026;
constexpr uint TRADE_RETCODE_CLIENT_DISABLES_AT = 10027;
constexpr uint TRADE_RETCODE_LOCKED = 10028;
constexpr uint TRADE_RETCODE_FROZEN = 10029;
constexpr uint TRADE_RETCODE_INVALID_FILL = 10030;
constexpr uint TRADE_RETCODE_CONNECTION = 10031;
constexpr uint TRADE_RETCODE_ONLY_REAL = 10032;
constexpr uint TRADE_RETCODE_LIMIT_ORDERS = 10033;
constexpr uint TRADE_RETCODE_LIMIT_VOLUME = 10034;
constexpr uint TRADE_RETCODE_INVALID_ORDER = 10035;
constexpr uint TRADE_RETCODE_POSITION_CLOSED = 10036;

//=============================================================================
// Print / Alert Functions (for debugging)
//=============================================================================

template<typename... Args>
inline void Print(Args&&... args) {
    std::ostringstream oss;
    ((oss << args), ...);
    std::cout << oss.str() << std::endl;
}

template<typename... Args>
inline void Alert(Args&&... args) {
    Print(std::forward<Args>(args)...);
}

template<typename... Args>
inline void Comment(Args&&... args) {
    Print(std::forward<Args>(args)...);
}

//=============================================================================
// Sleep function
//=============================================================================

inline void Sleep(uint milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

//=============================================================================
// Error handling
//=============================================================================

inline int GetLastError() {
    // In a real implementation, this would track the last error
    return 0;
}

inline void ResetLastError() {
    // In a real implementation, this would reset the error state
}

} // namespace mql5

#endif // MQL5_COMPAT_H
