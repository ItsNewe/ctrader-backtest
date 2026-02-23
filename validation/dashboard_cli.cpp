/**
 * Dashboard CLI - Extended backtest CLI for the React dashboard
 *
 * Extends the original backtest_cli with:
 * - CLI flags for all broker settings (contract-size, leverage, swap, pip-size)
 * - Full trade list output in JSON
 * - Max equity samples capping for browser performance
 * - All strategy parameters as CLI flags
 *
 * Usage: dashboard_cli.exe --symbol XAUUSD --strategy fillup --survive 13.0 --spacing 1.5 ...
 *
 * Build via CMake (in validation/CMakeLists.txt) or:
 *   g++ -O3 -mavx2 -mfma -std=c++17 -I ../include validation/dashboard_cli.cpp -o build/validation/dashboard_cli.exe
 */

#include "../include/fill_up_oscillation.h"
#include "../include/strategy_combined_ju.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <iomanip>

using namespace backtest;
namespace fs = std::filesystem;

// ──────────────────── Environment & File Discovery ────────────────────

std::string GetEnvVar(const std::string& name, const std::string& default_value = "") {
    const char* value = std::getenv(name.c_str());
    return value ? std::string(value) : default_value;
}

std::string FindTickDataFile(const std::string& symbol) {
    // 1. Symbol-specific env var
    std::string env_path = GetEnvVar("BACKTEST_" + symbol);
    if (!env_path.empty() && fs::exists(env_path)) return env_path;

    // 2. BACKTEST_DATA_DIR + symbol
    std::string data_dir = GetEnvVar("BACKTEST_DATA_DIR");
    std::vector<std::string> suffixes = {"_TICKS_2025.csv", "_TESTER_TICKS.csv", "_TICKS.csv", ".csv"};
    if (!data_dir.empty()) {
        for (const auto& suffix : suffixes) {
            std::string path = data_dir + "/" + symbol + suffix;
            if (fs::exists(path)) return path;
        }
    }

    // 3. Common relative directories
    std::vector<std::string> search_dirs = {
        "data", "validation", "validation/Grid", "../data", "../validation",
        "../validation/Grid", "tick_data", "ticks", "../tick_data"
    };
    for (const auto& dir : search_dirs) {
        for (const auto& suffix : suffixes) {
            std::string path = dir + "/" + symbol + suffix;
            if (fs::exists(path)) return path;
        }
    }

    // 4. User home directory
    std::string home = GetEnvVar("HOME", GetEnvVar("USERPROFILE"));
    if (!home.empty()) {
        std::vector<std::string> home_dirs = {
            home + "/Documents/ctrader-backtest/validation/Grid",
            home + "/Documents/backtest/data",
            home + "/backtest/data",
            home + "/tick_data"
        };
        for (const auto& dir : home_dirs) {
            for (const auto& suffix : suffixes) {
                std::string path = dir + "/" + symbol + suffix;
                if (fs::exists(path)) return path;
            }
        }
    }

    return "";
}

// ──────────────────── JSON Output Helper ────────────────────

class JsonOutput {
public:
    std::ostringstream ss;
    bool first = true;

    void begin_object() { ss << "{"; first = true; }
    void end_object() { ss << "}"; }
    void begin_array() { ss << "["; first = true; }
    void end_array() { ss << "]"; }

    void comma() {
        if (!first) ss << ",";
        first = false;
    }

    void key(const std::string& k) {
        comma();
        ss << "\"" << k << "\":";
        first = true; // next value doesn't need comma
    }

    void value_str(const std::string& v) { ss << "\"" << v << "\""; first = false; }
    void value_double(double v) { ss << std::fixed << std::setprecision(2) << v; first = false; }
    void value_double_precise(double v, int prec) { ss << std::fixed << std::setprecision(prec) << v; first = false; }
    void value_int(int v) { ss << v; first = false; }
    void value_size(size_t v) { ss << v; first = false; }
    void value_bool(bool v) { ss << (v ? "true" : "false"); first = false; }

    std::string str() { return ss.str(); }
};

// ──────────────────── Configuration ────────────────────

