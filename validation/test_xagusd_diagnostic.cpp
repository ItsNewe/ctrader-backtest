#include "../include/fill_up_strategy.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

using namespace backtest;

// Diagnostic strategy that wraps FillUpStrategy and logs everything
class DiagnosticFillUp {
public:
    DiagnosticFillUp(double survive_pct, double spacing,
                     double contract_size, double leverage)
        : survive_pct_(survive_pct), spacing_(spacing),
          contract_size_(contract_size), leverage_(leverage),
          lowest_buy_(DBL_MAX), highest_buy_(DBL_MIN),
          closest_above_(DBL_MAX), closest_below_(DBL_MAX),  // NOTE: using DBL_MAX like MT5
          volume_of_open_trades_(0.0), trade_size_buy_(0.0),
          spacing_buy_(spacing), min_volume_(0.01), max_volume_(100.0),
          entry_count_(0), tp_count_(0), sizing_fail_count_(0),
          condition_fail_count_(0), tick_count_(0),
          last_positions_count_(0)
    {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        tick_count_++;
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();

        int current_positions = engine.GetOpenPositions().size();

        // Detect TP hit (positions decreased)
        if (current_positions < last_positions_count_) {
            tp_count_ += (last_positions_count_ - current_positions);
        }
        last_positions_count_ = current_positions;

        // Iterate
        Iterate(engine);

        // Try to open
        OpenNew(engine);

        last_positions_count_ = engine.GetOpenPositions().size();
    }

    void PrintDiagnostics() {
        std::cout << "\n=== DIAGNOSTICS ===" << std::endl;
        std::cout << "Total ticks processed: " << tick_count_ << std::endl;
        std::cout << "Total TP hits: " << tp_count_ << std::endl;
        std::cout << "Total entries: " << entry_count_ << std::endl;
        std::cout << "Sizing condition failures: " << condition_fail_count_ << std::endl;
        std::cout << "Sizing gave 0 lots: " << sizing_fail_count_ << std::endl;
        std::cout << "\nFirst 30 trades:" << std::endl;
        std::cout << std::setw(10) << "Tick#" << std::setw(10) << "Price"
                  << std::setw(10) << "Lots" << std::setw(12) << "Equity"
                  << std::setw(10) << "TP" << std::setw(8) << "Spread"
                  << std::setw(8) << "PosN" << std::endl;
        for (size_t i = 0; i < std::min(trade_log_.size(), (size_t)30); i++) {
            auto& t = trade_log_[i];
            std::cout << std::setw(10) << t.tick_num
                      << std::setw(10) << std::fixed << std::setprecision(3) << t.price
                      << std::setw(10) << std::setprecision(2) << t.lots
                      << std::setw(12) << std::setprecision(2) << t.equity
                      << std::setw(10) << std::setprecision(3) << t.tp
                      << std::setw(8) << std::setprecision(3) << t.spread
                      << std::setw(8) << t.positions_before
                      << std::endl;
        }
    }

private:
    double survive_pct_, spacing_, contract_size_, leverage_;
    double lowest_buy_, highest_buy_, closest_above_, closest_below_;
    double volume_of_open_trades_;
    double trade_size_buy_, spacing_buy_;
    double min_volume_, max_volume_;
    double current_ask_, current_bid_, current_spread_, current_equity_;

    int entry_count_, tp_count_, sizing_fail_count_, condition_fail_count_;
    long tick_count_;
    int last_positions_count_;

    struct TradeLog {
        long tick_num;
        double price;
        double lots;
        double equity;
        double tp;
        double spread;
        int positions_before;
    };
    std::vector<TradeLog> trade_log_;

