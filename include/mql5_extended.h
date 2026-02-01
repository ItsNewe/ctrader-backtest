/**
 * MQL5 Extended Features for C++
 *
 * This header provides additional MQL5-compatible features:
 * - Terminal functions
 * - History access
 * - File operations
 * - Global variables
 * - Chart functions (stub for backtest)
 * - Event system
 * - Object functions (stub for backtest)
 * - Custom indicators
 * - Network functions (stub)
 * - Resource functions (stub)
 *
 * Usage:
 *   #include "mql5_extended.h"
 *   using namespace mql5;
 */

#ifndef MQL5_EXTENDED_H
#define MQL5_EXTENDED_H

#include "mql5_compat.h"
#include <fstream>
#include <map>
#include <set>
#include <queue>
#include <mutex>
#include <functional>
#include <filesystem>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace mql5 {

//=============================================================================
// Terminal Info Enumerations
//=============================================================================

enum ENUM_TERMINAL_INFO_INTEGER {
    TERMINAL_BUILD,
    TERMINAL_COMMUNITY_ACCOUNT,
    TERMINAL_COMMUNITY_CONNECTION,
    TERMINAL_CONNECTED,
    TERMINAL_DLLS_ALLOWED,
    TERMINAL_TRADE_ALLOWED,
    TERMINAL_EMAIL_ENABLED,
    TERMINAL_FTP_ENABLED,
    TERMINAL_NOTIFICATIONS_ENABLED,
    TERMINAL_MAXBARS,
    TERMINAL_MQID,
    TERMINAL_CODEPAGE,
    TERMINAL_CPU_CORES,
    TERMINAL_DISK_SPACE,
    TERMINAL_MEMORY_PHYSICAL,
    TERMINAL_MEMORY_TOTAL,
    TERMINAL_MEMORY_AVAILABLE,
    TERMINAL_MEMORY_USED,
    TERMINAL_SCREEN_DPI,
    TERMINAL_PING_LAST,
    TERMINAL_KEYSTATE_LEFT,
    TERMINAL_KEYSTATE_RIGHT,
    TERMINAL_KEYSTATE_TAB,
    TERMINAL_KEYSTATE_ESCAPE,
    TERMINAL_KEYSTATE_ENTER,
    TERMINAL_KEYSTATE_DELETE,
    TERMINAL_KEYSTATE_SCRLOCK,
    TERMINAL_KEYSTATE_CAPSLOCK,
    TERMINAL_KEYSTATE_NUMLOCK,
    TERMINAL_KEYSTATE_INSERT,
    TERMINAL_KEYSTATE_HOME,
    TERMINAL_KEYSTATE_END,
    TERMINAL_KEYSTATE_PAGEUP,
    TERMINAL_KEYSTATE_PAGEDOWN,
    TERMINAL_KEYSTATE_CONTROL,
    TERMINAL_KEYSTATE_SHIFT,
    TERMINAL_KEYSTATE_MENU,
    TERMINAL_VPS
};

enum ENUM_TERMINAL_INFO_STRING {
    TERMINAL_LANGUAGE,
    TERMINAL_COMPANY,
    TERMINAL_NAME,
    TERMINAL_PATH,
    TERMINAL_DATA_PATH,
    TERMINAL_COMMONDATA_PATH
};

enum ENUM_TERMINAL_INFO_DOUBLE {
    TERMINAL_COMMUNITY_BALANCE,
    TERMINAL_RETRANSMISSION
};

//=============================================================================
// File Enumerations
//=============================================================================

enum ENUM_FILE_FLAGS {
    FILE_READ = 1,
    FILE_WRITE = 2,
    FILE_BIN = 4,
    FILE_CSV = 8,
    FILE_TXT = 16,
    FILE_ANSI = 32,
    FILE_UNICODE = 64,
    FILE_SHARE_READ = 128,
    FILE_SHARE_WRITE = 256,
    FILE_REWRITE = 512,
    FILE_COMMON = 1024
};

enum ENUM_FILE_POSITION {
    SEEK_SET = 0,
    SEEK_CUR = 1,
    SEEK_END = 2
};

//=============================================================================
// Chart Enumerations (stubs for backtest compatibility)
//=============================================================================

enum ENUM_CHART_PROPERTY_INTEGER {
    CHART_SHOW,
    CHART_IS_OBJECT,
    CHART_BRING_TO_TOP,
    CHART_CONTEXT_MENU,
    CHART_CROSSHAIR_TOOL,
    CHART_MOUSE_SCROLL,
    CHART_EVENT_MOUSE_WHEEL,
    CHART_EVENT_MOUSE_MOVE,
    CHART_EVENT_OBJECT_CREATE,
    CHART_EVENT_OBJECT_DELETE,
    CHART_MODE,
    CHART_FOREGROUND,
    CHART_SHIFT,
    CHART_AUTOSCROLL,
    CHART_KEYBOARD_CONTROL,
    CHART_QUICK_NAVIGATION,
    CHART_SCALE,
    CHART_SCALEFIX,
    CHART_SCALEFIX_11,
    CHART_SCALE_PT_PER_BAR,
    CHART_SHOW_OHLC,
    CHART_SHOW_BID_LINE,
    CHART_SHOW_ASK_LINE,
    CHART_SHOW_LAST_LINE,
    CHART_SHOW_PERIOD_SEP,
    CHART_SHOW_GRID,
    CHART_SHOW_VOLUMES,
    CHART_SHOW_OBJECT_DESCR,
    CHART_VISIBLE_BARS,
    CHART_WINDOWS_TOTAL,
    CHART_WINDOW_IS_VISIBLE,
    CHART_WINDOW_HANDLE,
    CHART_WINDOW_YDISTANCE,
    CHART_FIRST_VISIBLE_BAR,
    CHART_WIDTH_IN_BARS,
    CHART_WIDTH_IN_PIXELS,
    CHART_HEIGHT_IN_PIXELS,
    CHART_COLOR_BACKGROUND,
    CHART_COLOR_FOREGROUND,
    CHART_COLOR_GRID,
    CHART_COLOR_VOLUME,
    CHART_COLOR_CHART_UP,
    CHART_COLOR_CHART_DOWN,
    CHART_COLOR_CHART_LINE,
    CHART_COLOR_CANDLE_BULL,
    CHART_COLOR_CANDLE_BEAR,
    CHART_COLOR_BID,
    CHART_COLOR_ASK,
    CHART_COLOR_LAST,
    CHART_COLOR_STOP_LEVEL
};

enum ENUM_CHART_PROPERTY_DOUBLE {
    CHART_SHIFT_SIZE,
    CHART_FIXED_POSITION,
    CHART_FIXED_MAX,
    CHART_FIXED_MIN,
    CHART_POINTS_PER_BAR,
    CHART_PRICE_MIN,
    CHART_PRICE_MAX
};

enum ENUM_CHART_PROPERTY_STRING {
    CHART_COMMENT,
    CHART_EXPERT_NAME,
    CHART_SCRIPT_NAME
};

//=============================================================================
// Object Enumerations
//=============================================================================

enum ENUM_OBJECT {
    OBJ_VLINE,
    OBJ_HLINE,
    OBJ_TREND,
    OBJ_TRENDBYANGLE,
    OBJ_CYCLES,
    OBJ_ARROWED_LINE,
    OBJ_CHANNEL,
    OBJ_STDDEVCHANNEL,
    OBJ_REGRESSION,
    OBJ_PITCHFORK,
    OBJ_GANNLINE,
    OBJ_GANNFAN,
    OBJ_GANNGRID,
    OBJ_FIBO,
    OBJ_FIBOTIMES,
    OBJ_FIBOFAN,
    OBJ_FIBOARC,
    OBJ_FIBOCHANNEL,
    OBJ_EXPANSION,
    OBJ_ELLIOTWAVE5,
    OBJ_ELLIOTWAVE3,
    OBJ_RECTANGLE,
    OBJ_TRIANGLE,
    OBJ_ELLIPSE,
    OBJ_ARROW_THUMB_UP,
    OBJ_ARROW_THUMB_DOWN,
    OBJ_ARROW_UP,
    OBJ_ARROW_DOWN,
    OBJ_ARROW_STOP,
    OBJ_ARROW_CHECK,
    OBJ_ARROW_LEFT_PRICE,
    OBJ_ARROW_RIGHT_PRICE,
    OBJ_ARROW_BUY,
    OBJ_ARROW_SELL,
    OBJ_ARROW,
    OBJ_TEXT,
    OBJ_LABEL,
    OBJ_BUTTON,
    OBJ_CHART,
    OBJ_BITMAP,
    OBJ_BITMAP_LABEL,
    OBJ_EDIT,
    OBJ_EVENT,
    OBJ_RECTANGLE_LABEL
};