struct Config {
    // Symbol & dates
    std::string symbol = "XAUUSD";
    std::string start_date = "2024.12.31";
    std::string end_date = "2025.01.31";
    double initial_balance = 10000.0;
    std::string strategy = "fillup";
    std::string data_path = "";
    bool verbose = false;

    // Broker settings (overridable from CLI)
    double contract_size = -1.0;  // -1 = use symbol defaults
    double leverage = -1.0;
    double pip_size = -1.0;
    double swap_long = -999.0;    // sentinel for "not set"
    double swap_short = -999.0;

    // FillUp parameters
    double survive_pct = 13.0;
    double base_spacing = 1.5;
    std::string mode = "ADAPTIVE_SPACING";
    double lookback_hours = 4.0;
    double antifragile_scale = 0.1;
    double velocity_threshold = 30.0;
    double max_spacing_mult = 30.0;
    bool pct_spacing = false;
    bool force_min_volume_entry = false;

    // CombinedJu parameters
    std::string tp_mode = "LINEAR";
    std::string sizing_mode = "UNIFORM";
    bool enable_velocity_filter = true;

    // Output control
    int max_equity_samples = 2000;
    bool include_trades = true;
    int max_trades = 10000;

    void apply_symbol_defaults() {
        if (symbol == "XAGUSD") {
            if (contract_size < 0) contract_size = 5000.0;
            if (leverage < 0) leverage = 500.0;
            if (pip_size < 0) pip_size = 0.001;
            if (swap_long < -998) swap_long = -15.0;
            if (swap_short < -998) swap_short = 13.72;
        } else {
            // Default: XAUUSD
            if (contract_size < 0) contract_size = 100.0;
            if (leverage < 0) leverage = 500.0;
            if (pip_size < 0) pip_size = 0.01;
            if (swap_long < -998) swap_long = -66.99;
            if (swap_short < -998) swap_short = 41.2;
        }
    }
};

void print_usage() {
    std::cerr << "Dashboard CLI - Extended backtest runner for React dashboard\n\n";
    std::cerr << "Usage: dashboard_cli.exe [options]\n\n";
    std::cerr << "Required:\n";
    std::cerr << "  --symbol SYMBOL         Trading symbol (default: XAUUSD)\n";
    std::cerr << "  --strategy NAME         fillup | combined (default: fillup)\n\n";
    std::cerr << "Date Range:\n";
    std::cerr << "  --start DATE            Start date YYYY.MM.DD (default: 2024.12.31)\n";
    std::cerr << "  --end DATE              End date YYYY.MM.DD (default: 2025.01.31)\n";
    std::cerr << "  --balance AMOUNT        Initial balance (default: 10000)\n\n";
    std::cerr << "Broker Settings (override symbol defaults):\n";
    std::cerr << "  --contract-size VALUE   Contract size (XAUUSD=100, XAGUSD=5000)\n";
    std::cerr << "  --leverage VALUE        Account leverage (default: 500)\n";
    std::cerr << "  --pip-size VALUE        Pip size (XAUUSD=0.01, XAGUSD=0.001)\n";
    std::cerr << "  --swap-long VALUE       Swap for long positions\n";
    std::cerr << "  --swap-short VALUE      Swap for short positions\n\n";
    std::cerr << "FillUp Parameters:\n";
    std::cerr << "  --survive PCT           Survive percentage (default: 13.0)\n";
    std::cerr << "  --spacing AMOUNT        Base spacing (default: 1.5)\n";
    std::cerr << "  --mode MODE             BASELINE|ADAPTIVE_SPACING|ANTIFRAGILE|VELOCITY_FILTER|ALL_COMBINED\n";
    std::cerr << "  --lookback HOURS        Volatility lookback (default: 4.0)\n";
    std::cerr << "  --antifragile SCALE     Antifragile scale (default: 0.1)\n";
    std::cerr << "  --velocity THRESHOLD    Velocity threshold $/hr (default: 30.0)\n";
    std::cerr << "  --max-spacing-mult N    Max spacing multiplier (default: 30.0)\n";
    std::cerr << "  --pct-spacing           Use percentage-based spacing\n";
    std::cerr << "  --force-min-volume      Force min volume entry\n\n";
    std::cerr << "CombinedJu Parameters:\n";
    std::cerr << "  --tp-mode MODE          FIXED|SQRT|LINEAR (default: LINEAR)\n";
    std::cerr << "  --sizing-mode MODE      UNIFORM|LINEAR_SIZING|THRESHOLD_SIZING (default: UNIFORM)\n";
    std::cerr << "  --no-velocity-filter    Disable velocity filter\n\n";
    std::cerr << "Output Control:\n";
    std::cerr << "  --max-equity-samples N  Cap equity curve points (default: 2000)\n";
    std::cerr << "  --max-trades N          Max trades in output (default: 10000)\n";
    std::cerr << "  --no-trades             Exclude trade list from output\n";
    std::cerr << "  --data PATH             Tick data file path (auto-detected)\n";
    std::cerr << "  --verbose               Verbose engine output\n";
    std::cerr << "  --help                  Show this help\n";
}