    void Iterate(TickBasedEngine& engine) {
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;
        closest_above_ = DBL_MAX;
        closest_below_ = DBL_MAX;  // MT5 uses DBL_MAX here
        volume_of_open_trades_ = 0.0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                double open_price = trade->entry_price;
                double lots = trade->lot_size;
                volume_of_open_trades_ += lots;
                lowest_buy_ = std::min(lowest_buy_, open_price);
                highest_buy_ = std::max(highest_buy_, open_price);
                if (open_price >= current_ask_) {
                    closest_above_ = std::min(closest_above_, open_price - current_ask_);
                }
                if (open_price <= current_ask_) {
                    closest_below_ = std::min(closest_below_, current_ask_ - open_price);
                }
            }
        }
    }

    void SizingBuy(TickBasedEngine& engine, int positions_total) {
        trade_size_buy_ = 0.0;

        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            used_margin += trade->lot_size * contract_size_ * trade->entry_price / leverage_;
        }
        double margin_stop_out_level = 20.0;
        double current_margin_level = (used_margin > 0) ? (current_equity_ / used_margin * 100.0) : 10000.0;

        double equity_at_target = current_equity_ * margin_stop_out_level / current_margin_level;
        double equity_difference = current_equity_ - equity_at_target;
        double price_difference = 0.0;
        if (volume_of_open_trades_ > 0) {
            price_difference = equity_difference / (volume_of_open_trades_ * contract_size_);
        }

        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - survive_pct_) / 100.0)
            : highest_buy_ * ((100.0 - survive_pct_) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / spacing_buy_);

        // Check condition
        if ((positions_total == 0) || ((current_ask_ - price_difference) < end_price)) {
            equity_at_target = current_equity_ - volume_of_open_trades_ * std::abs(distance) * contract_size_;
            double margin_level = (used_margin > 0) ? (equity_at_target / used_margin * 100.0) : 10000.0;
            double trade_size = min_volume_;

            if (margin_level > margin_stop_out_level) {
                // d_equity: grid loss cost
                double d_equity = contract_size_ * trade_size * (number_of_trades * (number_of_trades + 1) / 2.0);
                double d_spread = number_of_trades * trade_size * current_spread_ * contract_size_;
                d_equity += d_spread;

                // Margin using CFDLEVERAGE mode (like MT5 for silver)
                double starting_price = current_ask_;
                double margin_at_start = (trade_size * contract_size_ * starting_price) / leverage_;
                double margin_at_end = (trade_size * contract_size_ * end_price) / leverage_;
                double local_used_margin = (margin_at_start + margin_at_end) / 2.0;
                local_used_margin = number_of_trades * local_used_margin;

                // Find max multiplier
                double multiplier = 0.0;
                double equity_backup = equity_at_target;
                double used_margin_backup = used_margin;
                double max = max_volume_ / min_volume_;

                equity_at_target -= max * d_equity;
                used_margin += max * local_used_margin;
                if (margin_stop_out_level < (equity_at_target / used_margin * 100.0)) {
                    multiplier = max;
                } else {
                    used_margin = used_margin_backup;
                    equity_at_target = equity_backup;
                    for (double increment = max; increment >= 1; increment /= 10) {
                        while (margin_stop_out_level < (equity_at_target / used_margin * 100.0)) {
                            equity_backup = equity_at_target;
                            used_margin_backup = used_margin;
                            multiplier += increment;
                            equity_at_target -= increment * d_equity;
                            used_margin += increment * local_used_margin;
                        }
                        multiplier -= increment;
                        used_margin = used_margin_backup;
                        equity_at_target = equity_backup;
                    }
                }

                multiplier = std::max(1.0, multiplier);
                trade_size_buy_ = multiplier * min_volume_;
                trade_size_buy_ = std::min(trade_size_buy_, max_volume_);
            } else {
                sizing_fail_count_++;
            }
        } else {
            condition_fail_count_++;
        }
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = engine.GetOpenPositions().size();

        bool should_open = false;
        if (positions_total == 0) {
            should_open = true;
        } else if (lowest_buy_ >= current_ask_ + spacing_buy_) {
            should_open = true;
        } else if (highest_buy_ <= current_ask_ - spacing_buy_) {
            should_open = true;
        } else if ((closest_above_ >= spacing_buy_) && (closest_below_ >= spacing_buy_)) {
            should_open = true;
        }

        if (should_open) {
            SizingBuy(engine, positions_total);
            if (trade_size_buy_ >= min_volume_) {
                double tp = current_ask_ + current_spread_ + spacing_buy_;
                // Round lot size
                trade_size_buy_ = std::round(trade_size_buy_ * 100.0) / 100.0;
                Trade* trade = engine.OpenMarketOrder("BUY", trade_size_buy_, 0.0, tp);
                if (trade) {
                    entry_count_++;
                    if (positions_total == 0) {
                        highest_buy_ = current_ask_;
                        lowest_buy_ = current_ask_;
                    } else if (current_ask_ < lowest_buy_) {
                        lowest_buy_ = current_ask_;
                    } else if (current_ask_ > highest_buy_) {
                        highest_buy_ = current_ask_;
                    }
                    if (trade_log_.size() < 100) {
                        trade_log_.push_back({tick_count_, current_ask_, trade_size_buy_,
                                             current_equity_, tp, current_spread_, positions_total});
                    }
                }
            }
        }
    }
};

int main() {
    std::cout << "XAGUSD DIAGNOSTIC TEST" << std::endl;
    std::cout << "Comparing entry/exit timing with MT5" << std::endl;
    std::cout << std::endl;

    // Load tick data
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = true;

    TickBacktestConfig config;
    config.symbol = "XAGUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 5000.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.001;
    config.swap_long = -25.44;
    config.swap_short = 13.72;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2026.01.24";  // Match MT5's end date
    config.tick_data_config = tick_config;
    config.verbose = false;

    // Test spacing=$1.90, survive=18 (matches MT5 comparison)
    double survive = 18.0;
    double spacing = 1.90;

    std::cout << "Config: survive=" << survive << "%, spacing=$" << spacing << std::endl;
    std::cout << "Loading ticks..." << std::endl;

    try {
        TickBasedEngine engine(config);
        DiagnosticFillUp strategy(survive, spacing, 5000.0, 500.0);

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& engine) {
            strategy.OnTick(tick, engine);
        });

        auto results = engine.GetResults();
        std::cout << "\n=== ENGINE RESULTS ===" << std::endl;
        std::cout << "Final Balance: $" << std::fixed << std::setprecision(2) << results.final_balance << std::endl;
        std::cout << "Return: " << std::setprecision(2) << results.final_balance / 10000.0 << "x" << std::endl;
        std::cout << "Max DD: $" << std::setprecision(2) << results.max_drawdown << std::endl;
        std::cout << "Total Swap: $" << std::setprecision(2) << results.total_swap_charged << std::endl;
        std::cout << "Closed Trades: " << results.total_trades << std::endl;

        strategy.PrintDiagnostics();

        // Also test with original FillUpStrategy for comparison
        std::cout << "\n\n=== COMPARISON: Original FillUpStrategy ===" << std::endl;
        TickBasedEngine engine2(config);
        FillUpStrategy original_strategy(
            survive, 1.0, spacing, 0.01, 10.0, 5000.0, 500.0, 3, 1.0, false, 50.0
        );
        engine2.Run([&original_strategy](const Tick& tick, TickBasedEngine& engine) {
            original_strategy.OnTick(tick, engine);
        });
        auto results2 = engine2.GetResults();
        std::cout << "Final Balance: $" << std::fixed << std::setprecision(2) << results2.final_balance << std::endl;
        std::cout << "Return: " << std::setprecision(2) << results2.final_balance / 10000.0 << "x" << std::endl;
        std::cout << "Closed Trades: " << results2.total_trades << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