enum ENUM_OBJECT_PROPERTY_INTEGER {
    OBJPROP_COLOR,
    OBJPROP_STYLE,
    OBJPROP_WIDTH,
    OBJPROP_BACK,
    OBJPROP_ZORDER,
    OBJPROP_FILL,
    OBJPROP_HIDDEN,
    OBJPROP_SELECTED,
    OBJPROP_READONLY,
    OBJPROP_TYPE,
    OBJPROP_TIME,
    OBJPROP_SELECTABLE,
    OBJPROP_CREATETIME,
    OBJPROP_LEVELS,
    OBJPROP_LEVELCOLOR,
    OBJPROP_LEVELSTYLE,
    OBJPROP_LEVELWIDTH,
    OBJPROP_FONTSIZE,
    OBJPROP_RAY_LEFT,
    OBJPROP_RAY_RIGHT,
    OBJPROP_RAY,
    OBJPROP_ELLIPSE,
    OBJPROP_ARROWCODE,
    OBJPROP_TIMEFRAMES,
    OBJPROP_ANCHOR,
    OBJPROP_XDISTANCE,
    OBJPROP_YDISTANCE,
    OBJPROP_XSIZE,
    OBJPROP_YSIZE,
    OBJPROP_XOFFSET,
    OBJPROP_YOFFSET,
    OBJPROP_DIRECTION,
    OBJPROP_DEGREE,
    OBJPROP_DRAWLINES,
    OBJPROP_STATE,
    OBJPROP_CHART_ID,
    OBJPROP_XSCALE,
    OBJPROP_YSCALE,
    OBJPROP_BGCOLOR,
    OBJPROP_CORNER,
    OBJPROP_BORDER_TYPE,
    OBJPROP_BORDER_COLOR
};

enum ENUM_OBJECT_PROPERTY_DOUBLE {
    OBJPROP_PRICE,
    OBJPROP_LEVELVALUE,
    OBJPROP_SCALE,
    OBJPROP_ANGLE,
    OBJPROP_DEVIATION
};

enum ENUM_OBJECT_PROPERTY_STRING {
    OBJPROP_NAME,
    OBJPROP_TEXT,
    OBJPROP_TOOLTIP,
    OBJPROP_LEVELTEXT,
    OBJPROP_FONT,
    OBJPROP_BMPFILE,
    OBJPROP_SYMBOL
};

//=============================================================================
// Event Types
//=============================================================================

enum ENUM_CHART_EVENT {
    CHARTEVENT_KEYDOWN,
    CHARTEVENT_MOUSE_MOVE,
    CHARTEVENT_MOUSE_WHEEL,
    CHARTEVENT_OBJECT_CREATE,
    CHARTEVENT_OBJECT_CHANGE,
    CHARTEVENT_OBJECT_DELETE,
    CHARTEVENT_CLICK,
    CHARTEVENT_OBJECT_CLICK,
    CHARTEVENT_OBJECT_DRAG,
    CHARTEVENT_OBJECT_ENDEDIT,
    CHARTEVENT_CHART_CHANGE,
    CHARTEVENT_CUSTOM,
    CHARTEVENT_CUSTOM_LAST
};

//=============================================================================
// History Enumerations
//=============================================================================

enum ENUM_DEAL_PROPERTY_INTEGER {
    DEAL_TICKET,
    DEAL_ORDER,
    DEAL_TIME,
    DEAL_TIME_MSC,
    DEAL_TYPE,
    DEAL_ENTRY,
    DEAL_MAGIC,
    DEAL_REASON,
    DEAL_POSITION_ID
};

enum ENUM_DEAL_PROPERTY_DOUBLE {
    DEAL_VOLUME,
    DEAL_PRICE,
    DEAL_COMMISSION,
    DEAL_SWAP,
    DEAL_PROFIT,
    DEAL_FEE,
    DEAL_SL,
    DEAL_TP
};

enum ENUM_DEAL_PROPERTY_STRING {
    DEAL_SYMBOL,
    DEAL_COMMENT,
    DEAL_EXTERNAL_ID
};

enum ENUM_ORDER_PROPERTY_INTEGER {
    ORDER_TICKET,
    ORDER_TIME_SETUP,
    ORDER_TIME_SETUP_MSC,
    ORDER_TIME_DONE,
    ORDER_TIME_DONE_MSC,
    ORDER_TIME_EXPIRATION,
    ORDER_TYPE,
    ORDER_TYPE_TIME,
    ORDER_TYPE_FILLING,
    ORDER_STATE,
    ORDER_MAGIC,
    ORDER_POSITION_ID,
    ORDER_POSITION_BY_ID,
    ORDER_REASON
};

enum ENUM_ORDER_PROPERTY_DOUBLE {
    ORDER_VOLUME_INITIAL,
    ORDER_VOLUME_CURRENT,
    ORDER_PRICE_OPEN,
    ORDER_SL,
    ORDER_TP,
    ORDER_PRICE_CURRENT,
    ORDER_PRICE_STOPLIMIT
};

enum ENUM_ORDER_PROPERTY_STRING {
    ORDER_SYMBOL,
    ORDER_COMMENT,
    ORDER_EXTERNAL_ID
};

//=============================================================================
// Terminal Functions
//=============================================================================

class TerminalInfo {
public:
    static TerminalInfo& Instance() {
        static TerminalInfo instance;
        return instance;
    }

    long GetInteger(ENUM_TERMINAL_INFO_INTEGER prop) const {
        switch (prop) {
            case TERMINAL_BUILD: return 3500;  // Simulated build number
            case TERMINAL_COMMUNITY_ACCOUNT: return 0;
            case TERMINAL_COMMUNITY_CONNECTION: return 0;
            case TERMINAL_CONNECTED: return m_connected ? 1 : 0;
            case TERMINAL_DLLS_ALLOWED: return 1;
            case TERMINAL_TRADE_ALLOWED: return m_tradeAllowed ? 1 : 0;
            case TERMINAL_EMAIL_ENABLED: return 0;
            case TERMINAL_FTP_ENABLED: return 0;
            case TERMINAL_NOTIFICATIONS_ENABLED: return 0;
            case TERMINAL_MAXBARS: return m_maxBars;
            case TERMINAL_CPU_CORES: return std::thread::hardware_concurrency();
            case TERMINAL_MEMORY_PHYSICAL: return 16384;  // 16GB simulated
            case TERMINAL_MEMORY_TOTAL: return 16384;
            case TERMINAL_MEMORY_AVAILABLE: return 8192;
            case TERMINAL_MEMORY_USED: return 8192;
            case TERMINAL_VPS: return 0;
            default: return 0;
        }
    }

    double GetDouble(ENUM_TERMINAL_INFO_DOUBLE prop) const {
        switch (prop) {
            case TERMINAL_COMMUNITY_BALANCE: return 0.0;
            case TERMINAL_RETRANSMISSION: return 0.0;
            default: return 0.0;
        }
    }

    std::string GetString(ENUM_TERMINAL_INFO_STRING prop) const {
        switch (prop) {
            case TERMINAL_LANGUAGE: return "English";
            case TERMINAL_COMPANY: return "Backtest Engine";
            case TERMINAL_NAME: return "CTrader Backtest";
            case TERMINAL_PATH: return m_terminalPath;
            case TERMINAL_DATA_PATH: return m_dataPath;
            case TERMINAL_COMMONDATA_PATH: return m_commonDataPath;
            default: return "";
        }
    }