Config parse_args(int argc, char* argv[]) {
    Config cfg;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") { print_usage(); exit(0); }

        // Symbol & dates
        else if (arg == "--symbol" && i + 1 < argc) cfg.symbol = argv[++i];
        else if (arg == "--start" && i + 1 < argc) cfg.start_date = argv[++i];
        else if (arg == "--end" && i + 1 < argc) cfg.end_date = argv[++i];
        else if (arg == "--balance" && i + 1 < argc) cfg.initial_balance = std::stod(argv[++i]);
        else if (arg == "--strategy" && i + 1 < argc) cfg.strategy = argv[++i];
        else if (arg == "--data" && i + 1 < argc) cfg.data_path = argv[++i];
        else if (arg == "--verbose") cfg.verbose = true;

        // Broker settings
        else if (arg == "--contract-size" && i + 1 < argc) cfg.contract_size = std::stod(argv[++i]);
        else if (arg == "--leverage" && i + 1 < argc) cfg.leverage = std::stod(argv[++i]);
        else if (arg == "--pip-size" && i + 1 < argc) cfg.pip_size = std::stod(argv[++i]);
        else if (arg == "--swap-long" && i + 1 < argc) cfg.swap_long = std::stod(argv[++i]);
        else if (arg == "--swap-short" && i + 1 < argc) cfg.swap_short = std::stod(argv[++i]);

        // FillUp parameters
        else if (arg == "--survive" && i + 1 < argc) cfg.survive_pct = std::stod(argv[++i]);
        else if (arg == "--spacing" && i + 1 < argc) cfg.base_spacing = std::stod(argv[++i]);
        else if (arg == "--mode" && i + 1 < argc) cfg.mode = argv[++i];
        else if (arg == "--lookback" && i + 1 < argc) cfg.lookback_hours = std::stod(argv[++i]);
        else if (arg == "--antifragile" && i + 1 < argc) cfg.antifragile_scale = std::stod(argv[++i]);
        else if (arg == "--velocity" && i + 1 < argc) cfg.velocity_threshold = std::stod(argv[++i]);
        else if (arg == "--max-spacing-mult" && i + 1 < argc) cfg.max_spacing_mult = std::stod(argv[++i]);
        else if (arg == "--pct-spacing") cfg.pct_spacing = true;
        else if (arg == "--force-min-volume") cfg.force_min_volume_entry = true;

        // CombinedJu parameters
        else if (arg == "--tp-mode" && i + 1 < argc) cfg.tp_mode = argv[++i];
        else if (arg == "--sizing-mode" && i + 1 < argc) cfg.sizing_mode = argv[++i];
        else if (arg == "--no-velocity-filter") cfg.enable_velocity_filter = false;

        // Output control
        else if (arg == "--max-equity-samples" && i + 1 < argc) cfg.max_equity_samples = std::stoi(argv[++i]);
        else if (arg == "--max-trades" && i + 1 < argc) cfg.max_trades = std::stoi(argv[++i]);
        else if (arg == "--no-trades") cfg.include_trades = false;
    }

    // Auto-detect data path
    if (cfg.data_path.empty()) {
        cfg.data_path = FindTickDataFile(cfg.symbol);
    }

    // Apply symbol defaults for any settings not overridden
    cfg.apply_symbol_defaults();

    return cfg;
}

