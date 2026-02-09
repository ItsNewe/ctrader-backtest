#include "../include/fill_up_strategy_v5.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace backtest;

struct Result {
    std::string name;
    std::string start;
    double ret;
    double max_dd;
    int trades;
    bool stopped_out;
};

Result RunTest(const std::string& name, double survive, double spacing, int ma_period,
               const std::string& start_date, const std::vector<Tick>& ticks) {
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
    config.start_date = start_date;
    config.end_date = "2025.12.29";

    FillUpStrategyV5::Config sc;
    sc.survive_pct = survive;
    sc.spacing = spacing;
    sc.min_volume = 0.01;
    sc.max_volume = 10.0;
    sc.contract_size = 100.0;
    sc.leverage = 500.0;
    sc.ma_period = ma_period;
    sc.tp_multiplier = 1.0;
    sc.stop_new_at_dd = 5.0;
    sc.partial_close_at_dd = 8.0;
    sc.close_all_at_dd = 25.0;
    sc.max_positions = 50;
    sc.reduce_size_at_dd = 3.0;

    FillUpStrategyV5 strategy(sc);
    TickBasedEngine engine(config);

    engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
        strategy.OnTick(tick, eng);
    });

    auto results = engine.GetResults();

    Result r;
    r.name = name;
    r.start = start_date;
    r.ret = results.final_balance / 10000.0;
    r.max_dd = results.max_drawdown_pct;
    r.trades = results.total_trades;
    r.stopped_out = results.final_balance < 1000.0;

    return r;
}

int main() {
    std::cout << "=== V5 (SMA Filter) Performance Test ===" << std::endl;
    std::cout << std::endl;

    // Load tick data
    std::cout << "Loading tick data..." << std::endl;
    TickDataConfig tick_config;
    tick_config.file_path = "C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;

    TickDataManager manager(tick_config);
    std::vector<Tick> all_ticks;
    Tick tick;
    while (manager.GetNextTick(tick)) {
        all_ticks.push_back(tick);
    }
    std::cout << "Loaded " << all_ticks.size() << " ticks" << std::endl;
    std::cout << std::endl;

    std::vector<Result> results;

    // Test configurations
    struct TestCfg {
        std::string name;
        double survive;
        double spacing;
        int ma;
    };

    std::vector<TestCfg> configs = {
        {"V5_s13_sp1.5_ma11k", 13.0, 1.5, 11000},
        {"V5_s13_sp1.5_ma5k", 13.0, 1.5, 5000},
        {"V5_s15_sp2.0_ma11k", 15.0, 2.0, 11000},
        {"V5_s18_sp2.0_ma11k", 18.0, 2.0, 11000},
        {"V5_s20_sp2.5_ma11k", 20.0, 2.5, 11000},
        {"V5_s25_sp3.0_ma11k", 25.0, 3.0, 11000},
    };

    std::vector<std::string> starts = {"2025.01.01", "2025.10.17"};

    std::cout << std::fixed << std::setprecision(2);

    for (const auto& start : starts) {
        std::cout << "--- Testing from " << start << " ---" << std::endl;
        for (const auto& cfg : configs) {
            Result r = RunTest(cfg.name, cfg.survive, cfg.spacing, cfg.ma, start, all_ticks);
            results.push_back(r);
            double ret_dd = (r.max_dd > 0) ? (r.ret / (r.max_dd / 100.0)) : 0;
            std::cout << r.name << " | " << r.ret << "x | " << r.max_dd << "% DD | "
                      << r.trades << " trades | Ret/DD: " << ret_dd
                      << (r.stopped_out ? " | STOP-OUT" : "") << std::endl;
        }
        std::cout << std::endl;
    }

    // Summary comparison
    std::cout << "=== V5 vs CombinedJu COMPARISON ===" << std::endl;
    std::cout << std::endl;
    std::cout << "V5 (SMA Filter) Results:" << std::endl;
    std::cout << "Config             | Jan Return | Jan DD | Oct Return | Oct DD" << std::endl;
    std::cout << "-------------------|------------|--------|------------|-------" << std::endl;

    for (const auto& cfg : configs) {
        Result* jan = nullptr;
        Result* oct = nullptr;
        for (auto& r : results) {
            if (r.name == cfg.name) {
                if (r.start == "2025.01.01") jan = &r;
                if (r.start == "2025.10.17") oct = &r;
            }
        }
        if (jan && oct) {
            std::cout << std::left << std::setw(18) << cfg.name << " | "
                      << std::right << std::setw(9) << jan->ret << "x | "
                      << std::setw(5) << jan->max_dd << "% | "
                      << std::setw(9) << oct->ret << "x | "
                      << std::setw(5) << oct->max_dd << "%" << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << "CombinedJu (from earlier tests):" << std::endl;
    std::cout << "s13_sp1.5_UNI      |     13.05x | 66.55% |      1.33x | 86.81%" << std::endl;
    std::cout << "s18_sp2.0_UNI      |      4.31x | 59.31% |      1.24x | 62.69%" << std::endl;
    std::cout << "s25_sp3.0_UNI      |      3.10x | 44.45% |      1.17x | 45.80%" << std::endl;

    return 0;
}