    void SetConnected(bool connected) { m_connected = connected; }
    void SetTradeAllowed(bool allowed) { m_tradeAllowed = allowed; }
    void SetMaxBars(int maxBars) { m_maxBars = maxBars; }
    void SetPaths(const std::string& terminal, const std::string& data, const std::string& common) {
        m_terminalPath = terminal;
        m_dataPath = data;
        m_commonDataPath = common;
    }

private:
    TerminalInfo() : m_connected(true), m_tradeAllowed(true), m_maxBars(100000),
                     m_terminalPath("."), m_dataPath("."), m_commonDataPath(".") {}

    bool m_connected;
    bool m_tradeAllowed;
    int m_maxBars;
    std::string m_terminalPath;
    std::string m_dataPath;
    std::string m_commonDataPath;
};

inline long TerminalInfoInteger(ENUM_TERMINAL_INFO_INTEGER prop) {
    return TerminalInfo::Instance().GetInteger(prop);
}

inline double TerminalInfoDouble(ENUM_TERMINAL_INFO_DOUBLE prop) {
    return TerminalInfo::Instance().GetDouble(prop);
}

inline std::string TerminalInfoString(ENUM_TERMINAL_INFO_STRING prop) {
    return TerminalInfo::Instance().GetString(prop);
}

//=============================================================================
// File Operations
//=============================================================================

class FileManager {
public:
    static FileManager& Instance() {
        static FileManager instance;
        return instance;
    }

    int FileOpen(const std::string& filename, int flags, char delimiter = '\t') {
        std::ios_base::openmode mode = std::ios_base::in;

        if (flags & FILE_WRITE) {
            mode = std::ios_base::out;
            if (flags & FILE_REWRITE) {
                mode |= std::ios_base::trunc;
            } else {
                mode |= std::ios_base::app;
            }
        }

        if (flags & FILE_BIN) {
            mode |= std::ios_base::binary;
        }

        std::string fullPath = ResolvePath(filename, flags);

        auto file = std::make_shared<std::fstream>(fullPath, mode);
        if (!file->is_open()) {
            return INVALID_HANDLE;
        }

        int handle = m_nextHandle++;
        m_files[handle] = {file, fullPath, flags, delimiter};
        return handle;
    }

    void FileClose(int handle) {
        auto it = m_files.find(handle);
        if (it != m_files.end()) {
            it->second.stream->close();
            m_files.erase(it);
        }
    }

    bool FileIsExist(const std::string& filename, int common = 0) {
        int flags = common ? FILE_COMMON : 0;
        std::string fullPath = ResolvePath(filename, flags);
        return std::filesystem::exists(fullPath);
    }

    bool FileDelete(const std::string& filename, int common = 0) {
        int flags = common ? FILE_COMMON : 0;
        std::string fullPath = ResolvePath(filename, flags);
        return std::filesystem::remove(fullPath);
    }

    bool FileCopy(const std::string& src, int srcCommon, const std::string& dst, int dstCommon) {
        std::string srcPath = ResolvePath(src, srcCommon ? FILE_COMMON : 0);
        std::string dstPath = ResolvePath(dst, dstCommon ? FILE_COMMON : 0);
        try {
            std::filesystem::copy(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing);
            return true;
        } catch (...) {
            return false;
        }
    }

    bool FileMove(const std::string& src, int srcCommon, const std::string& dst, int dstCommon) {
        std::string srcPath = ResolvePath(src, srcCommon ? FILE_COMMON : 0);
        std::string dstPath = ResolvePath(dst, dstCommon ? FILE_COMMON : 0);
        try {
            std::filesystem::rename(srcPath, dstPath);
            return true;
        } catch (...) {
            return false;
        }
    }

    uint FileSize(int handle) {
        auto it = m_files.find(handle);
        if (it == m_files.end()) return 0;

        auto& file = it->second.stream;
        auto current = file->tellg();
        file->seekg(0, std::ios::end);
        auto size = file->tellg();
        file->seekg(current);
        return static_cast<uint>(size);
    }

    bool FileSeek(int handle, long offset, ENUM_FILE_POSITION origin) {
        auto it = m_files.find(handle);
        if (it == m_files.end()) return false;

        std::ios_base::seekdir dir;
        switch (origin) {
            case SEEK_SET: dir = std::ios::beg; break;
            case SEEK_CUR: dir = std::ios::cur; break;
            case SEEK_END: dir = std::ios::end; break;
            default: return false;
        }

        it->second.stream->seekg(offset, dir);
        it->second.stream->seekp(offset, dir);
        return !it->second.stream->fail();
    }

    ulong FileTell(int handle) {
        auto it = m_files.find(handle);
        if (it == m_files.end()) return 0;
        return static_cast<ulong>(it->second.stream->tellg());
    }

    bool FileIsEnding(int handle) {
        auto it = m_files.find(handle);
        if (it == m_files.end()) return true;
        return it->second.stream->eof();
    }

    bool FileIsLineEnding(int handle) {
        auto it = m_files.find(handle);
        if (it == m_files.end()) return true;
        return it->second.stream->peek() == '\n' || it->second.stream->peek() == '\r';
    }

    void FileFlush(int handle) {
        auto it = m_files.find(handle);
        if (it != m_files.end()) {
            it->second.stream->flush();
        }
    }

    // Read functions
    std::string FileReadString(int handle, int length = 0) {
        auto it = m_files.find(handle);
        if (it == m_files.end()) return "";

        if (length <= 0) {
            // Read until delimiter or newline
            std::string result;
            std::getline(*it->second.stream, result, it->second.delimiter);
            return result;
        } else {
            std::string result(length, '\0');
            it->second.stream->read(&result[0], length);
            result.resize(it->second.stream->gcount());
            return result;
        }
    }

    double FileReadDouble(int handle) {
        auto it = m_files.find(handle);
        if (it == m_files.end()) return 0.0;

        if (it->second.flags & FILE_BIN) {
            double value;
            it->second.stream->read(reinterpret_cast<char*>(&value), sizeof(double));
            return value;
        } else {
            double value;
            *it->second.stream >> value;
            return value;
        }
    }

    long FileReadLong(int handle) {
        auto it = m_files.find(handle);
        if (it == m_files.end()) return 0;

        if (it->second.flags & FILE_BIN) {
            long value;
            it->second.stream->read(reinterpret_cast<char*>(&value), sizeof(long));
            return value;
        } else {
            long value;
            *it->second.stream >> value;
            return value;
        }
    }

    int FileReadInteger(int handle) {
        return static_cast<int>(FileReadLong(handle));
    }

    uint FileReadArray(int handle, std::vector<double>& arr, int start = 0, int count = -1) {
        auto it = m_files.find(handle);
        if (it == m_files.end()) return 0;

        uint read = 0;
        if (count < 0) count = INT_MAX;

        if (start >= static_cast<int>(arr.size())) {
            arr.resize(start + 1);
        }

        while (!it->second.stream->eof() && read < static_cast<uint>(count)) {
            if (start + read >= arr.size()) {
                arr.resize(start + read + 1);
            }
            double value;
            it->second.stream->read(reinterpret_cast<char*>(&value), sizeof(double));
            if (it->second.stream->gcount() == sizeof(double)) {
                arr[start + read] = value;
                read++;
            }
        }
        return read;
    }

    // Write functions
    uint FileWriteString(int handle, const std::string& text, int length = -1) {
        auto it = m_files.find(handle);
        if (it == m_files.end()) return 0;

        if (length < 0) length = static_cast<int>(text.length());
        it->second.stream->write(text.c_str(), length);
        return static_cast<uint>(length);
    }

    uint FileWriteDouble(int handle, double value, int digits = -1) {
        auto it = m_files.find(handle);
        if (it == m_files.end()) return 0;

        if (it->second.flags & FILE_BIN) {
            it->second.stream->write(reinterpret_cast<const char*>(&value), sizeof(double));
            return sizeof(double);
        } else {
            std::ostringstream oss;
            if (digits >= 0) oss << std::fixed << std::setprecision(digits);
            oss << value;
            std::string str = oss.str();
            it->second.stream->write(str.c_str(), str.length());
            return static_cast<uint>(str.length());
        }
    }

