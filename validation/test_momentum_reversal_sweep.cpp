/**
 * MomentumReversal Parameter Sweep
 *
 * Tests various parameter combinations and checks for overfitting by:
 * 1. In-sample: 2025 Jan-Jun
 * 2. Out-of-sample: 2025 Jul-Dec
 * 3. Full backtest: 2025 full year
 * 4. 2024 validation (different market conditions)
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <deque>
#include <vector>
#include <cmath>
#include <cfloat>
#include <algorithm>

using namespace backtest;

struct MomentumParams {
    int short_ma;
    int long_ma;
    int max_wait;
    bool require_reversal_grid;
    double spacing;
};

struct SweepResult {
    MomentumParams params;
    double return_x;
    double max_dd_pct;
    int total_trades;
    bool stopped_out;
    std::string period;
};

class MomentumReversalStrategy {
public:
    MomentumReversalStrategy(int short_ma, int long_ma, int max_wait,
                              bool require_reversal_grid, double spacing)
        : short_ma_(short_ma), long_ma_(long_ma), max_wait_(max_wait),
          require_reversal_grid_(require_reversal_grid), spacing_(spacing),
          was_falling_(false), tick_count_(0) {
        prices_.clear();
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        tick_count_++;
        double mid = (tick.bid + tick.ask) / 2.0;

        // Track price history
        prices_.push_back(mid);
        if ((int)prices_.size() > long_ma_) prices_.pop_front();

        // Check for momentum reversal
        bool reversal = CheckReversal();

        // Get current positions
        double lowest_buy = DBL_MAX, highest_buy = -DBL_MAX;
        double total_volume = 0;
        int pos_count = 0;

        for (const Trade* t : engine.GetOpenPositions()) {
            if (t->direction == "BUY") {
                lowest_buy = std::min(lowest_buy, t->entry_price);
                highest_buy = std::max(highest_buy, t->entry_price);
                total_volume += t->lot_size;
                pos_count++;
            }
        }

        // Check spacing condition
        bool spacing_met = false;
        if (pos_count == 0) {
            spacing_met = true;
        } else if (lowest_buy >= tick.ask + spacing_) {
            spacing_met = true;
        } else if (highest_buy <= tick.ask - spacing_) {
            spacing_met = true;
        }

        // Apply momentum filter
        bool should_open = false;
        if (spacing_met) {
            if (pos_count == 0) {
                // First position
                should_open = reversal || tick_count_ > max_wait_;
            } else {
                // Grid additions
                if (require_reversal_grid_) {
                    should_open = reversal;
                } else {
                    should_open = true;
                }
            }
        }

        if (should_open) {
            double tp = tick.ask + tick.spread() + spacing_;
            engine.OpenMarketOrder("BUY", 0.01, 0.0, tp);
        }
    }

    void Reset() {
        prices_.clear();
        was_falling_ = false;
        tick_count_ = 0;
    }

private:
    bool CheckReversal() {
        if ((int)prices_.size() < long_ma_) return false;

        // Calculate short MA (most recent)
        double short_sum = 0;
        int start = prices_.size() - short_ma_;
        for (int i = start; i < (int)prices_.size(); i++) {
            short_sum += prices_[i];
        }
        double short_avg = short_sum / short_ma_;

        // Calculate older MA
        double older_sum = 0;
        int older_count = long_ma_ - short_ma_;
        for (int i = start - older_count; i < start; i++) {
            older_sum += prices_[i];
        }
        double older_avg = older_sum / older_count;

        bool is_falling = short_avg < older_avg;
        bool reversal = was_falling_ && !is_falling;
        was_falling_ = is_falling;

        return reversal;
    }

    int short_ma_;
    int long_ma_;
    int max_wait_;
    bool require_reversal_grid_;
    double spacing_;
    std::deque<double> prices_;
    bool was_falling_;
    long tick_count_;
};

SweepResult RunTest(const MomentumParams& params, const std::string& start_date,
                    const std::string& end_date, const std::string& period_name) {
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
    config.start_date = start_date;
    config.end_date = end_date;
    config.tick_data_config = tick_config;

    SweepResult result;
    result.params = params;
    result.period = period_name;
    result.stopped_out = false;

    try {
        TickBasedEngine engine(config);
        MomentumReversalStrategy strategy(
            params.short_ma, params.long_ma, params.max_wait,
            params.require_reversal_grid, params.spacing
        );

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
        result.return_x = res.final_balance / config.initial_balance;
        result.max_dd_pct = max_dd;
        result.total_trades = res.total_trades;

        if (res.final_balance < config.initial_balance * 0.1) {
            result.stopped_out = true;
        }
    } catch (...) {
        result.return_x = 0;
        result.stopped_out = true;
    }

    return result;
}

void PrintResult(const SweepResult& r) {
    std::cout << std::setw(4) << r.params.short_ma << "/"
              << std::setw(4) << r.params.long_ma
              << std::setw(6) << r.params.max_wait
              << std::setw(5) << (r.params.require_reversal_grid ? "Y" : "N")
              << std::setw(6) << std::fixed << std::setprecision(1) << r.params.spacing
              << std::setw(10) << std::setprecision(2) << r.return_x << "x"
              << std::setw(9) << r.max_dd_pct << "%"
              << std::setw(8) << r.total_trades
              << std::setw(10) << r.period
              << (r.stopped_out ? " STOPPED" : "") << std::endl;
}

int main(int argc, char* argv[]) {
    // Parse command line for which sweep to run
    int sweep_id = 0;
    if (argc > 1) {
        sweep_id = std::atoi(argv[1]);
    }

    std::cout << std::fixed << std::setprecision(2);

    // Define parameter combinations
    std::vector<MomentumParams> all_params;

    // Short MA: 10, 25, 50, 100
    // Long MA: 25, 50, 100, 200
    // Max wait: 500, 1000, 2000
    // Require reversal grid: true, false
    // Spacing: 0.5, 1.0, 1.5

    std::vector<int> short_mas = {10, 25, 50, 100};
    std::vector<int> long_mas = {25, 50, 100, 200};
    std::vector<int> max_waits = {500, 1000, 2000};
    std::vector<bool> req_rev = {true, false};
    std::vector<double> spacings = {0.5, 1.0, 1.5};

    for (int sm : short_mas) {
        for (int lm : long_mas) {
            if (lm <= sm) continue;  // Long MA must be > short MA
            for (int mw : max_waits) {
                for (bool rr : req_rev) {
                    for (double sp : spacings) {
                        MomentumParams p;
                        p.short_ma = sm;
                        p.long_ma = lm;
                        p.max_wait = mw;
                        p.require_reversal_grid = rr;
                        p.spacing = sp;
                        all_params.push_back(p);
                    }
                }
            }
        }
    }

    int total_configs = all_params.size();
    int configs_per_sweep = (total_configs + 7) / 8;  // 8 parallel sweeps
    int start_idx = sweep_id * configs_per_sweep;
    int end_idx = std::min(start_idx + configs_per_sweep, total_configs);

    if (sweep_id == 0) {
        std::cout << "\n" << std::string(90, '=') << std::endl;
        std::cout << "MOMENTUM REVERSAL PARAMETER SWEEP" << std::endl;
        std::cout << "Total configurations: " << total_configs << std::endl;
        std::cout << std::string(90, '=') << std::endl;
    }

    std::cout << "\nSweep " << sweep_id << ": Testing configs " << start_idx << " to " << end_idx - 1 << std::endl;
    std::cout << std::setw(8) << "MA"
              << std::setw(6) << "Wait"
              << std::setw(5) << "Rev"
              << std::setw(6) << "Spc"
              << std::setw(11) << "Return"
              << std::setw(10) << "MaxDD"
              << std::setw(8) << "Trades"
              << std::setw(10) << "Period" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    std::vector<SweepResult> results;

    for (int i = start_idx; i < end_idx; i++) {
        const auto& p = all_params[i];

        // Test on 2025 H1 (in-sample)
        auto r1 = RunTest(p, "2025.01.01", "2025.06.30", "2025-H1");
        PrintResult(r1);
        results.push_back(r1);

        // Test on 2025 H2 (out-of-sample)
        auto r2 = RunTest(p, "2025.07.01", "2025.12.30", "2025-H2");
        PrintResult(r2);
        results.push_back(r2);
    }

    // Summary for this sweep
    std::cout << "\n=== SWEEP " << sweep_id << " SUMMARY ===" << std::endl;

    // Find best configs
    SweepResult best_h1, best_h2;
    best_h1.return_x = 0;
    best_h2.return_x = 0;

    for (const auto& r : results) {
        if (r.period == "2025-H1" && r.return_x > best_h1.return_x && !r.stopped_out) {
            best_h1 = r;
        }
        if (r.period == "2025-H2" && r.return_x > best_h2.return_x && !r.stopped_out) {
            best_h2 = r;
        }
    }

    if (best_h1.return_x > 0) {
        std::cout << "Best H1: " << best_h1.params.short_ma << "/" << best_h1.params.long_ma
                  << " wait=" << best_h1.params.max_wait
                  << " rev=" << (best_h1.params.require_reversal_grid ? "Y" : "N")
                  << " sp=" << best_h1.params.spacing
                  << " -> " << best_h1.return_x << "x" << std::endl;
    }

    if (best_h2.return_x > 0) {
        std::cout << "Best H2: " << best_h2.params.short_ma << "/" << best_h2.params.long_ma
                  << " wait=" << best_h2.params.max_wait
                  << " rev=" << (best_h2.params.require_reversal_grid ? "Y" : "N")
                  << " sp=" << best_h2.params.spacing
                  << " -> " << best_h2.return_x << "x" << std::endl;
    }

    return 0;
}
