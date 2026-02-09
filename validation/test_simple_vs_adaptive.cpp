/**
 * Quick test: FIXED vs ADAPTIVE spacing on 2025 gold
 * Does volatility adaptation add any value?
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>

using namespace backtest;

struct Result {
    std::string name;
    double return_mult;
    double max_dd;
    int trades;
};

int main() {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "====================================================================\n";
    std::cout << "SIMPLICITY TEST: FIXED vs ADAPTIVE on XAUUSD 2025\n";
    std::cout << "====================================================================\n\n";

    std::string data_2025 = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

    TickDataConfig tick_config;
    tick_config.file_path = data_2025;
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.01;
    config.swap_long = -66.99;
    config.swap_short = 41.2;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2025.12.30";
    config.tick_data_config = tick_config;

    std::vector<Result> results;

    // Test 1: FIXED $1.50 spacing (matches ADAPTIVE base)
    {
        std::cout << "Testing FIXED $1.50 spacing... " << std::flush;
        TickBasedEngine engine(config);
        FillUpOscillation strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::BASELINE, 0.0, 0.0, 4.0);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        results.push_back({"FIXED_$1.50", res.final_balance / 10000.0,
                          res.max_drawdown_pct, res.total_trades});
        std::cout << results.back().return_mult << "x DD=" << results.back().max_dd
                  << "% trades=" << results.back().trades << "\n";
    }

    // Test 2: FIXED $1.00 spacing (tighter)
    {
        std::cout << "Testing FIXED $1.00 spacing... " << std::flush;
        TickBasedEngine engine(config);
        FillUpOscillation strategy(13.0, 1.0, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::BASELINE, 0.0, 0.0, 4.0);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        results.push_back({"FIXED_$1.00", res.final_balance / 10000.0,
                          res.max_drawdown_pct, res.total_trades});
        std::cout << results.back().return_mult << "x DD=" << results.back().max_dd
                  << "% trades=" << results.back().trades << "\n";
    }

    // Test 3: FIXED $2.00 spacing (wider)
    {
        std::cout << "Testing FIXED $2.00 spacing... " << std::flush;
        TickBasedEngine engine(config);
        FillUpOscillation strategy(13.0, 2.0, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::BASELINE, 0.0, 0.0, 4.0);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        results.push_back({"FIXED_$2.00", res.final_balance / 10000.0,
                          res.max_drawdown_pct, res.total_trades});
        std::cout << results.back().return_mult << "x DD=" << results.back().max_dd
                  << "% trades=" << results.back().trades << "\n";
    }

    // Test 4: ADAPTIVE (current V4)
    {
        std::cout << "Testing ADAPTIVE $1.50 base... " << std::flush;
        TickBasedEngine engine(config);

        FillUpOscillation::AdaptiveConfig adaptive_cfg;
        adaptive_cfg.typical_vol_pct = 0.55;
        adaptive_cfg.min_spacing_mult = 0.5;
        adaptive_cfg.max_spacing_mult = 3.0;

        FillUpOscillation strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0,
            FillUpOscillation::ADAPTIVE_SPACING, 0.0, 0.0, 4.0, adaptive_cfg);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        results.push_back({"ADAPTIVE", res.final_balance / 10000.0,
                          res.max_drawdown_pct, res.total_trades});
        std::cout << results.back().return_mult << "x DD=" << results.back().max_dd
                  << "% trades=" << results.back().trades << "\n";
    }

    // Summary
    std::cout << "\n====================================================================\n";
    std::cout << "SUMMARY - Does ADAPTIVE beat FIXED?\n";
    std::cout << "====================================================================\n\n";

    std::cout << std::left << std::setw(18) << "Strategy"
              << std::setw(12) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(12) << "Trades"
              << "Params\n";
    std::cout << std::string(60, '-') << "\n";

    for (const auto& r : results) {
        int params = (r.name == "ADAPTIVE") ? 6 : 2;
        std::cout << std::setw(18) << r.name
                  << std::setw(12) << (std::to_string(r.return_mult).substr(0,5) + "x")
                  << std::setw(10) << (std::to_string(r.max_dd).substr(0,5) + "%")
                  << std::setw(12) << r.trades
                  << params << "\n";
    }

    std::cout << "\n====================================================================\n";
    std::cout << "CONCLUSION\n";
    std::cout << "====================================================================\n";

    double adaptive_return = results[3].return_mult;
    double best_fixed = std::max({results[0].return_mult, results[1].return_mult, results[2].return_mult});

    if (adaptive_return > best_fixed * 1.1) {
        std::cout << "ADAPTIVE wins by >10% - complexity justified\n";
    } else if (best_fixed > adaptive_return * 1.1) {
        std::cout << "FIXED wins by >10% - simpler is better!\n";
    } else {
        std::cout << "No clear winner - FIXED and ADAPTIVE are comparable\n";
        std::cout << "Simpler (FIXED) may be preferable given similar performance\n";
    }

    return 0;
}