    uint FileWriteLong(int handle, long value) {
        auto it = m_files.find(handle);
        if (it == m_files.end()) return 0;

        if (it->second.flags & FILE_BIN) {
            it->second.stream->write(reinterpret_cast<const char*>(&value), sizeof(long));
            return sizeof(long);
        } else {
            std::string str = std::to_string(value);
            it->second.stream->write(str.c_str(), str.length());
            return static_cast<uint>(str.length());
        }
    }

    uint FileWriteInteger(int handle, int value) {
        return FileWriteLong(handle, value);
    }

    uint FileWriteArray(int handle, const std::vector<double>& arr, int start = 0, int count = -1) {
        auto it = m_files.find(handle);
        if (it == m_files.end()) return 0;

        if (count < 0) count = static_cast<int>(arr.size()) - start;
        for (int i = 0; i < count && start + i < static_cast<int>(arr.size()); ++i) {
            it->second.stream->write(reinterpret_cast<const char*>(&arr[start + i]), sizeof(double));
        }
        return static_cast<uint>(count * sizeof(double));
    }

    void SetBasePath(const std::string& path) { m_basePath = path; }
    void SetCommonPath(const std::string& path) { m_commonPath = path; }

private:
    FileManager() : m_nextHandle(1), m_basePath("."), m_commonPath(".") {}

    std::string ResolvePath(const std::string& filename, int flags) {
        if (flags & FILE_COMMON) {
            return m_commonPath + "/" + filename;
        }
        return m_basePath + "/" + filename;
    }

    struct FileInfo {
        std::shared_ptr<std::fstream> stream;
        std::string path;
        int flags;
        char delimiter;
    };

    int m_nextHandle;
    std::string m_basePath;
    std::string m_commonPath;
    std::map<int, FileInfo> m_files;
};

// File function wrappers
inline int FileOpen(const std::string& filename, int flags, char delimiter = '\t') {
    return FileManager::Instance().FileOpen(filename, flags, delimiter);
}

inline void FileClose(int handle) {
    FileManager::Instance().FileClose(handle);
}

inline bool FileIsExist(const std::string& filename, int common = 0) {
    return FileManager::Instance().FileIsExist(filename, common);
}

inline bool FileDelete(const std::string& filename, int common = 0) {
    return FileManager::Instance().FileDelete(filename, common);
}

inline bool FileCopy(const std::string& src, int srcCommon, const std::string& dst, int dstCommon) {
    return FileManager::Instance().FileCopy(src, srcCommon, dst, dstCommon);
}

inline bool FileMove(const std::string& src, int srcCommon, const std::string& dst, int dstCommon) {
    return FileManager::Instance().FileMove(src, srcCommon, dst, dstCommon);
}

inline uint FileSize(int handle) {
    return FileManager::Instance().FileSize(handle);
}

inline bool FileSeek(int handle, long offset, ENUM_FILE_POSITION origin) {
    return FileManager::Instance().FileSeek(handle, offset, origin);
}

inline ulong FileTell(int handle) {
    return FileManager::Instance().FileTell(handle);
}

inline bool FileIsEnding(int handle) {
    return FileManager::Instance().FileIsEnding(handle);
}

inline bool FileIsLineEnding(int handle) {
    return FileManager::Instance().FileIsLineEnding(handle);
}

inline void FileFlush(int handle) {
    FileManager::Instance().FileFlush(handle);
}

inline std::string FileReadString(int handle, int length = 0) {
    return FileManager::Instance().FileReadString(handle, length);
}

inline double FileReadDouble(int handle) {
    return FileManager::Instance().FileReadDouble(handle);
}

inline long FileReadLong(int handle) {
    return FileManager::Instance().FileReadLong(handle);
}

inline int FileReadInteger(int handle) {
    return FileManager::Instance().FileReadInteger(handle);
}

inline uint FileWriteString(int handle, const std::string& text, int length = -1) {
    return FileManager::Instance().FileWriteString(handle, text, length);
}

inline uint FileWriteDouble(int handle, double value, int digits = -1) {
    return FileManager::Instance().FileWriteDouble(handle, value, digits);
}

inline uint FileWriteLong(int handle, long value) {
    return FileManager::Instance().FileWriteLong(handle, value);
}

inline uint FileWriteInteger(int handle, int value) {
    return FileManager::Instance().FileWriteInteger(handle, value);
}

//=============================================================================
// Global Variables
//=============================================================================

class GlobalVariables {
public:
    static GlobalVariables& Instance() {
        static GlobalVariables instance;
        return instance;
    }

    bool GlobalVariableSet(const std::string& name, double value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_variables[name] = {value, TimeCurrent()};
        return true;
    }

    double GlobalVariableGet(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_variables.find(name);
        return it != m_variables.end() ? it->second.value : 0.0;
    }

    bool GlobalVariableCheck(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_variables.find(name) != m_variables.end();
    }

    bool GlobalVariableDel(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_variables.erase(name) > 0;
    }

    datetime GlobalVariableTime(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_variables.find(name);
        return it != m_variables.end() ? it->second.timestamp : 0;
    }

    bool GlobalVariableSetOnCondition(const std::string& name, double value, double checkValue) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_variables.find(name);
        if (it != m_variables.end() && it->second.value == checkValue) {
            it->second.value = value;
            it->second.timestamp = TimeCurrent();
            return true;
        }
        return false;
    }

    int GlobalVariablesTotal() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return static_cast<int>(m_variables.size());
    }

    std::string GlobalVariableName(int index) {
        std::lock_guard<std::mutex> lock(m_mutex);
        int i = 0;
        for (auto& pair : m_variables) {
            if (i == index) return pair.first;
            i++;
        }
        return "";
    }

    int GlobalVariablesDeleteAll(const std::string& prefix = "", datetime limitTime = 0) {
        std::lock_guard<std::mutex> lock(m_mutex);
        int count = 0;
        for (auto it = m_variables.begin(); it != m_variables.end();) {
            bool shouldDelete = true;
            if (!prefix.empty() && it->first.find(prefix) != 0) {
                shouldDelete = false;
            }
            if (limitTime > 0 && it->second.timestamp >= limitTime) {
                shouldDelete = false;
            }
            if (shouldDelete) {
                it = m_variables.erase(it);
                count++;
            } else {
                ++it;
            }
        }
        return count;
    }

    void GlobalVariablesFlush() {
        // In MQL5, this saves to file. Here we just make sure everything is sync'd.
        std::lock_guard<std::mutex> lock(m_mutex);
    }

    double GlobalVariableTemp(const std::string& name) {
        // Creates a temporary global variable
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_variables.find(name);
        if (it == m_variables.end()) {
            m_variables[name] = {0.0, TimeCurrent()};
            m_tempVars.insert(name);
            return 0.0;
        }
        return it->second.value;
    }

private:
    GlobalVariables() = default;

    struct GVEntry {
        double value;
        datetime timestamp;
    };

    std::map<std::string, GVEntry> m_variables;
    std::set<std::string> m_tempVars;
    std::mutex m_mutex;
};

// Global variable function wrappers
inline bool GlobalVariableSet(const std::string& name, double value) {
    return GlobalVariables::Instance().GlobalVariableSet(name, value);
}

inline double GlobalVariableGet(const std::string& name) {
    return GlobalVariables::Instance().GlobalVariableGet(name);
}

inline bool GlobalVariableCheck(const std::string& name) {
    return GlobalVariables::Instance().GlobalVariableCheck(name);
}

inline bool GlobalVariableDel(const std::string& name) {
    return GlobalVariables::Instance().GlobalVariableDel(name);
}

inline datetime GlobalVariableTime(const std::string& name) {
    return GlobalVariables::Instance().GlobalVariableTime(name);
}

inline bool GlobalVariableSetOnCondition(const std::string& name, double value, double checkValue) {
    return GlobalVariables::Instance().GlobalVariableSetOnCondition(name, value, checkValue);
}

inline int GlobalVariablesTotal() {
    return GlobalVariables::Instance().GlobalVariablesTotal();
}

inline std::string GlobalVariableName(int index) {
    return GlobalVariables::Instance().GlobalVariableName(index);
}

inline int GlobalVariablesDeleteAll(const std::string& prefix = "", datetime limitTime = 0) {
    return GlobalVariables::Instance().GlobalVariablesDeleteAll(prefix, limitTime);
}

