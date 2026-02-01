/**
 * Backtest CLI - Command-line interface for running backtests
 * Outputs JSON results for Flask server integration
 *
 * Usage: backtest_cli.exe --config config.json
 * Or:    backtest_cli.exe --symbol XAUUSD --start 2025.01.01 --end 2025.01.15 --strategy fillup
 *
 * Build: g++ -O3 -fno-inline -mavx2 -mfma -std=c++17 -static -I include src/backtest_cli.cpp -o build/backtest_cli.exe
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

using namespace backtest;

// Simple JSON output helper
class JsonOutput {
public:
    std::ostringstream ss;
    bool first = true;

    void begin_object() { ss << "{"; first = true; }
    void end_object() { ss << "}"; }
    void begin_array() { ss << "["; first = true; }
    void end_array() { ss << "]"; }

    void key(const std::string& k) {
        if (!first) ss << ",";
        ss << "\"" << k << "\":";
        first = false;
    }

    void value(const std::string& v) { ss << "\"" << v << "\""; }
    void value(double v) { ss << std::fixed << std::setprecision(2) << v; }
    void value(int v) { ss << v; }
    void value(size_t v) { ss << v; }
    void value(bool v) { ss << (v ? "true" : "false"); }

    std::string str() { return ss.str(); }
};

void print_usage() {
    std::cerr << "Backtest CLI - High-performance strategy backtester\n\n";
    std::cerr << "Usage:\n";
    std::cerr << "  backtest_cli.exe [options]\n\n";
    std::cerr << "Options:\n";
    std::cerr << "  --symbol SYMBOL       Trading symbol (default: XAUUSD)\n";
    std::cerr << "  --start DATE          Start date (YYYY.MM.DD, default: 2025.01.01)\n";
    std::cerr << "  --end DATE            End date (YYYY.MM.DD, default: 2025.01.31)\n";
    std::cerr << "  --balance AMOUNT      Initial balance (default: 10000)\n";
    std::cerr << "  --strategy NAME       Strategy: fillup, combined (default: fillup)\n";
    std::cerr << "  --data PATH           Tick data file path\n";
    std::cerr << "  --survive PCT         Survive percentage for FillUp (default: 13.0)\n";
    std::cerr << "  --spacing AMOUNT      Base spacing for FillUp (default: 1.5)\n";
    std::cerr << "  --json                Output results as JSON (default)\n";
    std::cerr << "  --verbose             Verbose output during backtest\n";
    std::cerr << "  --help                Show this help\n";
}

struct Config {
    std::string symbol = "XAUUSD";
    std::string start_date = "2024.12.31";  // Note: tick data starts 2024.12.31
    std::string end_date = "2025.01.31";
    double initial_balance = 10000.0;
    std::string strategy = "fillup";
    std::string data_path = "";
    double survive_pct = 13.0;
    double base_spacing = 1.5;
    bool json_output = true;
    bool verbose = false;

    // Broker settings (defaults for XAUUSD)
    double contract_size = 100.0;
    double leverage = 500.0;
    double pip_size = 0.01;
    double swap_long = -66.99;
    double swap_short = 41.2;
};

Config parse_args(int argc, char* argv[]) {
    Config config;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage();
            exit(0);
        } else if (arg == "--symbol" && i + 1 < argc) {
            config.symbol = argv[++i];
        } else if (arg == "--start" && i + 1 < argc) {
            config.start_date = argv[++i];
        } else if (arg == "--end" && i + 1 < argc) {
            config.end_date = argv[++i];
        } else if (arg == "--balance" && i + 1 < argc) {
            config.initial_balance = std::stod(argv[++i]);
        } else if (arg == "--strategy" && i + 1 < argc) {
            config.strategy = argv[++i];
        } else if (arg == "--data" && i + 1 < argc) {
            config.data_path = argv[++i];
        } else if (arg == "--survive" && i + 1 < argc) {
            config.survive_pct = std::stod(argv[++i]);
        } else if (arg == "--spacing" && i + 1 < argc) {
            config.base_spacing = std::stod(argv[++i]);
        } else if (arg == "--json") {
            config.json_output = true;
        } else if (arg == "--verbose") {
            config.verbose = true;
        }
    }

    // Default data path based on symbol
    if (config.data_path.empty()) {
        if (config.symbol == "XAUUSD") {
            config.data_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
        } else if (config.symbol == "XAGUSD") {
            config.data_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAGUSD_TESTER_TICKS.csv";
            config.contract_size = 5000.0;
            config.pip_size = 0.001;
            config.swap_long = -15.0;
            config.swap_short = 13.72;
        }
    }

    return config;
}

int main(int argc, char* argv[]) {
    Config cfg = parse_args(argc, argv);

    if (!cfg.json_output) {
        std::cerr << "Running backtest: " << cfg.symbol << " " << cfg.start_date << " - " << cfg.end_date << std::endl;
    }

    // Configure tick data
    TickDataConfig tick_config;
    tick_config.file_path = cfg.data_path;
    tick_config.format = TickDataFormat::MT5_CSV;

    // Configure backtest
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

    // Enable equity curve tracking for metrics
    bt_config.track_equity_curve = true;
    bt_config.equity_sample_interval = 1000;  // Sample every 1000 ticks

    try {
        TickBasedEngine engine(bt_config);

        // Run with appropriate strategy
        if (cfg.strategy == "fillup" || cfg.strategy == "FillUpOscillation") {
            FillUpOscillation strategy(
                cfg.survive_pct,
                cfg.base_spacing,
                0.01,   // min_volume
                10.0,   // max_volume
                cfg.contract_size,
                cfg.leverage,
                FillUpOscillation::ADAPTIVE_SPACING,
                0.1,    // antifragile_scale
                30.0,   // max_spacing_mult
                4.0     // lookback_hours
            );

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
        } else if (cfg.strategy == "combined" || cfg.strategy == "CombinedJu") {
            StrategyCombinedJu::Config ju_config;
            ju_config.survive_pct = cfg.survive_pct;
            ju_config.base_spacing = cfg.base_spacing;
            ju_config.min_volume = 0.01;
            ju_config.max_volume = 10.0;
            ju_config.contract_size = cfg.contract_size;
            ju_config.leverage = cfg.leverage;

            StrategyCombinedJu strategy(ju_config);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
        } else {
            std::cerr << "Unknown strategy: " << cfg.strategy << std::endl;
            return 1;
        }

        auto results = engine.GetResults();

        // Output JSON
        JsonOutput json;
        json.begin_object();

        json.key("status"); json.value("success");
        json.key("symbol"); json.value(cfg.symbol);
        json.key("strategy"); json.value(cfg.strategy);
        json.key("start_date"); json.value(cfg.start_date);
        json.key("end_date"); json.value(cfg.end_date);

        json.key("initial_balance"); json.value(cfg.initial_balance);
        json.key("final_balance"); json.value(results.final_balance);
        json.key("total_pnl"); json.value(results.final_balance - cfg.initial_balance);
        json.key("return_percent"); json.value((results.final_balance / cfg.initial_balance - 1.0) * 100.0);

        json.key("total_trades"); json.value(results.total_trades);
        json.key("winning_trades"); json.value(results.winning_trades);
        json.key("losing_trades"); json.value(results.losing_trades);

        double win_rate = results.total_trades > 0
            ? (double)results.winning_trades / results.total_trades * 100.0 : 0.0;
        json.key("win_rate"); json.value(win_rate);

        json.key("max_drawdown"); json.value(results.max_drawdown_pct);

        // Calculate profit factor
        double profit_factor = results.average_loss != 0.0
            ? std::abs(results.average_win * results.winning_trades) / std::abs(results.average_loss * results.losing_trades)
            : 0.0;
        json.key("profit_factor"); json.value(profit_factor);

        // Risk metrics from engine (actual calculated values)
        json.key("sharpe_ratio"); json.value(results.sharpe_ratio);
        json.key("sortino_ratio"); json.value(results.sortino_ratio);
        json.key("profit_factor_calc"); json.value(results.profit_factor);
        json.key("recovery_factor"); json.value(results.recovery_factor);

        json.key("total_swap"); json.value(results.total_swap_charged);

        json.key("average_win"); json.value(results.average_win);
        json.key("average_loss"); json.value(results.average_loss);
        json.key("largest_win"); json.value(results.largest_win);
        json.key("largest_loss"); json.value(results.largest_loss);

        // Equity curve from engine (actual tracked values)
        json.key("equity_curve");
        json.begin_array();
        json.first = true;  // Reset for array elements
        for (size_t i = 0; i < results.equity_curve.size(); ++i) {
            if (i > 0) json.ss << ",";
            json.value(results.equity_curve[i]);
        }
        // If no equity curve, at least provide start/end
        if (results.equity_curve.empty()) {
            json.value(cfg.initial_balance);
            json.ss << ",";
            json.value(results.final_balance);
        }
        json.end_array();
        json.first = false;  // Reset for next key

        // Equity timestamps
        json.key("equity_timestamps");
        json.begin_array();
        json.first = true;
        for (size_t i = 0; i < results.equity_timestamps.size(); ++i) {
            if (i > 0) json.ss << ",";
            json.value(results.equity_timestamps[i]);
        }
        json.end_array();
        json.first = false;

        json.end_object();

        std::cout << json.str() << std::endl;

    } catch (const std::exception& e) {
        JsonOutput json;
        json.begin_object();
        json.key("status"); json.value("error");
        json.key("message"); json.value(e.what());
        json.end_object();
        std::cout << json.str() << std::endl;
        return 1;
    }

    return 0;
}