// ──────────────────── FillUp Mode Parsing ────────────────────

FillUpOscillation::Mode parse_fillup_mode(const std::string& mode) {
    if (mode == "BASELINE") return FillUpOscillation::BASELINE;
    if (mode == "ADAPTIVE_SPACING") return FillUpOscillation::ADAPTIVE_SPACING;
    if (mode == "ANTIFRAGILE") return FillUpOscillation::ANTIFRAGILE;
    if (mode == "VELOCITY_FILTER") return FillUpOscillation::VELOCITY_FILTER;
    if (mode == "ALL_COMBINED") return FillUpOscillation::ALL_COMBINED;
    return FillUpOscillation::ADAPTIVE_SPACING; // default
}

// ──────────────────── Downsample Equity Curve ────────────────────

template<typename T>
std::vector<T> downsample(const std::vector<T>& data, size_t max_samples) {
    if (data.size() <= max_samples || max_samples == 0) return data;

    std::vector<T> result;
    result.reserve(max_samples);

    double step = (double)(data.size() - 1) / (max_samples - 1);
    for (size_t i = 0; i < max_samples - 1; ++i) {
        result.push_back(data[(size_t)(i * step)]);
    }
    result.push_back(data.back()); // always include last point

    return result;
}

// ──────────────────── Main ────────────────────