inline void GlobalVariablesFlush() {
    GlobalVariables::Instance().GlobalVariablesFlush();
}

inline double GlobalVariableTemp(const std::string& name) {
    return GlobalVariables::Instance().GlobalVariableTemp(name);
}

//=============================================================================
// Event System
//=============================================================================

class EventManager {
public:
    static EventManager& Instance() {
        static EventManager instance;
        return instance;
    }

    using TimerCallback = std::function<void()>;
    using TradeCallback = std::function<void()>;
    using TickCallback = std::function<void()>;
    using ChartEventCallback = std::function<void(int, long, double, const std::string&)>;
    using DeinitCallback = std::function<void(int)>;
    using TesterCallback = std::function<double()>;

    void SetOnTimer(TimerCallback callback) { m_onTimer = callback; }
    void SetOnTrade(TradeCallback callback) { m_onTrade = callback; }
    void SetOnTick(TickCallback callback) { m_onTick = callback; }
    void SetOnChartEvent(ChartEventCallback callback) { m_onChartEvent = callback; }
    void SetOnDeinit(DeinitCallback callback) { m_onDeinit = callback; }
    void SetOnTester(TesterCallback callback) { m_onTester = callback; }

    bool EventSetTimer(int seconds) {
        if (m_timerRunning) return false;

        m_timerRunning = true;
        m_timerInterval = seconds;

        m_timerThread = std::thread([this]() {
            while (m_timerRunning) {
                std::unique_lock<std::mutex> lock(m_timerMutex);
                if (m_timerCv.wait_for(lock, std::chrono::seconds(m_timerInterval),
                    [this]() { return !m_timerRunning; })) {
                    break;
                }
                if (m_onTimer && m_timerRunning) {
                    m_onTimer();
                }
            }
        });

        return true;
    }

    bool EventSetMillisecondTimer(int milliseconds) {
        if (m_timerRunning) return false;

        m_timerRunning = true;
        m_timerInterval = milliseconds;

        m_timerThread = std::thread([this]() {
            while (m_timerRunning) {
                std::unique_lock<std::mutex> lock(m_timerMutex);
                if (m_timerCv.wait_for(lock, std::chrono::milliseconds(m_timerInterval),
                    [this]() { return !m_timerRunning; })) {
                    break;
                }
                if (m_onTimer && m_timerRunning) {
                    m_onTimer();
                }
            }
        });

        return true;
    }

    void EventKillTimer() {
        if (!m_timerRunning) return;

        m_timerRunning = false;
        m_timerCv.notify_all();

        if (m_timerThread.joinable()) {
            m_timerThread.join();
        }
    }

    bool EventChartCustom(long chartId, ushort customEventId, long lparam, double dparam, const std::string& sparam) {
        if (m_onChartEvent) {
            m_onChartEvent(CHARTEVENT_CUSTOM + customEventId, lparam, dparam, sparam);
            return true;
        }
        return false;
    }

    void TriggerOnTrade() {
        if (m_onTrade) m_onTrade();
    }

    void TriggerOnTick() {
        if (m_onTick) m_onTick();
    }

    void TriggerOnDeinit(int reason) {
        if (m_onDeinit) m_onDeinit(reason);
    }

    double TriggerOnTester() {
        if (m_onTester) return m_onTester();
        return 0.0;
    }

    ~EventManager() {
        EventKillTimer();
    }

private:
    EventManager() : m_timerRunning(false), m_timerInterval(0) {}

    TimerCallback m_onTimer;
    TradeCallback m_onTrade;
    TickCallback m_onTick;
    ChartEventCallback m_onChartEvent;
    DeinitCallback m_onDeinit;
    TesterCallback m_onTester;

    std::atomic<bool> m_timerRunning;
    int m_timerInterval;
    std::thread m_timerThread;
    std::mutex m_timerMutex;
    std::condition_variable m_timerCv;
};

// Event function wrappers
inline bool EventSetTimer(int seconds) {
    return EventManager::Instance().EventSetTimer(seconds);
}

inline bool EventSetMillisecondTimer(int milliseconds) {
    return EventManager::Instance().EventSetMillisecondTimer(milliseconds);
}

inline void EventKillTimer() {
    EventManager::Instance().EventKillTimer();
}

inline bool EventChartCustom(long chartId, ushort customEventId, long lparam = 0, double dparam = 0.0, const std::string& sparam = "") {
    return EventManager::Instance().EventChartCustom(chartId, customEventId, lparam, dparam, sparam);
}

//=============================================================================
// Chart Functions (Stubs for backtest - no actual chart)
//=============================================================================

inline long ChartOpen(const std::string& symbol, ENUM_TIMEFRAMES period) {
    // In backtest mode, return a fake chart ID
    static long nextChartId = 1;
    return nextChartId++;
}

inline bool ChartClose(long chartId = 0) {
    return true;  // Stub
}

inline long ChartFirst() {
    return 0;  // Stub
}

inline long ChartNext(long chartId) {
    return -1;  // Stub - no more charts
}

inline bool ChartSetSymbolPeriod(long chartId, const std::string& symbol, ENUM_TIMEFRAMES period) {
    return true;  // Stub
}

inline std::string ChartSymbol(long chartId = 0) {
    return "";  // Stub
}

inline ENUM_TIMEFRAMES ChartPeriod(long chartId = 0) {
    return PERIOD_CURRENT;  // Stub
}

inline void ChartRedraw(long chartId = 0) {
    // Stub - no actual redraw in backtest
}

inline bool ChartSetInteger(long chartId, ENUM_CHART_PROPERTY_INTEGER prop, long value) {
    return true;  // Stub
}

inline bool ChartSetDouble(long chartId, ENUM_CHART_PROPERTY_DOUBLE prop, double value) {
    return true;  // Stub
}

inline bool ChartSetString(long chartId, ENUM_CHART_PROPERTY_STRING prop, const std::string& value) {
    return true;  // Stub
}

inline long ChartGetInteger(long chartId, ENUM_CHART_PROPERTY_INTEGER prop, int subWindow = 0) {
    return 0;  // Stub
}

inline double ChartGetDouble(long chartId, ENUM_CHART_PROPERTY_DOUBLE prop, int subWindow = 0) {
    return 0.0;  // Stub
}

inline std::string ChartGetString(long chartId, ENUM_CHART_PROPERTY_STRING prop) {
    return "";  // Stub
}

inline long ChartID() {
    return 0;  // Return main chart ID
}

inline int ChartWindowFind(long chartId = 0, const std::string& indicatorShortName = "") {
    return 0;  // Stub - main window
}

inline bool ChartIndicatorAdd(long chartId, int subWindow, int indicatorHandle) {
    return true;  // Stub
}

inline bool ChartIndicatorDelete(long chartId, int subWindow, const std::string& indicatorShortName) {
    return true;  // Stub
}

inline std::string ChartIndicatorName(long chartId, int subWindow, int index) {
    return "";  // Stub
}

inline int ChartIndicatorsTotal(long chartId, int subWindow) {
    return 0;  // Stub
}

inline int ChartWindowOnDropped() {
    return 0;  // Stub
}

inline double ChartPriceOnDropped() {
    return 0.0;  // Stub
}

inline datetime ChartTimeOnDropped() {
    return 0;  // Stub
}

inline int ChartXOnDropped() {
    return 0;  // Stub
}

inline int ChartYOnDropped() {
    return 0;  // Stub
}

inline bool ChartNavigate(long chartId, int position, int shift = 0) {
    return true;  // Stub
}

inline bool ChartApplyTemplate(long chartId, const std::string& filename) {
    return true;  // Stub
}

inline bool ChartSaveTemplate(long chartId, const std::string& filename) {
    return true;  // Stub
}

inline bool ChartScreenShot(long chartId, const std::string& filename, int width, int height, int alignMode = 0) {
    return true;  // Stub
}

//=============================================================================
// Object Functions (Stubs for backtest)
//=============================================================================

inline bool ObjectCreate(long chartId, const std::string& name, ENUM_OBJECT type,
                         int subWindow, datetime time1, double price1,
                         datetime time2 = 0, double price2 = 0,
                         datetime time3 = 0, double price3 = 0) {
    return true;  // Stub
}

