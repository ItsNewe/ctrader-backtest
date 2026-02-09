/**
 * Test FillUpOscillation on new instruments:
 * - XPTUSD (Platinum) - 24M ticks
 * - XPDUSD (Palladium) - 16M ticks
 * - ETHUSD (Ethereum) - 66M ticks
 *
 * Goal: Find additional profitable instruments using percentage-based spacing
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <thread>
#include <mutex>

using namespace backtest;

struct InstrumentConfig {
    std::string symbol;
    std::string data_file;
    double contract_size;
    double pip_size;
    double swap_long;
    double swap_short;
    std::string start_date;
    std::string end_date;

    // Derived from analysis
    double estimated_hourly_vol_pct;  // For TypicalVolPct
};

struct TestResult {
    std::string symbol;
    double survive_pct;
    double spacing_pct;
    double lookback_hours;
    double typical_vol_pct;

    double initial_balance;
    double final_balance;
    double return_mult;
    double max_dd_pct;
    double swap_total;
    int trade_count;
    std::string status;
};

std::mutex g_mutex;
std::vector<TestResult> g_results;

void TestInstrument(
    const InstrumentConfig& cfg,
    double survive_pct,
    double spacing_pct,
    double lookback_hours,
    double typical_vol_pct)
{
    TickDataConfig tick_config;
    tick_config.file_path = cfg.data_file;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = cfg.symbol;
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = cfg.contract_size;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = cfg.pip_size;
    config.swap_long = cfg.swap_long;
    config.swap_short = cfg.swap_short;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = cfg.start_date;
    config.end_date = cfg.end_date;
    config.tick_data_config = tick_config;

    TestResult result;
    result.symbol = cfg.symbol;
    result.survive_pct = survive_pct;
    result.spacing_pct = spacing_pct;
    result.lookback_hours = lookback_hours;
    result.typical_vol_pct = typical_vol_pct;
    result.initial_balance = 10000.0;

    try {
        TickBasedEngine engine(config);

        // Use percentage-based adaptive spacing
        FillUpOscillation::AdaptiveConfig adaptive_cfg;
        adaptive_cfg.pct_spacing = true;
        adaptive_cfg.typical_vol_pct = typical_vol_pct;
        adaptive_cfg.min_spacing_mult = 0.5;
        adaptive_cfg.max_spacing_mult = 3.0;
        adaptive_cfg.min_spacing_abs = 0.01;  // 0.01% min
        adaptive_cfg.max_spacing_abs = 10.0;  // 10% max
        adaptive_cfg.spacing_change_threshold = 0.01;

        FillUpOscillation strategy(
            survive_pct,
            spacing_pct,  // Now percentage-based
            0.01,         // min_volume
            10.0,         // max_volume
            cfg.contract_size,
            500.0,        // leverage
            FillUpOscillation::ADAPTIVE_SPACING,
            0.0,          // antifragile (unused)
            0.0,          // velocity (unused)
            lookback_hours,  // Passed as constructor param
            adaptive_cfg
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& engine) {
            strategy.OnTick(tick, engine);
        });

        auto results = engine.GetResults();
        result.final_balance = results.final_balance;
        result.return_mult = results.final_balance / result.initial_balance;
        result.max_dd_pct = results.max_drawdown_pct;
        result.swap_total = results.total_swap_charged;
        result.trade_count = results.total_trades;
        result.status = results.stop_out_occurred ? "STOP-OUT" : "OK";

    } catch (const std::exception& e) {
        result.final_balance = 0;
        result.return_mult = 0;
        result.max_dd_pct = 100.0;
        result.swap_total = 0;
        result.trade_count = 0;
        result.status = std::string("ERROR: ") + e.what();
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_results.push_back(result);

        // Print progress
        std::cout << result.symbol << " s" << result.survive_pct
                  << " sp" << result.spacing_pct << "% lb" << result.lookback_hours
                  << " tv" << result.typical_vol_pct
                  << " -> " << std::fixed << std::setprecision(2) << result.return_mult << "x"
                  << " DD=" << result.max_dd_pct << "%"
                  << " [" << result.status << "]" << std::endl;
    }
}

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "====================================================================\n";
    std::cout << "NEW INSTRUMENTS TEST - FillUpOscillation with Percentage Spacing\n";
    std::cout << "====================================================================\n\n";

    // Instrument configurations based on live analysis
    std::vector<InstrumentConfig> instruments = {
        {
            "XPTUSD",  // Platinum
            "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XPTUSD_TICKS_2025.csv",
            100.0,     // contract size
            0.01,      // pip size
            -43.73,    // swap long
            3.11,      // swap short
            "2025.01.01",
            "2025.12.31",
            1.73       // From analysis: 1.728% hourly range
        },
        {
            "XPDUSD",  // Palladium
            "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XPDUSD_TICKS_2025.csv",
            100.0,     // contract size
            0.01,      // pip size
            -17.64,    // swap long
            -11.60,    // swap short
            "2025.01.01",
            "2025.12.31",
            1.85       // From analysis: 1.846% hourly range
        },
        {
            "ETHUSD",  // Ethereum
            "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\ETHUSD_TICKS_2025.csv",
            1.0,       // contract size (1 ETH per lot)
            0.01,      // pip size
            -15.0,     // swap long
            -15.0,     // swap short
            "2025.01.01",
            "2025.12.31",
            0.80       // From analysis: 0.796% hourly range
        }
    };

    // Parameter sweep: survive%, spacing%, lookback, typical_vol%
    std::vector<double> survive_values = {15.0, 18.0, 20.0, 25.0};
    std::vector<double> spacing_values = {1.0, 1.5, 2.0, 3.0};  // Percentage
    std::vector<double> lookback_values = {1.0, 4.0};

    std::cout << "Testing " << instruments.size() << " instruments with "
              << (survive_values.size() * spacing_values.size() * lookback_values.size())
              << " configs each\n\n";

    for (const auto& instr : instruments) {
        std::cout << "\n====================================================================\n";
        std::cout << "Testing " << instr.symbol << "\n";
        std::cout << "  Contract: " << instr.contract_size
                  << " | Swap L/S: " << instr.swap_long << "/" << instr.swap_short << "\n";
        std::cout << "  Est hourly vol: " << instr.estimated_hourly_vol_pct << "%\n";
        std::cout << "====================================================================\n";

        for (double survive : survive_values) {
            for (double spacing : spacing_values) {
                for (double lb : lookback_values) {
                    // Use measured hourly vol scaled by lookback
                    double tv = (lb == 1.0) ? instr.estimated_hourly_vol_pct
                                            : instr.estimated_hourly_vol_pct * 2.0;  // 4h approx
                    TestInstrument(instr, survive, spacing, lb, tv);
                }
            }
        }
    }

    // Print summary sorted by return
    std::cout << "\n\n====================================================================\n";
    std::cout << "RESULTS SUMMARY (sorted by return)\n";
    std::cout << "====================================================================\n\n";

    std::sort(g_results.begin(), g_results.end(),
        [](const TestResult& a, const TestResult& b) {
            return a.return_mult > b.return_mult;
        });

    std::cout << std::left
              << std::setw(10) << "Symbol"
              << std::setw(8) << "Surv%"
              << std::setw(8) << "Sp%"
              << std::setw(6) << "LB"
              << std::setw(8) << "TV%"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD"
              << std::setw(10) << "Swap"
              << std::setw(10) << "Trades"
              << "Status\n";
    std::cout << std::string(90, '-') << "\n";

    for (const auto& r : g_results) {
        std::cout << std::left
                  << std::setw(10) << r.symbol
                  << std::setw(8) << r.survive_pct
                  << std::setw(8) << r.spacing_pct
                  << std::setw(6) << r.lookback_hours
                  << std::setw(8) << r.typical_vol_pct
                  << std::setw(10) << (std::to_string((int)(r.return_mult * 100) / 100.0) + "x")
                  << std::setw(10) << (std::to_string((int)r.max_dd_pct) + "%")
                  << std::setw(10) << (int)r.swap_total
                  << std::setw(10) << r.trade_count
                  << r.status << "\n";
    }

    // Best per instrument
    std::cout << "\n====================================================================\n";
    std::cout << "BEST CONFIG PER INSTRUMENT\n";
    std::cout << "====================================================================\n";

    std::map<std::string, TestResult> best_per_instr;
    for (const auto& r : g_results) {
        if (r.status == "OK") {
            if (best_per_instr.find(r.symbol) == best_per_instr.end() ||
                r.return_mult > best_per_instr[r.symbol].return_mult) {
                best_per_instr[r.symbol] = r;
            }
        }
    }

    for (const auto& [symbol, r] : best_per_instr) {
        std::cout << "\n" << symbol << ":\n";
        std::cout << "  Config: survive=" << r.survive_pct << "%, spacing=" << r.spacing_pct
                  << "%, lookback=" << r.lookback_hours << "h, typvol=" << r.typical_vol_pct << "%\n";
        std::cout << "  Return: " << r.return_mult << "x\n";
        std::cout << "  Max DD: " << r.max_dd_pct << "%\n";
        std::cout << "  Swap:   $" << r.swap_total << "\n";
        std::cout << "  Trades: " << r.trade_count << "\n";
    }

    // Comparison with XAUUSD baseline
    std::cout << "\n====================================================================\n";
    std::cout << "COMPARISON WITH XAUUSD BASELINE\n";
    std::cout << "====================================================================\n";
    std::cout << "XAUUSD (2025): 6.57x return, 67% DD, ~10k trades\n";
    std::cout << "XAGUSD (2025): 43.4x return, 29% DD with pct_spacing\n";

    return 0;
}
