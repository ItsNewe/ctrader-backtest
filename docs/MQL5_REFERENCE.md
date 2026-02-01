# MQL5 Language Reference for C++ Reimplementation

This document provides a comprehensive reference of MQL5 language features relevant for backtesting, organized for C++ reimplementation.

---

## Table of Contents

1. [Data Types](#1-data-types)
2. [Core Structures](#2-core-structures)
3. [Enumerations](#3-enumerations)
4. [Math Functions](#4-math-functions)
5. [Array Functions](#5-array-functions)
6. [String Functions](#6-string-functions)
7. [Conversion Functions](#7-conversion-functions)
8. [Date and Time Functions](#8-date-and-time-functions)
9. [Account Information Functions](#9-account-information-functions)
10. [Symbol/Market Information Functions](#10-symbolmarket-information-functions)
11. [Timeseries and Indicator Access Functions](#11-timeseries-and-indicator-access-functions)
12. [Technical Indicator Functions](#12-technical-indicator-functions)
13. [Trade Functions](#13-trade-functions)
14. [Common Functions](#14-common-functions)
15. [File Functions](#15-file-functions)
16. [Checkup Functions](#16-checkup-functions)

---

## 1. Data Types

### Integer Types
| MQL5 Type | C++ Equivalent | Size | Range |
|-----------|---------------|------|-------|
| `char` | `int8_t` | 1 byte | -128 to 127 |
| `uchar` | `uint8_t` | 1 byte | 0 to 255 |
| `short` | `int16_t` | 2 bytes | -32,768 to 32,767 |
| `ushort` | `uint16_t` | 2 bytes | 0 to 65,535 |
| `int` | `int32_t` | 4 bytes | -2^31 to 2^31-1 |
| `uint` | `uint32_t` | 4 bytes | 0 to 2^32-1 |
| `long` | `int64_t` | 8 bytes | -2^63 to 2^63-1 |
| `ulong` | `uint64_t` | 8 bytes | 0 to 2^64-1 |

### Floating-Point Types
| MQL5 Type | C++ Equivalent | Size | Precision |
|-----------|---------------|------|-----------|
| `float` | `float` | 4 bytes | ~7 digits |
| `double` | `double` | 8 bytes | ~15 digits |

### Special Types
| MQL5 Type | C++ Equivalent | Description |
|-----------|---------------|-------------|
| `bool` | `bool` | true/false |
| `string` | `std::string` | Dynamic string |
| `datetime` | `int64_t` | Seconds since 1970-01-01 00:00:00 |
| `color` | `uint32_t` | RGB color (0xBBGGRR) |

---

## 2. Core Structures

### MqlTick
```cpp
struct MqlTick {
    datetime time;          // Time of the last prices update
    double   bid;           // Current Bid price
    double   ask;           // Current Ask price
    double   last;          // Price of the last deal (Last)
    ulong    volume;        // Volume for the current Last price
    long     time_msc;      // Time in milliseconds
    uint     flags;         // Tick flags
    double   volume_real;   // Volume with greater accuracy
};
```

### MqlRates
```cpp
struct MqlRates {
    datetime time;          // Period start time
    double   open;          // Open price
    double   high;          // Highest price of the period
    double   low;           // Lowest price of the period
    double   close;         // Close price
    long     tick_volume;   // Tick volume
    int      spread;        // Spread
    long     real_volume;   // Trade volume
};
```

### MqlDateTime
```cpp
struct MqlDateTime {
    int year;           // Year
    int mon;            // Month (1-12)
    int day;            // Day (1-31)
    int hour;           // Hour (0-23)
    int min;            // Minutes (0-59)
    int sec;            // Seconds (0-59)
    int day_of_week;    // Day of week (0=Sunday, 6=Saturday)
    int day_of_year;    // Day number of the year (0-365)
};
```

### MqlTradeRequest
```cpp
struct MqlTradeRequest {
    ENUM_TRADE_REQUEST_ACTIONS action;      // Trade operation type
    ulong                      magic;       // Expert Advisor ID (magic number)
    ulong                      order;       // Order ticket
    string                     symbol;      // Trade symbol
    double                     volume;      // Requested volume in lots
    double                     price;       // Price
    double                     stoplimit;   // StopLimit level
    double                     sl;          // Stop Loss level
    double                     tp;          // Take Profit level
    ulong                      deviation;   // Max deviation from price
    ENUM_ORDER_TYPE            type;        // Order type
    ENUM_ORDER_TYPE_FILLING    type_filling;// Order execution type
    ENUM_ORDER_TYPE_TIME       type_time;   // Order expiration type
    datetime                   expiration;  // Order expiration time
    string                     comment;     // Order comment
    ulong                      position;    // Position ticket
    ulong                      position_by; // Opposite position ticket
};
```

### MqlTradeResult
```cpp
struct MqlTradeResult {
    uint     retcode;          // Operation return code
    ulong    deal;             // Deal ticket, if performed
    ulong    order;            // Order ticket, if placed
    double   volume;           // Deal volume, confirmed by broker
    double   price;            // Deal price, confirmed by broker
    double   bid;              // Current Bid price
    double   ask;              // Current Ask price
    string   comment;          // Broker comment to operation
    uint     request_id;       // Request ID set by terminal
    int      retcode_external; // Return code of external system
};
```

---

## 3. Enumerations

### ENUM_TIMEFRAMES
```cpp
enum ENUM_TIMEFRAMES {
    PERIOD_CURRENT = 0,     // Current timeframe
    PERIOD_M1      = 1,     // 1 minute
    PERIOD_M2      = 2,     // 2 minutes
    PERIOD_M3      = 3,     // 3 minutes
    PERIOD_M4      = 4,     // 4 minutes
    PERIOD_M5      = 5,     // 5 minutes
    PERIOD_M6      = 6,     // 6 minutes
    PERIOD_M10     = 10,    // 10 minutes
    PERIOD_M12     = 12,    // 12 minutes
    PERIOD_M15     = 15,    // 15 minutes
    PERIOD_M20     = 20,    // 20 minutes
    PERIOD_M30     = 30,    // 30 minutes
    PERIOD_H1      = 0x4001,// 1 hour (16385)
    PERIOD_H2      = 0x4002,// 2 hours
    PERIOD_H3      = 0x4003,// 3 hours
    PERIOD_H4      = 0x4004,// 4 hours
    PERIOD_H6      = 0x4006,// 6 hours
    PERIOD_H8      = 0x4008,// 8 hours
    PERIOD_H12     = 0x400C,// 12 hours
    PERIOD_D1      = 0x4018,// 1 day (16408)
    PERIOD_W1      = 0x8001,// 1 week (32769)
    PERIOD_MN1     = 0xC001 // 1 month (49153)
};
```

### ENUM_ORDER_TYPE
```cpp
enum ENUM_ORDER_TYPE {
    ORDER_TYPE_BUY             = 0,  // Market buy order
    ORDER_TYPE_SELL            = 1,  // Market sell order
    ORDER_TYPE_BUY_LIMIT       = 2,  // Buy Limit pending order
    ORDER_TYPE_SELL_LIMIT      = 3,  // Sell Limit pending order
    ORDER_TYPE_BUY_STOP        = 4,  // Buy Stop pending order
    ORDER_TYPE_SELL_STOP       = 5,  // Sell Stop pending order
    ORDER_TYPE_BUY_STOP_LIMIT  = 6,  // Buy Stop Limit pending order
    ORDER_TYPE_SELL_STOP_LIMIT = 7,  // Sell Stop Limit pending order
    ORDER_TYPE_CLOSE_BY        = 8   // Close position by opposite one
};
```

### ENUM_POSITION_TYPE
```cpp
enum ENUM_POSITION_TYPE {
    POSITION_TYPE_BUY  = 0,  // Buy position (long)
    POSITION_TYPE_SELL = 1   // Sell position (short)
};
```

### ENUM_TRADE_REQUEST_ACTIONS
```cpp
enum ENUM_TRADE_REQUEST_ACTIONS {
    TRADE_ACTION_DEAL     = 1,  // Place market order
    TRADE_ACTION_PENDING  = 5,  // Place pending order
    TRADE_ACTION_SLTP     = 6,  // Modify SL/TP
    TRADE_ACTION_MODIFY   = 7,  // Modify pending order
    TRADE_ACTION_REMOVE   = 8,  // Delete pending order
    TRADE_ACTION_CLOSE_BY = 10  // Close by opposite position
};
```

### ENUM_ORDER_TYPE_FILLING
```cpp
enum ENUM_ORDER_TYPE_FILLING {
    ORDER_FILLING_FOK    = 0,  // Fill or Kill
    ORDER_FILLING_IOC    = 1,  // Immediate or Cancel
    ORDER_FILLING_RETURN = 2   // Partial filling allowed
};
```

### ENUM_ORDER_TYPE_TIME
```cpp
enum ENUM_ORDER_TYPE_TIME {
    ORDER_TIME_GTC           = 0,  // Good till cancel
    ORDER_TIME_DAY           = 1,  // Good till current trade day
    ORDER_TIME_SPECIFIED     = 2,  // Good till expired
    ORDER_TIME_SPECIFIED_DAY = 3   // Effective till 23:59:59 of specified day
};
```

### ENUM_MA_METHOD
```cpp
enum ENUM_MA_METHOD {
    MODE_SMA  = 0,  // Simple Moving Average
    MODE_EMA  = 1,  // Exponential Moving Average
    MODE_SMMA = 2,  // Smoothed Moving Average
    MODE_LWMA = 3   // Linear Weighted Moving Average
};
```

### ENUM_APPLIED_PRICE
```cpp
enum ENUM_APPLIED_PRICE {
    PRICE_CLOSE    = 1,  // Close price
    PRICE_OPEN     = 2,  // Open price
    PRICE_HIGH     = 3,  // High price
    PRICE_LOW      = 4,  // Low price
    PRICE_MEDIAN   = 5,  // (High + Low) / 2
    PRICE_TYPICAL  = 6,  // (High + Low + Close) / 3
    PRICE_WEIGHTED = 7   // (High + Low + Close + Close) / 4
};
```

### ENUM_APPLIED_VOLUME
```cpp
enum ENUM_APPLIED_VOLUME {
    VOLUME_TICK = 0,  // Tick volume
    VOLUME_REAL = 1   // Trade volume
};
```

---

## 4. Math Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `MathAbs` | `double MathAbs(double value)` | Absolute value |
| `MathArccos` | `double MathArccos(double value)` | Arc cosine in radians |
| `MathArcsin` | `double MathArcsin(double value)` | Arc sine in radians |
| `MathArctan` | `double MathArctan(double value)` | Arc tangent in radians |
| `MathArctan2` | `double MathArctan2(double y, double x)` | Angle from two coordinates |
| `MathCeil` | `double MathCeil(double value)` | Round up to nearest integer |
| `MathCos` | `double MathCos(double value)` | Cosine |
| `MathExp` | `double MathExp(double value)` | e^value |
| `MathFloor` | `double MathFloor(double value)` | Round down to nearest integer |
| `MathLog` | `double MathLog(double value)` | Natural logarithm |
| `MathLog10` | `double MathLog10(double value)` | Base-10 logarithm |
| `MathMax` | `double MathMax(double a, double b)` | Maximum of two values |
| `MathMin` | `double MathMin(double a, double b)` | Minimum of two values |
| `MathMod` | `double MathMod(double a, double b)` | Remainder after division |
| `MathPow` | `double MathPow(double base, double exp)` | Power function |
| `MathRand` | `int MathRand()` | Random 0 to 32767 |
| `MathRound` | `double MathRound(double value)` | Round to nearest integer |
| `MathSin` | `double MathSin(double value)` | Sine |
| `MathSqrt` | `double MathSqrt(double value)` | Square root |
| `MathSrand` | `void MathSrand(int seed)` | Set random seed |
| `MathTan` | `double MathTan(double value)` | Tangent |
| `MathIsValidNumber` | `bool MathIsValidNumber(double value)` | Check for NaN/Inf |
| `MathExpm1` | `double MathExpm1(double value)` | exp(x) - 1 |
| `MathLog1p` | `double MathLog1p(double value)` | log(1 + x) |
| `MathCosh` | `double MathCosh(double value)` | Hyperbolic cosine |
| `MathSinh` | `double MathSinh(double value)` | Hyperbolic sine |
| `MathTanh` | `double MathTanh(double value)` | Hyperbolic tangent |
| `MathArccosh` | `double MathArccosh(double value)` | Hyperbolic arc cosine |
| `MathArcsinh` | `double MathArcsinh(double value)` | Hyperbolic arc sine |
| `MathArctanh` | `double MathArctanh(double value)` | Hyperbolic arc tangent |

### C++ Equivalents
```cpp
#include <cmath>
// MathAbs   -> std::abs or std::fabs
// MathCeil  -> std::ceil
// MathFloor -> std::floor
// MathLog   -> std::log
// MathPow   -> std::pow
// MathSqrt  -> std::sqrt
// etc.
```

---

## 5. Array Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `ArrayBsearch` | `int ArrayBsearch(array[], value)` | Binary search, returns index |
| `ArrayCopy` | `int ArrayCopy(dst[], src[], dst_start, src_start, count)` | Copy elements between arrays |
| `ArrayCompare` | `int ArrayCompare(arr1[], arr2[], start1, start2, count)` | Compare arrays (-1, 0, 1) |
| `ArrayFill` | `void ArrayFill(array[], start, count, value)` | Fill with value |
| `ArrayFree` | `void ArrayFree(array[])` | Free dynamic array memory |
| `ArrayGetAsSeries` | `bool ArrayGetAsSeries(array[])` | Check if indexed as series |
| `ArrayInitialize` | `int ArrayInitialize(array[], value)` | Initialize all elements |
| `ArrayInsert` | `int ArrayInsert(dst[], src[], dst_start, src_start, count)` | Insert elements |
| `ArrayIsDynamic` | `bool ArrayIsDynamic(array[])` | Check if dynamic |
| `ArrayIsSeries` | `bool ArrayIsSeries(array[])` | Check if timeseries |
| `ArrayMaximum` | `int ArrayMaximum(array[], start, count)` | Index of maximum |
| `ArrayMinimum` | `int ArrayMinimum(array[], start, count)` | Index of minimum |
| `ArrayPrint` | `void ArrayPrint(array[], digits, separator)` | Print to log |
| `ArrayRange` | `int ArrayRange(array[], dimension)` | Size of dimension |
| `ArrayRemove` | `bool ArrayRemove(array[], start, count)` | Remove elements |
| `ArrayResize` | `int ArrayResize(array[], new_size, reserve)` | Resize first dimension |
| `ArrayReverse` | `bool ArrayReverse(array[], start, count)` | Reverse elements |
| `ArraySetAsSeries` | `bool ArraySetAsSeries(array[], flag)` | Set series indexing |
| `ArraySize` | `int ArraySize(array[])` | Total element count |
| `ArraySort` | `bool ArraySort(array[])` | Sort ascending |
| `ArraySwap` | `bool ArraySwap(array1[], array2[])` | Swap array contents |

### C++ Equivalents
```cpp
#include <vector>
#include <algorithm>
// ArraySize     -> vec.size()
// ArrayResize   -> vec.resize()
// ArrayCopy     -> std::copy()
// ArraySort     -> std::sort()
// ArrayMaximum  -> std::max_element() - vec.begin()
// ArrayMinimum  -> std::min_element() - vec.begin()
// ArrayFill     -> std::fill()
// ArrayReverse  -> std::reverse()
// ArrayBsearch  -> std::lower_bound()
```

---

## 6. String Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `StringAdd` | `bool StringAdd(string& str, string add)` | Append to string |
| `StringBufferLen` | `int StringBufferLen(string str)` | Buffer size allocated |
| `StringCompare` | `int StringCompare(str1, str2, case_sensitive)` | Compare (-1, 0, 1) |
| `StringConcatenate` | `int StringConcatenate(string& out, ...)` | Concatenate multiple |
| `StringFill` | `bool StringFill(string& str, ushort character)` | Fill with character |
| `StringFind` | `int StringFind(str, substring, start_pos)` | Find substring (-1 if not found) |
| `StringGetCharacter` | `ushort StringGetCharacter(str, pos)` | Get character code at position |
| `StringInit` | `bool StringInit(string& str, int size, ushort char)` | Initialize with character |
| `StringLen` | `int StringLen(string str)` | Character count |
| `StringReplace` | `int StringReplace(string& str, find, replace)` | Replace all occurrences |
| `StringReserve` | `bool StringReserve(string& str, int size)` | Pre-allocate buffer |
| `StringSetCharacter` | `bool StringSetCharacter(string& str, pos, char)` | Set character at position |
| `StringSetLength` | `bool StringSetLength(string& str, int length)` | Set string length |
| `StringSplit` | `int StringSplit(str, separator, result[])` | Split into array |
| `StringSubstr` | `string StringSubstr(str, start, length)` | Extract substring |
| `StringToLower` | `bool StringToLower(string& str)` | Convert to lowercase |
| `StringToUpper` | `bool StringToUpper(string& str)` | Convert to uppercase |
| `StringTrimLeft` | `int StringTrimLeft(string& str)` | Remove leading whitespace |
| `StringTrimRight` | `int StringTrimRight(string& str)` | Remove trailing whitespace |

### C++ Equivalents
```cpp
#include <string>
#include <algorithm>
// StringLen       -> str.length() or str.size()
// StringFind      -> str.find()
// StringSubstr    -> str.substr()
// StringAdd       -> str += other
// StringCompare   -> str.compare()
// StringToLower   -> std::transform with ::tolower
// StringToUpper   -> std::transform with ::toupper
```

---

## 7. Conversion Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `CharToString` | `string CharToString(uchar code)` | Character code to string |
| `CharArrayToString` | `string CharArrayToString(uchar arr[], start, count, codepage)` | Char array to string |
| `CharArrayToStruct` | `bool CharArrayToStruct(struct& s, uchar arr[], start)` | Array to structure |
| `ColorToARGB` | `uint ColorToARGB(color clr, uchar alpha)` | Color to ARGB format |
| `ColorToString` | `string ColorToString(color clr, bool color_name)` | Color to string |
| `DoubleToString` | `string DoubleToString(double value, int digits)` | Double to string |
| `EnumToString` | `string EnumToString(enum_value)` | Enum value to string |
| `IntegerToString` | `string IntegerToString(long value, int length, ushort fill)` | Integer to string |
| `NormalizeDouble` | `double NormalizeDouble(double value, int digits)` | Round to precision |
| `ShortToString` | `string ShortToString(ushort code)` | Unicode to string |
| `ShortArrayToString` | `string ShortArrayToString(ushort arr[], start, count)` | Short array to string |
| `StringFormat` | `string StringFormat(format, ...)` | Printf-style formatting |
| `StringToCharArray` | `int StringToCharArray(str, uchar arr[], start, count, codepage)` | String to char array |
| `StringToColor` | `color StringToColor(string str)` | String to color |
| `StringToDouble` | `double StringToDouble(string str)` | String to double |
| `StringToInteger` | `long StringToInteger(string str)` | String to integer |
| `StringToShortArray` | `int StringToShortArray(str, ushort arr[], start, count)` | String to short array |
| `StringToTime` | `datetime StringToTime(string str)` | String to datetime |
| `StructToCharArray` | `bool StructToCharArray(struct& s, uchar arr[], start)` | Structure to array |
| `TimeToString` | `string TimeToString(datetime time, int mode)` | Datetime to string |

### C++ Equivalents
```cpp
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdio>
// DoubleToString   -> std::to_string() or snprintf
// IntegerToString  -> std::to_string()
// StringToDouble   -> std::stod()
// StringToInteger  -> std::stoll()
// StringFormat     -> snprintf()
// NormalizeDouble  -> round(value * pow(10, digits)) / pow(10, digits)
```

---

## 8. Date and Time Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `TimeCurrent` | `datetime TimeCurrent()` | Last known server time |
| `TimeTradeServer` | `datetime TimeTradeServer()` | Current trade server time |
| `TimeLocal` | `datetime TimeLocal()` | Local computer time |
| `TimeGMT` | `datetime TimeGMT()` | GMT time |
| `TimeDaylightSavings` | `int TimeDaylightSavings()` | DST correction in seconds |
| `TimeGMTOffset` | `int TimeGMTOffset()` | GMT offset in seconds |
| `TimeToStruct` | `bool TimeToStruct(datetime time, MqlDateTime& dt)` | Datetime to structure |
| `StructToTime` | `datetime StructToTime(MqlDateTime& dt)` | Structure to datetime |

### C++ Implementation
```cpp
#include <ctime>
#include <chrono>

// datetime is seconds since 1970-01-01 00:00:00 UTC
using datetime = int64_t;

datetime TimeCurrent() {
    return std::time(nullptr);
}

bool TimeToStruct(datetime time, MqlDateTime& dt) {
    std::tm* tm = std::gmtime(&time);
    dt.year = tm->tm_year + 1900;
    dt.mon = tm->tm_mon + 1;
    dt.day = tm->tm_mday;
    dt.hour = tm->tm_hour;
    dt.min = tm->tm_min;
    dt.sec = tm->tm_sec;
    dt.day_of_week = tm->tm_wday;
    dt.day_of_year = tm->tm_yday;
    return true;
}
```

---

## 9. Account Information Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `AccountInfoDouble` | `double AccountInfoDouble(ENUM_ACCOUNT_INFO_DOUBLE prop)` | Get double property |
| `AccountInfoInteger` | `long AccountInfoInteger(ENUM_ACCOUNT_INFO_INTEGER prop)` | Get integer property |
| `AccountInfoString` | `string AccountInfoString(ENUM_ACCOUNT_INFO_STRING prop)` | Get string property |

### ENUM_ACCOUNT_INFO_INTEGER
| Constant | Description |
|----------|-------------|
| `ACCOUNT_LOGIN` | Account number |
| `ACCOUNT_TRADE_MODE` | Demo/Contest/Real |
| `ACCOUNT_LEVERAGE` | Account leverage |
| `ACCOUNT_LIMIT_ORDERS` | Max pending orders |
| `ACCOUNT_MARGIN_SO_MODE` | Stop Out mode |
| `ACCOUNT_TRADE_ALLOWED` | Trading allowed |
| `ACCOUNT_TRADE_EXPERT` | EA trading allowed |
| `ACCOUNT_MARGIN_MODE` | Margin calculation mode |
| `ACCOUNT_CURRENCY_DIGITS` | Currency decimal places |
| `ACCOUNT_FIFO_CLOSE` | FIFO close rule |
| `ACCOUNT_HEDGE_ALLOWED` | Hedging allowed |

### ENUM_ACCOUNT_INFO_DOUBLE
| Constant | Description |
|----------|-------------|
| `ACCOUNT_BALANCE` | Account balance |
| `ACCOUNT_CREDIT` | Account credit |
| `ACCOUNT_PROFIT` | Current profit |
| `ACCOUNT_EQUITY` | Account equity |
| `ACCOUNT_MARGIN` | Used margin |
| `ACCOUNT_MARGIN_FREE` | Free margin |
| `ACCOUNT_MARGIN_LEVEL` | Margin level % |
| `ACCOUNT_MARGIN_SO_CALL` | Margin call level |
| `ACCOUNT_MARGIN_SO_SO` | Stop out level |
| `ACCOUNT_MARGIN_INITIAL` | Initial margin |
| `ACCOUNT_MARGIN_MAINTENANCE` | Maintenance margin |
| `ACCOUNT_ASSETS` | Current assets |
| `ACCOUNT_LIABILITIES` | Current liabilities |
| `ACCOUNT_COMMISSION_BLOCKED` | Blocked commission |

### ENUM_ACCOUNT_INFO_STRING
| Constant | Description |
|----------|-------------|
| `ACCOUNT_NAME` | Client name |
| `ACCOUNT_SERVER` | Trade server name |
| `ACCOUNT_CURRENCY` | Account currency |
| `ACCOUNT_COMPANY` | Broker company name |

---

## 10. Symbol/Market Information Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `SymbolInfoDouble` | `double SymbolInfoDouble(string symbol, ENUM_SYMBOL_INFO_DOUBLE prop)` | Get double property |
| `SymbolInfoInteger` | `long SymbolInfoInteger(string symbol, ENUM_SYMBOL_INFO_INTEGER prop)` | Get integer property |
| `SymbolInfoString` | `string SymbolInfoString(string symbol, ENUM_SYMBOL_INFO_STRING prop)` | Get string property |
| `SymbolInfoTick` | `bool SymbolInfoTick(string symbol, MqlTick& tick)` | Get last tick |
| `SymbolInfoMarginRate` | `bool SymbolInfoMarginRate(symbol, order_type, init_margin, maint_margin)` | Get margin rates |
| `SymbolInfoSessionQuote` | `bool SymbolInfoSessionQuote(symbol, day, session, from, to)` | Get quote session |
| `SymbolInfoSessionTrade` | `bool SymbolInfoSessionTrade(symbol, day, session, from, to)` | Get trade session |
| `SymbolIsSynchronized` | `bool SymbolIsSynchronized(string symbol)` | Check sync status |
| `SymbolName` | `string SymbolName(int index, bool selected)` | Get symbol by index |
| `SymbolSelect` | `bool SymbolSelect(string symbol, bool select)` | Select in Market Watch |
| `SymbolsTotal` | `int SymbolsTotal(bool selected)` | Count of symbols |
| `MarketBookAdd` | `bool MarketBookAdd(string symbol)` | Open Depth of Market |
| `MarketBookGet` | `bool MarketBookGet(string symbol, MqlBookInfo& book[])` | Get DOM data |
| `MarketBookRelease` | `bool MarketBookRelease(string symbol)` | Close DOM |

### Key ENUM_SYMBOL_INFO_DOUBLE
| Constant | Description |
|----------|-------------|
| `SYMBOL_BID` | Current Bid price |
| `SYMBOL_ASK` | Current Ask price |
| `SYMBOL_LAST` | Last deal price |
| `SYMBOL_POINT` | Symbol point size |
| `SYMBOL_TRADE_TICK_VALUE` | Tick value in deposit currency |
| `SYMBOL_TRADE_TICK_VALUE_PROFIT` | Tick value for profitable position |
| `SYMBOL_TRADE_TICK_VALUE_LOSS` | Tick value for losing position |
| `SYMBOL_TRADE_TICK_SIZE` | Minimum price change |
| `SYMBOL_TRADE_CONTRACT_SIZE` | Contract size |
| `SYMBOL_VOLUME_MIN` | Minimum volume |
| `SYMBOL_VOLUME_MAX` | Maximum volume |
| `SYMBOL_VOLUME_STEP` | Volume step |
| `SYMBOL_SWAP_LONG` | Long swap value |
| `SYMBOL_SWAP_SHORT` | Short swap value |
| `SYMBOL_MARGIN_INITIAL` | Initial margin |
| `SYMBOL_MARGIN_MAINTENANCE` | Maintenance margin |
| `SYMBOL_MARGIN_HEDGED` | Hedged margin |

### Key ENUM_SYMBOL_INFO_INTEGER
| Constant | Description |
|----------|-------------|
| `SYMBOL_DIGITS` | Number of decimal places |
| `SYMBOL_SPREAD` | Spread in points |
| `SYMBOL_SPREAD_FLOAT` | Floating spread flag |
| `SYMBOL_TRADE_CALC_MODE` | Profit calculation mode |
| `SYMBOL_TRADE_MODE` | Trade execution mode |
| `SYMBOL_TRADE_STOPS_LEVEL` | Min stop level in points |
| `SYMBOL_TRADE_FREEZE_LEVEL` | Freeze level in points |
| `SYMBOL_TRADE_EXEMODE` | Deal execution mode |
| `SYMBOL_SWAP_MODE` | Swap calculation mode |
| `SYMBOL_SWAP_ROLLOVER3DAYS` | Triple swap day |
| `SYMBOL_TIME` | Time of last quote |

### Key ENUM_SYMBOL_INFO_STRING
| Constant | Description |
|----------|-------------|
| `SYMBOL_CURRENCY_BASE` | Base currency |
| `SYMBOL_CURRENCY_PROFIT` | Profit currency |
| `SYMBOL_CURRENCY_MARGIN` | Margin currency |
| `SYMBOL_DESCRIPTION` | Symbol description |
| `SYMBOL_PATH` | Path in symbol tree |

---

## 11. Timeseries and Indicator Access Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Bars` | `int Bars(symbol, timeframe)` | Number of bars in history |
| `BarsCalculated` | `int BarsCalculated(int handle)` | Calculated bars count |
| `CopyBuffer` | `int CopyBuffer(handle, buffer_num, start, count, buffer[])` | Copy indicator buffer |
| `CopyRates` | `int CopyRates(symbol, timeframe, start, count, rates[])` | Copy OHLCV data |
| `CopyTime` | `int CopyTime(symbol, timeframe, start, count, time[])` | Copy bar times |
| `CopyOpen` | `int CopyOpen(symbol, timeframe, start, count, open[])` | Copy open prices |
| `CopyHigh` | `int CopyHigh(symbol, timeframe, start, count, high[])` | Copy high prices |
| `CopyLow` | `int CopyLow(symbol, timeframe, start, count, low[])` | Copy low prices |
| `CopyClose` | `int CopyClose(symbol, timeframe, start, count, close[])` | Copy close prices |
| `CopyTickVolume` | `int CopyTickVolume(symbol, timeframe, start, count, volume[])` | Copy tick volumes |
| `CopyRealVolume` | `int CopyRealVolume(symbol, timeframe, start, count, volume[])` | Copy real volumes |
| `CopySpread` | `int CopySpread(symbol, timeframe, start, count, spread[])` | Copy spreads |
| `CopyTicks` | `int CopyTicks(symbol, ticks[], flags, from, count)` | Copy tick data |
| `CopyTicksRange` | `int CopyTicksRange(symbol, ticks[], flags, from, to)` | Copy ticks in range |
| `IndicatorCreate` | `int IndicatorCreate(symbol, timeframe, type, params_count, params[])` | Create indicator |
| `IndicatorParameters` | `int IndicatorParameters(handle, type, params[])` | Get indicator params |
| `IndicatorRelease` | `bool IndicatorRelease(int handle)` | Release indicator |
| `SeriesInfoInteger` | `long SeriesInfoInteger(symbol, timeframe, prop)` | Series information |

### Legacy Access Functions (by shift)
| Function | Signature | Description |
|----------|-----------|-------------|
| `iBars` | `int iBars(symbol, timeframe)` | Bar count |
| `iBarShift` | `int iBarShift(symbol, timeframe, time, exact)` | Bar index by time |
| `iClose` | `double iClose(symbol, timeframe, shift)` | Close price at shift |
| `iHigh` | `double iHigh(symbol, timeframe, shift)` | High price at shift |
| `iLow` | `double iLow(symbol, timeframe, shift)` | Low price at shift |
| `iOpen` | `double iOpen(symbol, timeframe, shift)` | Open price at shift |
| `iTime` | `datetime iTime(symbol, timeframe, shift)` | Time at shift |
| `iTickVolume` | `long iTickVolume(symbol, timeframe, shift)` | Tick volume at shift |
| `iRealVolume` | `long iRealVolume(symbol, timeframe, shift)` | Real volume at shift |
| `iVolume` | `long iVolume(symbol, timeframe, shift)` | Volume at shift |
| `iSpread` | `int iSpread(symbol, timeframe, shift)` | Spread at shift |
| `iHighest` | `int iHighest(symbol, timeframe, type, count, start)` | Highest bar index |
| `iLowest` | `int iLowest(symbol, timeframe, type, count, start)` | Lowest bar index |

---

## 12. Technical Indicator Functions

All indicator functions return a handle (int). Data is retrieved via `CopyBuffer()`.

### Trend Indicators
| Function | Parameters | Description |
|----------|-----------|-------------|
| `iMA` | `symbol, period, ma_period, ma_shift, ma_method, applied_price` | Moving Average |
| `iDEMA` | `symbol, period, ma_period, ma_shift, applied_price` | Double Exponential MA |
| `iTEMA` | `symbol, period, ma_period, ma_shift, applied_price` | Triple Exponential MA |
| `iFrAMA` | `symbol, period, ma_period, ma_shift, applied_price` | Fractal Adaptive MA |
| `iVIDyA` | `symbol, period, cmo_period, ema_period, ma_shift, applied_price` | Variable Index Dynamic Average |
| `iAMA` | `symbol, period, ama_period, fast_ma, slow_ma, ma_shift, applied_price` | Adaptive MA |
| `iBands` | `symbol, period, bands_period, bands_shift, deviation, applied_price` | Bollinger Bands (3 buffers) |
| `iEnvelopes` | `symbol, period, ma_period, ma_shift, ma_method, applied_price, deviation` | Envelopes |
| `iSAR` | `symbol, period, step, maximum` | Parabolic SAR |
| `iIchimoku` | `symbol, period, tenkan, kijun, senkou_span_b` | Ichimoku (5 buffers) |
| `iAlligator` | `symbol, period, jaw_period, jaw_shift, teeth_period, teeth_shift, lips_period, lips_shift, ma_method, applied_price` | Alligator (3 buffers) |

### Oscillators
| Function | Parameters | Description |
|----------|-----------|-------------|
| `iRSI` | `symbol, period, ma_period, applied_price` | Relative Strength Index |
| `iStochastic` | `symbol, period, Kperiod, Dperiod, slowing, ma_method, price_field` | Stochastic (2 buffers) |
| `iMACD` | `symbol, period, fast_ema, slow_ema, signal_period, applied_price` | MACD (2 buffers) |
| `iCCI` | `symbol, period, ma_period, applied_price` | Commodity Channel Index |
| `iMomentum` | `symbol, period, mom_period, applied_price` | Momentum |
| `iRVI` | `symbol, period, ma_period` | Relative Vigor Index (2 buffers) |
| `iWPR` | `symbol, period, calc_period` | Williams %R |
| `iDeMarker` | `symbol, period, ma_period` | DeMarker |
| `iTriX` | `symbol, period, ma_period, applied_price` | Triple Exponential MA Oscillator |
| `iAO` | `symbol, period` | Awesome Oscillator |

### Volume Indicators
| Function | Parameters | Description |
|----------|-----------|-------------|
| `iOBV` | `symbol, period, applied_volume` | On Balance Volume |
| `iVolumes` | `symbol, period, applied_volume` | Volumes |
| `iMFI` | `symbol, period, ma_period, applied_volume` | Money Flow Index |
| `iAD` | `symbol, period, applied_volume` | Accumulation/Distribution |
| `iChaikin` | `symbol, period, fast_ma, slow_ma, ma_method, applied_volume` | Chaikin Oscillator |
| `iForce` | `symbol, period, ma_period, ma_method, applied_volume` | Force Index |

### Volatility Indicators
| Function | Parameters | Description |
|----------|-----------|-------------|
| `iATR` | `symbol, period, ma_period` | Average True Range |
| `iStdDev` | `symbol, period, ma_period, ma_shift, ma_method, applied_price` | Standard Deviation |

### Bill Williams Indicators
| Function | Parameters | Description |
|----------|-----------|-------------|
| `iGator` | `symbol, period, jaw_period, jaw_shift, teeth_period, teeth_shift, lips_period, lips_shift, ma_method, applied_price` | Gator Oscillator |
| `iFractals` | `symbol, period` | Fractals |
| `iBWMFI` | `symbol, period, applied_volume` | Market Facilitation Index |
| `iBearsPower` | `symbol, period, ma_period` | Bears Power |
| `iBullsPower` | `symbol, period, ma_period` | Bulls Power |

### Other
| Function | Parameters | Description |
|----------|-----------|-------------|
| `iADX` | `symbol, period, adx_period` | Average Directional Index (3 buffers) |
| `iADXWilder` | `symbol, period, adx_period` | ADX by Welles Wilder |
| `iOsMA` | `symbol, period, fast_ema, slow_ema, signal_period, applied_price` | Moving Average of Oscillator |
| `iCustom` | `symbol, period, name, ...params` | Custom indicator |

### Buffer Indices for Multi-Buffer Indicators
```cpp
// iBands buffers
#define BASE_LINE   0
#define UPPER_BAND  1
#define LOWER_BAND  2

// iMACD buffers
#define MAIN_LINE   0
#define SIGNAL_LINE 1

// iStochastic buffers
#define MAIN_LINE   0
#define SIGNAL_LINE 1

// iADX buffers
#define MAIN_LINE   0
#define PLUSDI_LINE 1
#define MINUSDI_LINE 2

// iIchimoku buffers
#define TENKANSEN_LINE   0
#define KIJUNSEN_LINE    1
#define SENKOUSPANA_LINE 2
#define SENKOUSPANB_LINE 3
#define CHIKOUSPAN_LINE  4

// iAlligator buffers
#define GATORJAW_LINE   0
#define GATORTEETH_LINE 1
#define GATORLIPS_LINE  2
```

---

## 13. Trade Functions

### Order Functions
| Function | Signature | Description |
|----------|-----------|-------------|
| `OrderSend` | `bool OrderSend(MqlTradeRequest& request, MqlTradeResult& result)` | Send trade request |
| `OrderSendAsync` | `bool OrderSendAsync(MqlTradeRequest& request, MqlTradeResult& result)` | Async trade request |
| `OrderCheck` | `bool OrderCheck(MqlTradeRequest& request, MqlTradeCheckResult& result)` | Check if order valid |
| `OrderCalcMargin` | `bool OrderCalcMargin(action, symbol, volume, price, double& margin)` | Calculate margin needed |
| `OrderCalcProfit` | `bool OrderCalcProfit(action, symbol, volume, price_open, price_close, double& profit)` | Calculate profit |

### Position Functions
| Function | Signature | Description |
|----------|-----------|-------------|
| `PositionsTotal` | `int PositionsTotal()` | Count of open positions |
| `PositionGetSymbol` | `string PositionGetSymbol(int index)` | Symbol by index (selects position) |
| `PositionSelect` | `bool PositionSelect(string symbol)` | Select position by symbol |
| `PositionSelectByTicket` | `bool PositionSelectByTicket(ulong ticket)` | Select by ticket |
| `PositionGetTicket` | `ulong PositionGetTicket(int index)` | Get ticket by index |
| `PositionGetDouble` | `double PositionGetDouble(ENUM_POSITION_PROPERTY_DOUBLE prop)` | Get double property |
| `PositionGetInteger` | `long PositionGetInteger(ENUM_POSITION_PROPERTY_INTEGER prop)` | Get integer property |
| `PositionGetString` | `string PositionGetString(ENUM_POSITION_PROPERTY_STRING prop)` | Get string property |

### Position Properties
```cpp
// ENUM_POSITION_PROPERTY_INTEGER
POSITION_TICKET        // Position ticket
POSITION_TIME          // Open time
POSITION_TIME_MSC      // Open time in ms
POSITION_TIME_UPDATE   // Last update time
POSITION_TYPE          // POSITION_TYPE_BUY or POSITION_TYPE_SELL
POSITION_MAGIC         // Magic number
POSITION_IDENTIFIER    // Position ID
POSITION_REASON        // Open reason

// ENUM_POSITION_PROPERTY_DOUBLE
POSITION_VOLUME        // Volume in lots
POSITION_PRICE_OPEN    // Open price
POSITION_SL            // Stop Loss
POSITION_TP            // Take Profit
POSITION_PRICE_CURRENT // Current price
POSITION_SWAP          // Accumulated swap
POSITION_PROFIT        // Current profit

// ENUM_POSITION_PROPERTY_STRING
POSITION_SYMBOL        // Symbol name
POSITION_COMMENT       // Comment
POSITION_EXTERNAL_ID   // External ID
```

### Order Query Functions
| Function | Signature | Description |
|----------|-----------|-------------|
| `OrdersTotal` | `int OrdersTotal()` | Count of pending orders |
| `OrderGetTicket` | `ulong OrderGetTicket(int index)` | Order ticket by index |
| `OrderSelect` | `bool OrderSelect(ulong ticket)` | Select order by ticket |
| `OrderGetDouble` | `double OrderGetDouble(ENUM_ORDER_PROPERTY_DOUBLE prop)` | Get double property |
| `OrderGetInteger` | `long OrderGetInteger(ENUM_ORDER_PROPERTY_INTEGER prop)` | Get integer property |
| `OrderGetString` | `string OrderGetString(ENUM_ORDER_PROPERTY_STRING prop)` | Get string property |

### History Functions
| Function | Signature | Description |
|----------|-----------|-------------|
| `HistorySelect` | `bool HistorySelect(datetime from, datetime to)` | Select history period |
| `HistorySelectByPosition` | `bool HistorySelectByPosition(long position_id)` | Select by position |
| `HistoryOrdersTotal` | `int HistoryOrdersTotal()` | Count of history orders |
| `HistoryOrderGetTicket` | `ulong HistoryOrderGetTicket(int index)` | Order ticket by index |
| `HistoryOrderSelect` | `bool HistoryOrderSelect(ulong ticket)` | Select history order |
| `HistoryOrderGetDouble` | `double HistoryOrderGetDouble(ulong ticket, prop)` | Get order double |
| `HistoryOrderGetInteger` | `long HistoryOrderGetInteger(ulong ticket, prop)` | Get order integer |
| `HistoryOrderGetString` | `string HistoryOrderGetString(ulong ticket, prop)` | Get order string |
| `HistoryDealsTotal` | `int HistoryDealsTotal()` | Count of history deals |
| `HistoryDealGetTicket` | `ulong HistoryDealGetTicket(int index)` | Deal ticket by index |
| `HistoryDealSelect` | `bool HistoryDealSelect(ulong ticket)` | Select history deal |
| `HistoryDealGetDouble` | `double HistoryDealGetDouble(ulong ticket, prop)` | Get deal double |
| `HistoryDealGetInteger` | `long HistoryDealGetInteger(ulong ticket, prop)` | Get deal integer |
| `HistoryDealGetString` | `string HistoryDealGetString(ulong ticket, prop)` | Get deal string |

---

## 14. Common Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Alert` | `void Alert(...)` | Display message in alert window |
| `CheckPointer` | `ENUM_POINTER_TYPE CheckPointer(object*)` | Check pointer validity |
| `Comment` | `void Comment(...)` | Display on chart |
| `CryptEncode` | `int CryptEncode(method, data[], key[], result[])` | Encrypt data |
| `CryptDecode` | `int CryptDecode(method, data[], key[], result[])` | Decrypt data |
| `DebugBreak` | `void DebugBreak()` | Breakpoint |
| `ExpertRemove` | `void ExpertRemove()` | Unload EA |
| `GetPointer` | `void* GetPointer(object)` | Get object pointer |
| `GetTickCount` | `uint GetTickCount()` | Milliseconds since boot |
| `GetTickCount64` | `ulong GetTickCount64()` | Milliseconds (64-bit) |
| `GetMicrosecondCount` | `ulong GetMicrosecondCount()` | Microseconds since start |
| `MessageBox` | `int MessageBox(text, caption, flags)` | Show dialog |
| `PeriodSeconds` | `int PeriodSeconds(ENUM_TIMEFRAMES period)` | Seconds in timeframe |
| `PlaySound` | `bool PlaySound(string filename)` | Play WAV file |
| `Print` | `void Print(...)` | Print to log |
| `PrintFormat` | `void PrintFormat(string format, ...)` | Formatted print |
| `ResetLastError` | `void ResetLastError()` | Clear last error |
| `SetUserError` | `void SetUserError(ushort error)` | Set user error |
| `Sleep` | `void Sleep(int milliseconds)` | Pause execution |
| `TerminalClose` | `bool TerminalClose(int ret_code)` | Close terminal |
| `TesterStatistics` | `double TesterStatistics(ENUM_STATISTICS stat)` | Get test statistic |
| `TesterStop` | `void TesterStop()` | Stop testing |
| `TesterDeposit` | `bool TesterDeposit(double money)` | Simulate deposit |
| `TesterWithdrawal` | `bool TesterWithdrawal(double money)` | Simulate withdrawal |
| `ZeroMemory` | `void ZeroMemory(variable)` | Zero variable |

---

## 15. File Functions

### File Operations
| Function | Signature | Description |
|----------|-----------|-------------|
| `FileOpen` | `int FileOpen(filename, flags, delimiter, codepage)` | Open file |
| `FileClose` | `void FileClose(int handle)` | Close file |
| `FileDelete` | `bool FileDelete(filename, common_flag)` | Delete file |
| `FileCopy` | `bool FileCopy(src, common_src, dst, common_dst)` | Copy file |
| `FileMove` | `bool FileMove(src, common_src, dst, common_dst)` | Move/rename file |
| `FileFlush` | `void FileFlush(int handle)` | Flush buffer |
| `FileIsExist` | `bool FileIsExist(filename, common_flag)` | Check existence |
| `FileSize` | `ulong FileSize(int handle)` | File size |
| `FileTell` | `ulong FileTell(int handle)` | Current position |
| `FileSeek` | `bool FileSeek(handle, offset, origin)` | Set position |
| `FileIsEnding` | `bool FileIsEnding(int handle)` | At end of file |
| `FileIsLineEnding` | `bool FileIsLineEnding(int handle)` | At end of line |

### File Reading
| Function | Signature | Description |
|----------|-----------|-------------|
| `FileReadArray` | `uint FileReadArray(handle, array[], start, count)` | Read array |
| `FileReadBool` | `bool FileReadBool(int handle)` | Read boolean |
| `FileReadDatetime` | `datetime FileReadDatetime(int handle)` | Read datetime |
| `FileReadDouble` | `double FileReadDouble(int handle)` | Read double |
| `FileReadFloat` | `float FileReadFloat(int handle)` | Read float |
| `FileReadInteger` | `int FileReadInteger(handle, size)` | Read integer |
| `FileReadLong` | `long FileReadLong(int handle)` | Read long |
| `FileReadNumber` | `double FileReadNumber(int handle)` | Read as double |
| `FileReadString` | `string FileReadString(handle, length)` | Read string |
| `FileReadStruct` | `uint FileReadStruct(handle, struct, size)` | Read structure |

### File Writing
| Function | Signature | Description |
|----------|-----------|-------------|
| `FileWrite` | `uint FileWrite(handle, ...)` | Write to CSV/TXT |
| `FileWriteArray` | `uint FileWriteArray(handle, array[], start, count)` | Write array |
| `FileWriteDouble` | `uint FileWriteDouble(handle, value)` | Write double |
| `FileWriteFloat` | `uint FileWriteFloat(handle, value)` | Write float |
| `FileWriteInteger` | `uint FileWriteInteger(handle, value, size)` | Write integer |
| `FileWriteLong` | `uint FileWriteLong(handle, value)` | Write long |
| `FileWriteString` | `uint FileWriteString(handle, string)` | Write string |
| `FileWriteStruct` | `uint FileWriteStruct(handle, struct, size)` | Write structure |

### File Flags
```cpp
#define FILE_READ           1    // Read mode
#define FILE_WRITE          2    // Write mode
#define FILE_BIN            4    // Binary mode
#define FILE_CSV            8    // CSV mode
#define FILE_TXT           16    // Text mode
#define FILE_ANSI          32    // ANSI encoding
#define FILE_UNICODE       64    // Unicode encoding
#define FILE_SHARE_READ   128    // Shared read
#define FILE_SHARE_WRITE  256    // Shared write
#define FILE_REWRITE      512    // Rewrite existing
#define FILE_COMMON      4096    // Common folder
```

---

## 16. Checkup Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `GetLastError` | `int GetLastError()` | Get last error code |
| `IsStopped` | `bool IsStopped()` | Program stop requested |
| `UninitializeReason` | `int UninitializeReason()` | Deinit reason code |
| `TerminalInfoInteger` | `long TerminalInfoInteger(prop)` | Terminal integer property |
| `TerminalInfoDouble` | `double TerminalInfoDouble(prop)` | Terminal double property |
| `TerminalInfoString` | `string TerminalInfoString(prop)` | Terminal string property |
| `MQLInfoInteger` | `long MQLInfoInteger(prop)` | MQL program integer property |
| `MQLInfoString` | `string MQLInfoString(prop)` | MQL program string property |
| `Symbol` | `string Symbol()` | Current chart symbol |
| `Period` | `ENUM_TIMEFRAMES Period()` | Current chart timeframe |
| `Digits` | `int Digits()` | Symbol decimal places |
| `Point` | `double Point()` | Symbol point size |

---

## Appendix: C++ Implementation Notes

### Priority for Backtesting

**High Priority (Must Implement):**
1. Core structures: MqlTick, MqlRates, MqlDateTime
2. Math functions (use `<cmath>`)
3. Array operations (use `<vector>` and `<algorithm>`)
4. Time functions
5. Symbol/Account info (as configuration)
6. Position management
7. Order calculation: OrderCalcMargin, OrderCalcProfit
8. Technical indicators (core set)

**Medium Priority:**
1. String functions (use `<string>`)
2. File I/O (use `<fstream>`)
3. Additional indicators

**Low Priority (Skip for backtesting):**
1. GUI functions (Alert, MessageBox, Comment)
2. Network functions
3. Chart operations
4. Object functions
5. Resource functions

### Example C++ Wrapper Class
```cpp
class MQL5Compat {
public:
    // Math
    static double MathAbs(double x) { return std::abs(x); }
    static double MathMax(double a, double b) { return std::max(a, b); }
    static double MathMin(double a, double b) { return std::min(a, b); }
    static double MathPow(double base, double exp) { return std::pow(base, exp); }
    static double MathSqrt(double x) { return std::sqrt(x); }
    static double MathRound(double x) { return std::round(x); }
    static double MathFloor(double x) { return std::floor(x); }
    static double MathCeil(double x) { return std::ceil(x); }

    // Normalize
    static double NormalizeDouble(double value, int digits) {
        double mult = std::pow(10.0, digits);
        return std::round(value * mult) / mult;
    }

    // Time
    static datetime TimeCurrent() { return std::time(nullptr); }
    static bool TimeToStruct(datetime time, MqlDateTime& dt);
    static datetime StructToTime(const MqlDateTime& dt);

    // String
    static int StringLen(const std::string& s) { return s.length(); }
    static std::string StringSubstr(const std::string& s, int start, int len = -1);
    static int StringFind(const std::string& s, const std::string& sub, int start = 0);
};
```

---

---

## 17. Extended Features (mql5_extended.h)

The `include/mql5_extended.h` header provides additional MQL5 features beyond the core compatibility layer.

### Terminal Functions
```cpp
// Get terminal information
long build = TerminalInfoInteger(TERMINAL_BUILD);
bool connected = TerminalInfoInteger(TERMINAL_CONNECTED);
int cores = TerminalInfoInteger(TERMINAL_CPU_CORES);
std::string name = TerminalInfoString(TERMINAL_NAME);
std::string path = TerminalInfoString(TERMINAL_DATA_PATH);
```

### Global Variables
```cpp
// Set and get global variables (persist across ticks)
GlobalVariableSet("LastPrice", 2750.50);
double price = GlobalVariableGet("LastPrice");
bool exists = GlobalVariableCheck("LastPrice");
datetime when = GlobalVariableTime("LastPrice");
GlobalVariableDel("LastPrice");

// Atomic set-on-condition (for synchronization)
GlobalVariableSetOnCondition("Lock", 1.0, 0.0);  // Set to 1 only if currently 0

// Enumerate all globals
int total = GlobalVariablesTotal();
for (int i = 0; i < total; i++) {
    std::string name = GlobalVariableName(i);
}
```

### Event System
```cpp
// Timer events
EventSetTimer(5);        // Fire OnTimer every 5 seconds
EventSetMillisecondTimer(100);  // Fire every 100ms
EventKillTimer();

// Custom chart events
EventChartCustom(0, CHARTEVENT_CUSTOM, 123, 45.67, "data");

// Set event callbacks
EventManager::Instance().SetOnTimer([]() { /* handle timer */ });
EventManager::Instance().SetOnTrade([]() { /* handle trade */ });
EventManager::Instance().SetOnTick([]() { /* handle tick */ });
```

### File Operations
```cpp
// Open file
int handle = FileOpen("data.csv", FILE_READ | FILE_CSV, ',');

// Read data
std::string line = FileReadString(handle);
double value = FileReadDouble(handle);
int num = FileReadInteger(handle);

// Write data
FileWriteString(handle, "Hello");
FileWriteDouble(handle, 123.456, 2);

// File management
bool exists = FileIsExist("data.csv");
FileDelete("old.csv");
FileCopy("src.csv", 0, "dst.csv", 0);
FileMove("old.csv", 0, "new.csv", 0);
ulong size = FileSize(handle);
FileSeek(handle, 0, SEEK_SET);

FileClose(handle);
```

### History Access
```cpp
// Select history period
HistorySelect(from_time, to_time);
HistorySelectByPosition(position_id);

// Access deals
int deals = HistoryDealsTotal();
for (int i = 0; i < deals; i++) {
    ulong ticket = HistoryDealGetTicket(i);
    double profit = HistoryDealGetDouble(ticket, DEAL_PROFIT);
    double volume = HistoryDealGetDouble(ticket, DEAL_VOLUME);
    std::string symbol = HistoryDealGetString(ticket, DEAL_SYMBOL);
}

// Access orders
int orders = HistoryOrdersTotal();
for (int i = 0; i < orders; i++) {
    ulong ticket = HistoryOrderGetTicket(i);
    double price = HistoryOrderGetDouble(ticket, ORDER_PRICE_OPEN);
}
```

### Chart Functions (Stubs for Backtest)
```cpp
// These are stubs - no actual chart in backtest mode
long chart = ChartOpen("XAUUSD", PERIOD_H1);
ChartSetInteger(chart, CHART_SHOW_GRID, false);
ChartSetString(chart, CHART_COMMENT, "Test");
ChartRedraw(chart);
ChartClose(chart);
```

### Object Functions (Stubs for Backtest)
```cpp
// These are stubs - no actual objects in backtest mode
ObjectCreate(0, "line1", OBJ_HLINE, 0, 0, 2750.0);
ObjectSetDouble(0, "line1", OBJPROP_PRICE, 2751.0);
ObjectDelete(0, "line1");
```

### Custom Indicators
```cpp
// Create indicator handle
int ma_handle = iMA("XAUUSD", PERIOD_H1, 14, 0, MODE_SMA, PRICE_CLOSE);
int rsi_handle = iRSI("XAUUSD", PERIOD_H1, 14, PRICE_CLOSE);
int bands_handle = iBands("XAUUSD", PERIOD_H1, 20, 0, 2.0, PRICE_CLOSE);

// Copy buffer data
std::vector<double> buffer;
CopyBuffer(ma_handle, 0, 0, 100, buffer);

// Release when done
IndicatorRelease(ma_handle);
```

### Series Access (Legacy Functions)
```cpp
int bars = iBars("XAUUSD", PERIOD_H1);
int shift = iBarShift("XAUUSD", PERIOD_H1, time);
datetime t = iTime("XAUUSD", PERIOD_H1, 0);
double o = iOpen("XAUUSD", PERIOD_H1, 0);
double h = iHigh("XAUUSD", PERIOD_H1, 0);
double l = iLow("XAUUSD", PERIOD_H1, 0);
double c = iClose("XAUUSD", PERIOD_H1, 0);
```

### Notifications (Logging in Backtest)
```cpp
SendNotification("Alert message");  // Logs to console
SendMail("Subject", "Body");        // Logs to console
PlaySound("alert.wav");             // Logs to console
MessageBox("Message", "Title");     // Logs to console, returns IDOK
```

---

## Implementation Files

| File | Description |
|------|-------------|
| `include/mql5_compat.h` | Core MQL5 types, math, string, array, datetime functions, 11 technical indicators |
| `include/mql5_extended.h` | Terminal, file, global variables, events, history, chart/object stubs, custom indicators |
| `include/mql5_trade_context.h` | CTrade, CPositionInfo, CSymbolInfo, CAccountInfo classes |
| `include/mt5_statistics.h` | 60+ statistics and optimization criteria |

### Usage Example
```cpp
#include "mql5_compat.h"
#include "mql5_extended.h"
#include "mql5_trade_context.h"
using namespace mql5;

// Now use MQL5-style code
datetime now = TimeCurrent();
double price = NormalizeDouble(2750.123, 2);

// Global variables
GlobalVariableSet("LastTrade", now);

// File operations
int f = FileOpen("log.txt", FILE_WRITE | FILE_TXT);
FileWriteString(f, "Started at " + TimeToString(now));
FileClose(f);

// Timer
EventSetTimer(60);  // Every minute

// Trading
CTrade trade;
trade.SetExpertMagicNumber(12345);
trade.Buy(0.1, "XAUUSD", 0, 2740.0, 2760.0, "Grid buy");
```

---

## Sources

- [MQL5 Function Reference](https://www.mql5.com/en/docs/function_indices)
- [MQL5 Technical Indicators](https://www.mql5.com/en/docs/indicators)
- [MQL5 Trade Functions](https://www.mql5.com/en/docs/trading)
- [MQL5 Data Structures](https://www.mql5.com/en/docs/constants/structures)
- [MQL5 Symbol Properties](https://www.mql5.com/en/docs/constants/environment_state/marketinfoconstants)
- [MQL5 Account Properties](https://www.mql5.com/en/docs/constants/environment_state/accountinformation)