inline std::string ObjectName(long chartId, int index, int subWindow = -1, int type = -1) {
    return "";  // Stub
}

inline bool ObjectDelete(long chartId, const std::string& name) {
    return true;  // Stub
}

inline int ObjectsDeleteAll(long chartId, int subWindow = -1, int type = -1) {
    return 0;  // Stub
}

inline int ObjectFind(long chartId, const std::string& name) {
    return -1;  // Stub - not found
}

inline datetime ObjectGetTimeByValue(long chartId, const std::string& name, double value, int lineId = 0) {
    return 0;  // Stub
}

inline double ObjectGetValueByTime(long chartId, const std::string& name, datetime time, int lineId = 0) {
    return 0.0;  // Stub
}

inline bool ObjectMove(long chartId, const std::string& name, int pointIndex, datetime time, double price) {
    return true;  // Stub
}

inline int ObjectsTotal(long chartId, int subWindow = -1, int type = -1) {
    return 0;  // Stub
}

inline bool ObjectSetInteger(long chartId, const std::string& name, ENUM_OBJECT_PROPERTY_INTEGER prop, long value) {
    return true;  // Stub
}

inline bool ObjectSetDouble(long chartId, const std::string& name, ENUM_OBJECT_PROPERTY_DOUBLE prop, double value) {
    return true;  // Stub
}

inline bool ObjectSetString(long chartId, const std::string& name, ENUM_OBJECT_PROPERTY_STRING prop, const std::string& value) {
    return true;  // Stub
}

inline long ObjectGetInteger(long chartId, const std::string& name, ENUM_OBJECT_PROPERTY_INTEGER prop, int modifier = 0) {
    return 0;  // Stub
}

inline double ObjectGetDouble(long chartId, const std::string& name, ENUM_OBJECT_PROPERTY_DOUBLE prop, int modifier = 0) {
    return 0.0;  // Stub
}

inline std::string ObjectGetString(long chartId, const std::string& name, ENUM_OBJECT_PROPERTY_STRING prop, int modifier = 0) {
    return "";  // Stub
}

inline std::string ObjectDescription(const std::string& name) {
    return "";  // Stub
}

//=============================================================================
// Custom Indicators
//=============================================================================

class IndicatorManager {
public:
    static IndicatorManager& Instance() {
        static IndicatorManager instance;
        return instance;
    }

    using IndicatorCalcFunc = std::function<void(int, int, datetime, double, double, double, double, long,
                                                  std::vector<double>&)>;

    int RegisterIndicator(const std::string& name, IndicatorCalcFunc calcFunc) {
        int handle = m_nextHandle++;
        m_indicators[handle] = {name, calcFunc, {}};
        return handle;
    }

    int iCustom(const std::string& symbol, ENUM_TIMEFRAMES period, const std::string& name, ...) {
        // Look for a registered indicator with this name
        for (auto& pair : m_indicators) {
            if (pair.second.name == name) {
                return pair.first;
            }
        }
        // Not found - register a dummy
        return RegisterIndicator(name, nullptr);
    }

    double GetBuffer(int handle, int bufferIndex, int shift) {
        auto it = m_indicators.find(handle);
        if (it == m_indicators.end()) return EMPTY_VALUE;
        if (bufferIndex >= static_cast<int>(it->second.buffers.size())) return EMPTY_VALUE;
        if (shift >= static_cast<int>(it->second.buffers[bufferIndex].size())) return EMPTY_VALUE;
        return it->second.buffers[bufferIndex][shift];
    }

    void SetBuffer(int handle, int bufferIndex, int shift, double value) {
        auto it = m_indicators.find(handle);
        if (it == m_indicators.end()) return;
        while (bufferIndex >= static_cast<int>(it->second.buffers.size())) {
            it->second.buffers.push_back({});
        }
        while (shift >= static_cast<int>(it->second.buffers[bufferIndex].size())) {
            it->second.buffers[bufferIndex].push_back(EMPTY_VALUE);
        }
        it->second.buffers[bufferIndex][shift] = value;
    }

    void ReleaseIndicator(int handle) {
        m_indicators.erase(handle);
    }

private:
    IndicatorManager() : m_nextHandle(1) {}

    struct IndicatorData {
        std::string name;
        IndicatorCalcFunc calcFunc;
        std::vector<std::vector<double>> buffers;
    };

    int m_nextHandle;
    std::map<int, IndicatorData> m_indicators;
};

// Indicator creation functions
inline int iCustom(const std::string& symbol, ENUM_TIMEFRAMES period, const std::string& name) {
    return IndicatorManager::Instance().iCustom(symbol, period, name);
}

inline int iMA(const std::string& symbol, ENUM_TIMEFRAMES period, int maPeriod, int maShift, ENUM_MA_METHOD maMethod, ENUM_APPLIED_PRICE appliedPrice) {
    return IndicatorManager::Instance().RegisterIndicator("MA", nullptr);
}

inline int iRSI(const std::string& symbol, ENUM_TIMEFRAMES period, int maPeriod, ENUM_APPLIED_PRICE appliedPrice) {
    return IndicatorManager::Instance().RegisterIndicator("RSI", nullptr);
}

inline int iMACD(const std::string& symbol, ENUM_TIMEFRAMES period, int fastEma, int slowEma, int signalPeriod, ENUM_APPLIED_PRICE appliedPrice) {
    return IndicatorManager::Instance().RegisterIndicator("MACD", nullptr);
}

inline int iBands(const std::string& symbol, ENUM_TIMEFRAMES period, int bandsPeriod, int bandsShift, double deviation, ENUM_APPLIED_PRICE appliedPrice) {
    return IndicatorManager::Instance().RegisterIndicator("Bands", nullptr);
}

inline int iATR(const std::string& symbol, ENUM_TIMEFRAMES period, int maPeriod) {
    return IndicatorManager::Instance().RegisterIndicator("ATR", nullptr);
}

inline int iADX(const std::string& symbol, ENUM_TIMEFRAMES period, int adxPeriod) {
    return IndicatorManager::Instance().RegisterIndicator("ADX", nullptr);
}

inline int iStochastic(const std::string& symbol, ENUM_TIMEFRAMES period, int kPeriod, int dPeriod, int slowing, ENUM_MA_METHOD maMethod, ENUM_APPLIED_PRICE priceField) {
    return IndicatorManager::Instance().RegisterIndicator("Stochastic", nullptr);
}

inline int iCCI(const std::string& symbol, ENUM_TIMEFRAMES period, int maPeriod, ENUM_APPLIED_PRICE appliedPrice) {
    return IndicatorManager::Instance().RegisterIndicator("CCI", nullptr);
}

// Buffer copy functions
inline int CopyBuffer(int indicatorHandle, int bufferNum, int startPos, int count, std::vector<double>& buffer) {
    buffer.resize(count);
    for (int i = 0; i < count; ++i) {
        buffer[i] = IndicatorManager::Instance().GetBuffer(indicatorHandle, bufferNum, startPos + i);
    }
    return count;
}

inline void IndicatorRelease(int handle) {
    IndicatorManager::Instance().ReleaseIndicator(handle);
}

//=============================================================================
// Network Functions (Stubs - not implemented in backtest)
//=============================================================================

inline int WebRequest(const std::string& method, const std::string& url, const std::string& headers,
                      int timeout, const std::string& data, std::string& result, std::string& resultHeaders) {
    // Stub - would need actual HTTP implementation
    return 0;  // Return error
}

inline int SocketCreate() {
    return INVALID_HANDLE;  // Stub
}

inline bool SocketConnect(int socket, const std::string& server, uint port, uint timeout) {
    return false;  // Stub
}

inline bool SocketIsConnected(int socket) {
    return false;  // Stub
}

inline void SocketClose(int socket) {
    // Stub
}

inline int SocketSend(int socket, const std::vector<uchar>& data) {
    return 0;  // Stub
}

inline int SocketRead(int socket, std::vector<uchar>& data, int maxLen, uint timeout) {
    return 0;  // Stub
}

inline bool SocketIsReadable(int socket) {
    return false;  // Stub
}

inline bool SocketIsWritable(int socket) {
    return false;  // Stub
}

//=============================================================================
// Resource Functions (Stubs)
//=============================================================================

