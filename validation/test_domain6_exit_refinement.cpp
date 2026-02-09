/**
 * Domain 6: Exit Strategy Refinement
 *
 * Test alternative exit methods:
 * - Current: Fixed TP (spread + spacing)
 * - Dynamic TP based on recent oscillation amplitude
 * - Trailing TP
 * - Time-based exit for stuck positions
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <deque>
#include <map>
#include <cfloat>

using namespace backtest;

struct ExitTestResult {
    std::string name;
    double final_balance;
    double return_x;
    double max_dd_pct;
    int total_trades;
    int time_exits;
};

ExitTestResult TestFixedTP() {
    // This uses the standard FillUpOscillation with fixed TP
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
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

    ExitTestResult result;
    result.name = "Fixed TP";
    result.time_exits = 0;

    try {
        TickBasedEngine engine(config);

        FillUpOscillation strategy(13.0, 1.0, 0.01, 10.0, 100.0, 500.0,
                                    FillUpOscillation::ADAPTIVE_SPACING,
                                    0.1, 30.0, 1.0);

        double peak = config.initial_balance;
        double max_dd = 0;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
            double eq = eng.GetEquity();
            if (eq > peak) peak = eq;
            double dd = (peak - eq) / peak * 100;
            if (dd > max_dd) max_dd = dd;
        });

        auto res = engine.GetResults();
        result.final_balance = res.final_balance;
        result.return_x = res.final_balance / config.initial_balance;
        result.max_dd_pct = max_dd;
        result.total_trades = res.total_trades;
    } catch (...) {
        result.return_x = 0;
    }

    return result;
}

ExitTestResult TestDynamicTP() {
    // Dynamic TP: Adjust based on recent volatility
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
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

    ExitTestResult result;
    result.name = "Dynamic TP";
    result.time_exits = 0;

    try {
        TickBasedEngine engine(config);

        // Track recent amplitude for dynamic TP
        std::deque<double> recent_prices;
        double recent_high = 0, recent_low = DBL_MAX;
        double spacing = 1.0;
        double lowest_buy = DBL_MAX, highest_buy = -DBL_MAX;
        long tick_count = 0;

        double peak = config.initial_balance;
        double max_dd = 0;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            tick_count++;

            // Track recent volatility
            recent_prices.push_back(tick.bid);
            if (recent_prices.size() > 720000) recent_prices.pop_front();  // 1 hour

            if (tick_count % 72000 == 0) {  // Update every ~6 min
                recent_high = 0;
                recent_low = DBL_MAX;
                for (double p : recent_prices) {
                    recent_high = std::max(recent_high, p);
                    recent_low = std::min(recent_low, p);
                }
            }

            // Calculate dynamic TP based on recent range
            double range = recent_high - recent_low;
            double dynamic_tp_offset = std::max(0.5, std::min(3.0, range / 5.0));

            // Update tracking
            lowest_buy = DBL_MAX;
            highest_buy = -DBL_MAX;
            for (const Trade* t : eng.GetOpenPositions()) {
                lowest_buy = std::min(lowest_buy, t->entry_price);
                highest_buy = std::max(highest_buy, t->entry_price);
            }

            int pos_count = eng.GetOpenPositions().size();
            bool should_open = pos_count == 0 ||
                               lowest_buy >= tick.ask + spacing ||
                               highest_buy <= tick.ask - spacing;

            if (should_open) {
                double tp = tick.ask + tick.spread() + dynamic_tp_offset;
                eng.OpenMarketOrder("BUY", 0.01, 0.0, tp);
            }

            double eq = eng.GetEquity();
            if (eq > peak) peak = eq;
            double dd = (peak - eq) / peak * 100;
            if (dd > max_dd) max_dd = dd;
        });

        auto res = engine.GetResults();
        result.final_balance = res.final_balance;
        result.return_x = res.final_balance / config.initial_balance;
        result.max_dd_pct = max_dd;
        result.total_trades = res.total_trades;
    } catch (...) {
        result.return_x = 0;
    }

    return result;
}

ExitTestResult TestTimeBasedExit() {
    // Close positions that are open too long (>4 hours)
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
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

    ExitTestResult result;
    result.name = "Time-Based Exit (4h)";
    result.time_exits = 0;

    try {
        TickBasedEngine engine(config);

        double spacing = 1.0;
        double lowest_buy = DBL_MAX, highest_buy = -DBL_MAX;
        long tick_count = 0;
        std::map<const Trade*, long> position_open_tick;
        long max_ticks = 4 * 720000;  // 4 hours

        double peak = config.initial_balance;
        double max_dd = 0;

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            tick_count++;

            // Track when positions were opened
            for (const Trade* t : eng.GetOpenPositions()) {
                if (position_open_tick.find(t) == position_open_tick.end()) {
                    position_open_tick[t] = tick_count;
                }
            }

            // Check for time-based exits
            for (const Trade* t : eng.GetOpenPositions()) {
                if (tick_count - position_open_tick[t] > max_ticks) {
                    // Close this position manually (at current price)
                    eng.ClosePosition(const_cast<Trade*>(t), "TIME_EXIT");
                    result.time_exits++;
                }
            }

            // Clean up closed positions from tracking
            std::vector<const Trade*> to_remove;
            for (auto& [t, tick] : position_open_tick) {
                bool found = false;
                for (const Trade* open : eng.GetOpenPositions()) {
                    if (open == t) { found = true; break; }
                }
                if (!found) to_remove.push_back(t);
            }
            for (auto t : to_remove) position_open_tick.erase(t);

            // Standard entry logic
            lowest_buy = DBL_MAX;
            highest_buy = -DBL_MAX;
            for (const Trade* t : eng.GetOpenPositions()) {
                lowest_buy = std::min(lowest_buy, t->entry_price);
                highest_buy = std::max(highest_buy, t->entry_price);
            }

            int pos_count = eng.GetOpenPositions().size();
            bool should_open = pos_count == 0 ||
                               lowest_buy >= tick.ask + spacing ||
                               highest_buy <= tick.ask - spacing;

            if (should_open) {
                double tp = tick.ask + tick.spread() + spacing;
                eng.OpenMarketOrder("BUY", 0.01, 0.0, tp);
            }

            double eq = eng.GetEquity();
            if (eq > peak) peak = eq;
            double dd = (peak - eq) / peak * 100;
            if (dd > max_dd) max_dd = dd;
        });

        auto res = engine.GetResults();
        result.final_balance = res.final_balance;
        result.return_x = res.final_balance / config.initial_balance;
        result.max_dd_pct = max_dd;
        result.total_trades = res.total_trades;
    } catch (...) {
        result.return_x = 0;
    }

    return result;
}

int main() {
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "EXIT STRATEGY REFINEMENT" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    std::vector<ExitTestResult> results;

    std::cout << "\n[1/3] Testing Fixed TP (baseline)..." << std::endl;
    results.push_back(TestFixedTP());
    std::cout << "  Return: " << results.back().return_x << "x" << std::endl;

    std::cout << "\n[2/3] Testing Dynamic TP..." << std::endl;
    results.push_back(TestDynamicTP());
    std::cout << "  Return: " << results.back().return_x << "x" << std::endl;

    std::cout << "\n[3/3] Testing Time-Based Exit..." << std::endl;
    results.push_back(TestTimeBasedExit());
    std::cout << "  Return: " << results.back().return_x << "x, Time exits: " << results.back().time_exits << std::endl;

    // Summary
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "EXIT REFINEMENT RESULTS" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << std::setw(22) << "Strategy"
              << std::setw(14) << "Final $"
              << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(10) << "Trades" << std::endl;
    std::cout << std::string(66, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::setw(22) << r.name
                  << std::setw(13) << "$" << r.final_balance
                  << std::setw(9) << r.return_x << "x"
                  << std::setw(9) << r.max_dd_pct << "%"
                  << std::setw(10) << r.total_trades << std::endl;
    }

    std::cout << "\n=== ANALYSIS ===" << std::endl;
    double baseline = results[0].return_x;
    for (size_t i = 1; i < results.size(); i++) {
        double diff = results[i].return_x - baseline;
        std::cout << results[i].name << ": " << (diff > 0 ? "+" : "") << diff << "x vs baseline" << std::endl;
    }

    return 0;
}