int main(int argc, char* argv[]) {
    Config cfg = parse_args(argc, argv);

    // Validate data path
    if (cfg.data_path.empty()) {
        JsonOutput json;
        json.begin_object();
        json.key("status"); json.value_str("error");
        json.key("message"); json.value_str(
            "Could not find tick data file for symbol: " + cfg.symbol +
            ". Set BACKTEST_DATA_DIR or BACKTEST_" + cfg.symbol +
            " env var, or use --data PATH.");
        json.end_object();
        std::cout << json.str() << std::endl;
        return 1;
    }

    if (!fs::exists(cfg.data_path)) {
        JsonOutput json;
        json.begin_object();
        json.key("status"); json.value_str("error");
        json.key("message"); json.value_str("Tick data file not found: " + cfg.data_path);
        json.end_object();
        std::cout << json.str() << std::endl;
        return 1;
    }

    if (cfg.verbose) {
        std::cerr << "Running backtest: " << cfg.symbol << " "
                  << cfg.start_date << " - " << cfg.end_date << std::endl;
        std::cerr << "Data file: " << cfg.data_path << std::endl;
        std::cerr << "Broker: contract_size=" << cfg.contract_size
                  << " leverage=" << cfg.leverage
                  << " pip_size=" << cfg.pip_size << std::endl;
    }

    // Configure tick data
    TickDataConfig tick_config;
    tick_config.file_path = cfg.data_path;
    tick_config.format = TickDataFormat::MT5_CSV;

    // Configure backtest engine
    TickBacktestConfig bt_config;
    bt_config.symbol = cfg.symbol;
    bt_config.initial_balance = cfg.initial_balance;
    bt_config.contract_size = cfg.contract_size;
    bt_config.leverage = cfg.leverage;
    bt_config.pip_size = cfg.pip_size;
    bt_config.swap_long = cfg.swap_long;
    bt_config.swap_short = cfg.swap_short;
    bt_config.swap_mode = 1;
    bt_config.swap_3days = 3;
    bt_config.start_date = cfg.start_date;
    bt_config.end_date = cfg.end_date;
    bt_config.tick_data_config = tick_config;
    bt_config.verbose = cfg.verbose;
    bt_config.track_equity_curve = true;
    bt_config.equity_sample_interval = 1000;

    try {
        TickBasedEngine engine(bt_config);

        // ── Run Strategy ──
        if (cfg.strategy == "fillup" || cfg.strategy == "FillUpOscillation") {
            FillUpOscillation::Config fc;
            fc.survive_pct = cfg.survive_pct;
            fc.base_spacing = cfg.base_spacing;
            fc.min_volume = 0.01;
            fc.max_volume = 10.0;
            fc.contract_size = cfg.contract_size;
            fc.leverage = cfg.leverage;
            fc.mode = parse_fillup_mode(cfg.mode);
            fc.antifragile_scale = cfg.antifragile_scale;
            fc.adaptive.max_spacing_mult = cfg.max_spacing_mult;
            fc.volatility_lookback_hours = cfg.lookback_hours;
            fc.adaptive.pct_spacing = cfg.pct_spacing;
            fc.safety.force_min_volume_entry = cfg.force_min_volume_entry;
            fc.velocity_threshold = cfg.velocity_threshold;

            FillUpOscillation strategy(fc);
            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

        } else if (cfg.strategy == "combined" || cfg.strategy == "CombinedJu") {
            StrategyCombinedJu::Config jc;
            jc.survive_pct = cfg.survive_pct;
            jc.base_spacing = cfg.base_spacing;
            jc.min_volume = 0.01;
            jc.max_volume = 10.0;
            jc.contract_size = cfg.contract_size;
            jc.leverage = cfg.leverage;
            jc.force_min_volume_entry = cfg.force_min_volume_entry;

            // TP mode
            if (cfg.tp_mode == "FIXED") jc.tp_mode = StrategyCombinedJu::TPMode::FIXED;
            else if (cfg.tp_mode == "SQRT") jc.tp_mode = StrategyCombinedJu::TPMode::SQRT;
            else jc.tp_mode = StrategyCombinedJu::TPMode::LINEAR;

            // Sizing mode
            if (cfg.sizing_mode == "LINEAR_SIZING") jc.sizing_mode = StrategyCombinedJu::SizingMode::LINEAR_SIZING;
            else if (cfg.sizing_mode == "THRESHOLD_SIZING") jc.sizing_mode = StrategyCombinedJu::SizingMode::THRESHOLD_SIZING;
            else jc.sizing_mode = StrategyCombinedJu::SizingMode::UNIFORM;

            jc.enable_velocity_filter = cfg.enable_velocity_filter;

            StrategyCombinedJu strategy(jc);
            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

        } else {
            JsonOutput json;
            json.begin_object();
            json.key("status"); json.value_str("error");
            json.key("message"); json.value_str("Unknown strategy: " + cfg.strategy +
                ". Available: fillup, combined");
            json.end_object();
            std::cout << json.str() << std::endl;
            return 1;
        }

        // ── Gather Results ──
        auto results = engine.GetResults();
        const auto& closed_trades = engine.GetClosedTrades();

        // ── Build JSON Output ──
        JsonOutput json;
        json.begin_object();

        json.key("status"); json.value_str("success");
        json.key("symbol"); json.value_str(cfg.symbol);
        json.key("strategy"); json.value_str(cfg.strategy);
        json.key("start_date"); json.value_str(cfg.start_date);
        json.key("end_date"); json.value_str(cfg.end_date);

        // Broker settings used
        json.key("broker_settings");
        json.begin_object();
        json.key("contract_size"); json.value_double(cfg.contract_size);
        json.key("leverage"); json.value_double(cfg.leverage);
        json.key("pip_size"); json.value_double_precise(cfg.pip_size, 4);
        json.key("swap_long"); json.value_double(cfg.swap_long);
        json.key("swap_short"); json.value_double(cfg.swap_short);
        json.end_object();
        json.first = false;

        // Account metrics
        json.key("initial_balance"); json.value_double(cfg.initial_balance);
        json.key("final_balance"); json.value_double(results.final_balance);
        json.key("total_pnl"); json.value_double(results.final_balance - cfg.initial_balance);
        json.key("return_percent"); json.value_double((results.final_balance / cfg.initial_balance - 1.0) * 100.0);

        // Trade stats
        json.key("total_trades"); json.value_size(results.total_trades);
        json.key("total_trades_opened"); json.value_size(results.total_trades_opened);
        json.key("winning_trades"); json.value_size(results.winning_trades);
        json.key("losing_trades"); json.value_size(results.losing_trades);
        double win_rate = results.total_trades > 0
            ? (double)results.winning_trades / results.total_trades * 100.0 : 0.0;
        json.key("win_rate"); json.value_double(win_rate);

        // Risk metrics
        json.key("max_drawdown"); json.value_double(results.max_drawdown);
        json.key("max_drawdown_pct"); json.value_double(results.max_drawdown_pct);
        json.key("sharpe_ratio"); json.value_double(results.sharpe_ratio);
        json.key("sortino_ratio"); json.value_double(results.sortino_ratio);
        json.key("profit_factor"); json.value_double(results.profit_factor);
        json.key("recovery_factor"); json.value_double(results.recovery_factor);

        // Trade metrics
        json.key("average_win"); json.value_double(results.average_win);
        json.key("average_loss"); json.value_double(results.average_loss);
        json.key("largest_win"); json.value_double(results.largest_win);
        json.key("largest_loss"); json.value_double(results.largest_loss);
        json.key("total_swap"); json.value_double(results.total_swap_charged);

        // Peak metrics
        json.key("peak_equity"); json.value_double(results.peak_equity);
        json.key("peak_balance"); json.value_double(results.peak_balance);
        json.key("max_open_positions"); json.value_int(results.max_open_positions);
        json.key("max_used_margin"); json.value_double(results.max_used_margin);
        json.key("stop_out_occurred"); json.value_bool(results.stop_out_occurred);

        // ── Equity Curve (downsampled) ──
        auto eq_curve = downsample(results.equity_curve, (size_t)cfg.max_equity_samples);
        auto eq_timestamps = downsample(results.equity_timestamps, (size_t)cfg.max_equity_samples);

        json.key("equity_curve");
        json.begin_array();
        for (size_t i = 0; i < eq_curve.size(); ++i) {
            if (i > 0) json.ss << ",";
            json.ss << std::fixed << std::setprecision(2) << eq_curve[i];
        }
        if (eq_curve.empty()) {
            json.ss << std::fixed << std::setprecision(2) << cfg.initial_balance << "," << results.final_balance;
        }
        json.end_array();
        json.first = false;

        json.key("equity_timestamps");
        json.begin_array();
        for (size_t i = 0; i < eq_timestamps.size(); ++i) {
            if (i > 0) json.ss << ",";
            json.ss << "\"" << eq_timestamps[i] << "\"";
        }
        json.end_array();
        json.first = false;

        json.key("equity_curve_sampled"); json.value_bool(
            results.equity_curve.size() > (size_t)cfg.max_equity_samples);
        json.key("equity_curve_original_size"); json.value_size(results.equity_curve.size());

        // ── Trade List ──
        if (cfg.include_trades) {
            size_t trade_count = std::min(closed_trades.size(), (size_t)cfg.max_trades);

            json.key("trades");
            json.begin_array();
            for (size_t i = 0; i < trade_count; ++i) {
                const auto& t = closed_trades[i];
                if (i > 0) json.ss << ",";

                json.ss << "{";
                json.ss << "\"id\":" << t.id;
                json.ss << ",\"direction\":\"" << (t.IsBuy() ? "BUY" : "SELL") << "\"";
                json.ss << ",\"entry_price\":" << std::fixed << std::setprecision(5) << t.entry_price;
                json.ss << ",\"exit_price\":" << std::fixed << std::setprecision(5) << t.exit_price;
                json.ss << ",\"entry_time\":\"" << t.entry_time << "\"";
                json.ss << ",\"exit_time\":\"" << t.exit_time << "\"";
                json.ss << ",\"lot_size\":" << std::fixed << std::setprecision(2) << t.lot_size;
                json.ss << ",\"profit_loss\":" << std::fixed << std::setprecision(2) << t.profit_loss;
                json.ss << ",\"commission\":" << std::fixed << std::setprecision(2) << t.commission;
                json.ss << ",\"exit_reason\":\"" << t.exit_reason << "\"";
                json.ss << "}";
            }
            json.end_array();
            json.first = false;

            json.key("trades_total"); json.value_size(closed_trades.size());
            json.key("trades_truncated"); json.value_bool(closed_trades.size() > (size_t)cfg.max_trades);
        }

        json.end_object();
        std::cout << json.str() << std::endl;

    } catch (const std::exception& e) {
        JsonOutput json;
        json.begin_object();
        json.key("status"); json.value_str("error");
        json.key("message"); json.value_str(e.what());
        json.end_object();
        std::cout << json.str() << std::endl;
        return 1;
    }

    return 0;
}