inline bool ResourceCreate(const std::string& resourceName, const std::vector<uint>& data, int width, int height, int dataOffset, int dataTotal, int colorFormat) {
    return true;  // Stub
}

inline bool ResourceFree(const std::string& resourceName) {
    return true;  // Stub
}

inline bool ResourceReadImage(const std::string& resourceName, std::vector<uint>& data, int& width, int& height) {
    return false;  // Stub
}

inline bool ResourceSave(const std::string& resourceName, const std::string& fileName) {
    return false;  // Stub
}

//=============================================================================
// History Selection Functions
//=============================================================================

class HistoryManager {
public:
    static HistoryManager& Instance() {
        static HistoryManager instance;
        return instance;
    }

    void AddDeal(ulong ticket, datetime time, const std::string& symbol, ENUM_DEAL_TYPE type,
                 ENUM_DEAL_ENTRY entry, double volume, double price, double commission,
                 double swap, double profit, ulong magic, ulong positionId, ulong orderId,
                 const std::string& comment) {
        DealRecord deal;
        deal.ticket = ticket;
        deal.time = time;
        deal.symbol = symbol;
        deal.type = type;
        deal.entry = entry;
        deal.volume = volume;
        deal.price = price;
        deal.commission = commission;
        deal.swap = swap;
        deal.profit = profit;
        deal.magic = magic;
        deal.positionId = positionId;
        deal.orderId = orderId;
        deal.comment = comment;
        m_deals.push_back(deal);
    }

    void AddOrder(ulong ticket, datetime timeSetup, datetime timeDone, const std::string& symbol,
                  ENUM_ORDER_TYPE type, double volume, double price, double sl, double tp,
                  ulong magic, ulong positionId, const std::string& comment) {
        OrderRecord order;
        order.ticket = ticket;
        order.timeSetup = timeSetup;
        order.timeDone = timeDone;
        order.symbol = symbol;
        order.type = type;
        order.volume = volume;
        order.price = price;
        order.sl = sl;
        order.tp = tp;
        order.magic = magic;
        order.positionId = positionId;
        order.comment = comment;
        m_orders.push_back(order);
    }

    bool HistorySelect(datetime from, datetime to) {
        m_selectedDeals.clear();
        m_selectedOrders.clear();

        for (auto& deal : m_deals) {
            if (deal.time >= from && deal.time <= to) {
                m_selectedDeals.push_back(&deal);
            }
        }

        for (auto& order : m_orders) {
            if (order.timeSetup >= from && order.timeDone <= to) {
                m_selectedOrders.push_back(&order);
            }
        }

        return true;
    }

    bool HistorySelectByPosition(long positionId) {
        m_selectedDeals.clear();
        m_selectedOrders.clear();

        for (auto& deal : m_deals) {
            if (deal.positionId == static_cast<ulong>(positionId)) {
                m_selectedDeals.push_back(&deal);
            }
        }

        for (auto& order : m_orders) {
            if (order.positionId == static_cast<ulong>(positionId)) {
                m_selectedOrders.push_back(&order);
            }
        }

        return true;
    }

    int HistoryDealsTotal() const {
        return static_cast<int>(m_selectedDeals.size());
    }

    int HistoryOrdersTotal() const {
        return static_cast<int>(m_selectedOrders.size());
    }

    ulong HistoryDealGetTicket(int index) const {
        if (index >= 0 && index < static_cast<int>(m_selectedDeals.size())) {
            return m_selectedDeals[index]->ticket;
        }
        return 0;
    }

    ulong HistoryOrderGetTicket(int index) const {
        if (index >= 0 && index < static_cast<int>(m_selectedOrders.size())) {
            return m_selectedOrders[index]->ticket;
        }
        return 0;
    }

    bool HistoryDealSelect(ulong ticket) {
        for (auto& deal : m_deals) {
            if (deal.ticket == ticket) {
                m_currentDeal = &deal;
                return true;
            }
        }
        return false;
    }

    bool HistoryOrderSelect(ulong ticket) {
        for (auto& order : m_orders) {
            if (order.ticket == ticket) {
                m_currentOrder = &order;
                return true;
            }
        }
        return false;
    }

    // Deal property getters
    long HistoryDealGetInteger(ulong ticket, ENUM_DEAL_PROPERTY_INTEGER prop) {
        HistoryDealSelect(ticket);
        if (!m_currentDeal) return 0;

        switch (prop) {
            case DEAL_TICKET: return m_currentDeal->ticket;
            case DEAL_ORDER: return m_currentDeal->orderId;
            case DEAL_TIME: return m_currentDeal->time;
            case DEAL_TIME_MSC: return m_currentDeal->time * 1000;
            case DEAL_TYPE: return m_currentDeal->type;
            case DEAL_ENTRY: return m_currentDeal->entry;
            case DEAL_MAGIC: return m_currentDeal->magic;
            case DEAL_POSITION_ID: return m_currentDeal->positionId;
            default: return 0;
        }
    }

    double HistoryDealGetDouble(ulong ticket, ENUM_DEAL_PROPERTY_DOUBLE prop) {
        HistoryDealSelect(ticket);
        if (!m_currentDeal) return 0.0;

        switch (prop) {
            case DEAL_VOLUME: return m_currentDeal->volume;
            case DEAL_PRICE: return m_currentDeal->price;
            case DEAL_COMMISSION: return m_currentDeal->commission;
            case DEAL_SWAP: return m_currentDeal->swap;
            case DEAL_PROFIT: return m_currentDeal->profit;
            default: return 0.0;
        }
    }

    std::string HistoryDealGetString(ulong ticket, ENUM_DEAL_PROPERTY_STRING prop) {
        HistoryDealSelect(ticket);
        if (!m_currentDeal) return "";

        switch (prop) {
            case DEAL_SYMBOL: return m_currentDeal->symbol;
            case DEAL_COMMENT: return m_currentDeal->comment;
            default: return "";
        }
    }

    // Order property getters
    long HistoryOrderGetInteger(ulong ticket, ENUM_ORDER_PROPERTY_INTEGER prop) {
        HistoryOrderSelect(ticket);
        if (!m_currentOrder) return 0;

        switch (prop) {
            case ORDER_TICKET: return m_currentOrder->ticket;
            case ORDER_TIME_SETUP: return m_currentOrder->timeSetup;
            case ORDER_TIME_DONE: return m_currentOrder->timeDone;
            case ORDER_TYPE: return m_currentOrder->type;
            case ORDER_MAGIC: return m_currentOrder->magic;
            case ORDER_POSITION_ID: return m_currentOrder->positionId;
            default: return 0;
        }
    }

    double HistoryOrderGetDouble(ulong ticket, ENUM_ORDER_PROPERTY_DOUBLE prop) {
        HistoryOrderSelect(ticket);
        if (!m_currentOrder) return 0.0;

        switch (prop) {
            case ORDER_VOLUME_INITIAL: return m_currentOrder->volume;
            case ORDER_PRICE_OPEN: return m_currentOrder->price;
            case ORDER_SL: return m_currentOrder->sl;
            case ORDER_TP: return m_currentOrder->tp;
            default: return 0.0;
        }
    }

    std::string HistoryOrderGetString(ulong ticket, ENUM_ORDER_PROPERTY_STRING prop) {
        HistoryOrderSelect(ticket);
        if (!m_currentOrder) return "";

        switch (prop) {
            case ORDER_SYMBOL: return m_currentOrder->symbol;
            case ORDER_COMMENT: return m_currentOrder->comment;
            default: return "";
        }
    }

    void Clear() {
        m_deals.clear();
        m_orders.clear();
        m_selectedDeals.clear();
        m_selectedOrders.clear();
        m_currentDeal = nullptr;
        m_currentOrder = nullptr;
    }

private:
    HistoryManager() : m_currentDeal(nullptr), m_currentOrder(nullptr) {}

    struct DealRecord {
        ulong ticket;
        datetime time;
        std::string symbol;
        ENUM_DEAL_TYPE type;
        ENUM_DEAL_ENTRY entry;
        double volume;
        double price;
        double commission;
        double swap;
        double profit;
        ulong magic;
        ulong positionId;
        ulong orderId;
        std::string comment;
    };

    struct OrderRecord {
        ulong ticket;
        datetime timeSetup;
        datetime timeDone;
        std::string symbol;
        ENUM_ORDER_TYPE type;
        double volume;
        double price;
        double sl;
        double tp;
        ulong magic;
        ulong positionId;
        std::string comment;
    };

    std::vector<DealRecord> m_deals;
    std::vector<OrderRecord> m_orders;
    std::vector<DealRecord*> m_selectedDeals;
    std::vector<OrderRecord*> m_selectedOrders;
    DealRecord* m_currentDeal;
    OrderRecord* m_currentOrder;
};

// History function wrappers
inline bool HistorySelect(datetime from, datetime to) {
    return HistoryManager::Instance().HistorySelect(from, to);
}

inline bool HistorySelectByPosition(long positionId) {
    return HistoryManager::Instance().HistorySelectByPosition(positionId);
}

inline int HistoryDealsTotal() {
    return HistoryManager::Instance().HistoryDealsTotal();
}

inline int HistoryOrdersTotal() {
    return HistoryManager::Instance().HistoryOrdersTotal();
}

inline ulong HistoryDealGetTicket(int index) {
    return HistoryManager::Instance().HistoryDealGetTicket(index);
}

inline ulong HistoryOrderGetTicket(int index) {
    return HistoryManager::Instance().HistoryOrderGetTicket(index);
}

inline bool HistoryDealSelect(ulong ticket) {
    return HistoryManager::Instance().HistoryDealSelect(ticket);
}

inline bool HistoryOrderSelect(ulong ticket) {
    return HistoryManager::Instance().HistoryOrderSelect(ticket);
}

inline long HistoryDealGetInteger(ulong ticket, ENUM_DEAL_PROPERTY_INTEGER prop) {
    return HistoryManager::Instance().HistoryDealGetInteger(ticket, prop);
}

inline double HistoryDealGetDouble(ulong ticket, ENUM_DEAL_PROPERTY_DOUBLE prop) {
    return HistoryManager::Instance().HistoryDealGetDouble(ticket, prop);
}

inline std::string HistoryDealGetString(ulong ticket, ENUM_DEAL_PROPERTY_STRING prop) {
    return HistoryManager::Instance().HistoryDealGetString(ticket, prop);
}

inline long HistoryOrderGetInteger(ulong ticket, ENUM_ORDER_PROPERTY_INTEGER prop) {
    return HistoryManager::Instance().HistoryOrderGetInteger(ticket, prop);
}

inline double HistoryOrderGetDouble(ulong ticket, ENUM_ORDER_PROPERTY_DOUBLE prop) {
    return HistoryManager::Instance().HistoryOrderGetDouble(ticket, prop);
}

inline std::string HistoryOrderGetString(ulong ticket, ENUM_ORDER_PROPERTY_STRING prop) {
    return HistoryManager::Instance().HistoryOrderGetString(ticket, prop);
}

//=============================================================================
// Tester Functions
//=============================================================================

inline bool TesterStatistics(int statisticId, double& value) {
    // Stub - would be populated by backtest engine
    value = 0.0;
    return true;
}

inline void TesterStop() {
    // Stub - signal to stop optimization
}

inline void TesterDeposit(double money) {
    // Stub - add money during backtest
}

inline void TesterWithdrawal(double money) {
    // Stub - withdraw money during backtest
}

inline bool MQLInfoInteger(int propertyId) {
    // Stub - MQL execution info
    return false;
}

inline std::string MQLInfoString(int propertyId) {
    return "";  // Stub
}

//=============================================================================
// Series Access Functions
//=============================================================================

inline int CopyRates(const std::string& symbol, ENUM_TIMEFRAMES period, int startPos, int count, std::vector<MqlRates>& ratesArray) {
    // Stub - would be implemented by data provider
    return 0;
}

inline int CopyTime(const std::string& symbol, ENUM_TIMEFRAMES period, int startPos, int count, std::vector<datetime>& timeArray) {
    return 0;  // Stub
}

inline int CopyOpen(const std::string& symbol, ENUM_TIMEFRAMES period, int startPos, int count, std::vector<double>& openArray) {
    return 0;  // Stub
}

inline int CopyHigh(const std::string& symbol, ENUM_TIMEFRAMES period, int startPos, int count, std::vector<double>& highArray) {
    return 0;  // Stub
}

inline int CopyLow(const std::string& symbol, ENUM_TIMEFRAMES period, int startPos, int count, std::vector<double>& lowArray) {
    return 0;  // Stub
}

inline int CopyClose(const std::string& symbol, ENUM_TIMEFRAMES period, int startPos, int count, std::vector<double>& closeArray) {
    return 0;  // Stub
}

inline int CopyTickVolume(const std::string& symbol, ENUM_TIMEFRAMES period, int startPos, int count, std::vector<long>& volumeArray) {
    return 0;  // Stub
}

inline int CopyRealVolume(const std::string& symbol, ENUM_TIMEFRAMES period, int startPos, int count, std::vector<long>& volumeArray) {
    return 0;  // Stub
}

inline int CopySpread(const std::string& symbol, ENUM_TIMEFRAMES period, int startPos, int count, std::vector<int>& spreadArray) {
    return 0;  // Stub
}

inline int CopyTicks(const std::string& symbol, std::vector<MqlTick>& ticksArray, uint flags = 0, ulong from = 0, uint count = 0) {
    return 0;  // Stub
}

inline int CopyTicksRange(const std::string& symbol, std::vector<MqlTick>& ticksArray, uint flags, ulong from, ulong to) {
    return 0;  // Stub
}

//=============================================================================
// Bars and iBarShift
//=============================================================================

inline int Bars(const std::string& symbol, ENUM_TIMEFRAMES period) {
    return 0;  // Stub - would be provided by data manager
}

inline int iBars(const std::string& symbol, ENUM_TIMEFRAMES period) {
    return Bars(symbol, period);
}

inline int iBarShift(const std::string& symbol, ENUM_TIMEFRAMES period, datetime time, bool exact = false) {
    return 0;  // Stub
}

inline datetime iTime(const std::string& symbol, ENUM_TIMEFRAMES period, int shift) {
    return 0;  // Stub
}

inline double iOpen(const std::string& symbol, ENUM_TIMEFRAMES period, int shift) {
    return 0.0;  // Stub
}

inline double iHigh(const std::string& symbol, ENUM_TIMEFRAMES period, int shift) {
    return 0.0;  // Stub
}

inline double iLow(const std::string& symbol, ENUM_TIMEFRAMES period, int shift) {
    return 0.0;  // Stub
}

inline double iClose(const std::string& symbol, ENUM_TIMEFRAMES period, int shift) {
    return 0.0;  // Stub
}

inline long iVolume(const std::string& symbol, ENUM_TIMEFRAMES period, int shift) {
    return 0;  // Stub
}

inline int iLowest(const std::string& symbol, ENUM_TIMEFRAMES period, int mode, int count, int start) {
    return 0;  // Stub
}

inline int iHighest(const std::string& symbol, ENUM_TIMEFRAMES period, int mode, int count, int start) {
    return 0;  // Stub
}

//=============================================================================
// SendNotification, SendMail, PlaySound
//=============================================================================

inline bool SendNotification(const std::string& text) {
    Print("[NOTIFICATION] ", text);
    return true;
}

inline bool SendMail(const std::string& subject, const std::string& text) {
    Print("[EMAIL] Subject: ", subject, " Body: ", text);
    return true;
}

inline bool PlaySound(const std::string& filename) {
    Print("[SOUND] ", filename);
    return true;
}

//=============================================================================
// MessageBox
//=============================================================================

enum ENUM_MB_RESULT {
    IDOK = 1,
    IDCANCEL = 2,
    IDABORT = 3,
    IDRETRY = 4,
    IDIGNORE = 5,
    IDYES = 6,
    IDNO = 7
};

inline int MessageBox(const std::string& text, const std::string& caption = "", int flags = 0) {
    Print("[MESSAGEBOX] ", caption, ": ", text);
    return IDOK;  // Stub - always returns OK
}

} // namespace mql5

#endif // MQL5_EXTENDED_H
