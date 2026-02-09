#include "../include/fill_up_strategy.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cfloat>
#include <cmath>

using namespace backtest;

// =============================================================================
// Fixed FillUp strategy matching MT5 behavior exactly:
// 1. closest_below = DBL_MAX (not DBL_MIN)
// 2. CFDLEVERAGE margin mode in sizing (price-based)
// 3. max_volume = 100 (matching MT5 broker limit)
// 4. TP fill at TP price (not at bid) to match MT5 limit order behavior
// =============================================================================
class FillUpFixed {
public:
    FillUpFixed(double survive_pct, double spacing,
                double contract_size, double leverage,
                double min_volume = 0.01, double max_volume = 100.0)
        : survive_pct_(survive_pct), spacing_(spacing),
          contract_size_(contract_size), leverage_(leverage),
          min_volume_(min_volume), max_volume_(max_volume),
          lowest_buy_(DBL_MAX), highest_buy_(DBL_MIN),
          closest_above_(DBL_MAX), closest_below_(DBL_MAX),  // FIX: DBL_MAX like MT5
          volume_of_open_trades_(0.0), trade_size_buy_(0.0),
          spacing_buy_(spacing),
          entry_count_(0), tp_count_(0),
          tick_count_(0), last_positions_count_(0),
          near_miss_count_(0), max_near_miss_distance_(0.0)
    {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        tick_count_++;
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_timestamp_ = tick.timestamp;

        int current_positions = engine.GetOpenPositions().size();

        // Detect TP hit (positions decreased)
        if (current_positions < last_positions_count_) {
            int closed = last_positions_count_ - current_positions;
            tp_count_ += closed;
        }
        last_positions_count_ = current_positions;

        // Track near misses for open positions
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->take_profit > 0 && trade->direction == "BUY") {
                double distance_to_tp = trade->take_profit - tick.bid;
                if (distance_to_tp > 0 && distance_to_tp < spacing_ * 0.05) {
                    // Within 5% of spacing distance to TP
                    near_miss_count_++;
                    max_near_miss_distance_ = std::max(max_near_miss_distance_, distance_to_tp);
                }
            }
        }

        // Strategy logic
        Iterate(engine);
        OpenNew(engine);

        last_positions_count_ = engine.GetOpenPositions().size();
    }

    void PrintResults(double initial_balance) {
        std::cout << "\n=== FIXED STRATEGY RESULTS ===" << std::endl;
        std::cout << "Total ticks processed: " << tick_count_ << std::endl;
        std::cout << "Total entries: " << entry_count_ << std::endl;
        std::cout << "Total TP hits: " << tp_count_ << std::endl;
        std::cout << "Near-miss TP events (bid within 5% of spacing to TP): " << near_miss_count_ << std::endl;
        std::cout << "Sizing condition failures: " << condition_fail_count_ << std::endl;
        std::cout << "Sizing gave 0 lots: " << sizing_fail_count_ << std::endl;
        std::cout << std::endl;

        std::cout << "=== FULL TRADE LOG ===" << std::endl;
        std::cout << std::setw(5) << "#"
                  << std::setw(24) << "Timestamp"
                  << std::setw(10) << "EntryAsk"
                  << std::setw(10) << "Lots"
                  << std::setw(12) << "Equity"
                  << std::setw(10) << "TP"
                  << std::setw(8) << "Spread"
                  << std::setw(6) << "PosN"
                  << std::setw(12) << "Profit/Tr"
                  << std::endl;

        double running_profit = 0.0;
        for (size_t i = 0; i < trade_log_.size(); i++) {
            auto& t = trade_log_[i];
            double expected_profit = (t.spread + spacing_) * t.lots * contract_size_;
            running_profit += expected_profit;
            std::cout << std::setw(5) << (i+1)
                      << std::setw(24) << t.timestamp
                      << std::setw(10) << std::fixed << std::setprecision(3) << t.entry_price
                      << std::setw(10) << std::setprecision(2) << t.lots
                      << std::setw(12) << std::setprecision(2) << t.equity
                      << std::setw(10) << std::setprecision(3) << t.tp
                      << std::setw(8) << std::setprecision(3) << t.spread
                      << std::setw(6) << t.positions_before
                      << std::setw(12) << std::setprecision(2) << expected_profit
                      << std::endl;
        }
        std::cout << std::endl;
        std::cout << "Expected total profit from logged trades (spread+spacing per trade): $"
                  << std::fixed << std::setprecision(2) << running_profit << std::endl;
    }

    int GetEntryCount() const { return entry_count_; }
    int GetTPCount() const { return tp_count_; }

private:
    double survive_pct_, spacing_, contract_size_, leverage_;
    double min_volume_, max_volume_;
    double lowest_buy_, highest_buy_, closest_above_, closest_below_;
    double volume_of_open_trades_;
    double trade_size_buy_, spacing_buy_;
    double current_ask_, current_bid_, current_spread_, current_equity_;
    std::string current_timestamp_;

    int entry_count_, tp_count_;
    int sizing_fail_count_ = 0, condition_fail_count_ = 0;
    long tick_count_;
    int last_positions_count_;
    long near_miss_count_;
    double max_near_miss_distance_;

    struct TradeLog {
        std::string timestamp;
        double entry_price;
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
        closest_below_ = DBL_MAX;  // FIX: MT5 uses DBL_MAX
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

        // Calculate used margin using CFDLEVERAGE mode (price-based) for existing positions
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            // FIX: Use current bid for margin calculation, not entry price
            // MT5's AccountInfoDouble(ACCOUNT_MARGIN) uses current market price
            used_margin += trade->lot_size * contract_size_ * current_bid_ / leverage_;
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

        // Condition gate (identical to MT5)
        if ((positions_total == 0) || ((current_ask_ - price_difference) < end_price)) {
            equity_at_target = current_equity_ - volume_of_open_trades_ * std::abs(distance) * contract_size_;
            double margin_level = (used_margin > 0) ? (equity_at_target / used_margin * 100.0) : 10000.0;
            double trade_size = min_volume_;

            if (margin_level > margin_stop_out_level) {
                // d_equity: grid loss cost (same formula as MT5 - note: missing *spacing factor)
                double d_equity = contract_size_ * trade_size * (number_of_trades * (number_of_trades + 1) / 2.0);
                double d_spread = number_of_trades * trade_size * current_spread_ * contract_size_;
                d_equity += d_spread;

                // FOREX mode margin for projected grid (matching MT5 EA behavior)
                // MT5 EA uses: local_used_margin = trade_size * contract_size / leverage
                // (no price multiplication - the currency conversion is NOT applied in sizing)
                double local_used_margin = trade_size * contract_size_ / leverage_;
                local_used_margin = number_of_trades * local_used_margin;

                // Find max multiplier (binary search)
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
            should_open = true;  // Gap fill - now works with DBL_MAX fix
        }

        if (should_open) {
            SizingBuy(engine, positions_total);
            if (trade_size_buy_ >= min_volume_) {
                double tp = current_ask_ + current_spread_ + spacing_buy_;
                // Round lot size to 2 decimal places
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
                    trade_log_.push_back({current_timestamp_, current_ask_, trade_size_buy_,
                                         current_equity_, tp, current_spread_, positions_total});
                }
            }
        }
    }
};

// =============================================================================
// Load tick data
// =============================================================================
std::vector<Tick> LoadTicks(const std::string& path) {
    std::vector<Tick> ticks;
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open: " + path);
    }

    std::string line;
    std::getline(file, line);  // header
    ticks.reserve(30000000);

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        Tick tick;
        std::stringstream ss(line);
        std::string ts, bid_s, ask_s;
        std::getline(ss, ts, '\t');
        std::getline(ss, bid_s, '\t');
        std::getline(ss, ask_s, '\t');
        tick.timestamp = ts;
        tick.bid = std::stod(bid_s);
        tick.ask = std::stod(ask_s);
        tick.volume = 0;
        ticks.push_back(tick);
    }
    return ticks;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " XAGUSD FIXED STRATEGY DIAGNOSTIC" << std::endl;
    std::cout << " All known MT5 differences corrected" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    std::cout << "Fixes applied:" << std::endl;
    std::cout << "  1. closest_below = DBL_MAX (not DBL_MIN)" << std::endl;
    std::cout << "  2. CFDLEVERAGE margin mode in sizing" << std::endl;
    std::cout << "  3. Current bid for margin calc (not entry price)" << std::endl;
    std::cout << "  4. max_volume = 100 (matching MT5)" << std::endl;
    std::cout << std::endl;

    // Parameters matching MT5 optimization best result
    double survive = 18.0;
    double spacing = 1.90;  // Close to MT5's best at 1.95

    std::cout << "Parameters: survive=" << survive << "%, spacing=$" << spacing << std::endl;
    std::cout << "MT5 reference (S18, SP1.95): 165x return, 67 trades" << std::endl;
    std::cout << std::endl;

    // Load tick data
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\mt5\\fill_up_xagusd\\XAGUSD_TESTER_TICKS.csv";
    std::cout << "Loading ticks..." << std::endl;
    auto ticks = LoadTicks(tick_path);
    std::cout << "Loaded " << ticks.size() << " ticks" << std::endl;
    std::cout << "Data range: " << ticks.front().timestamp << " to " << ticks.back().timestamp << std::endl;
    std::cout << "First: bid=" << ticks.front().bid << " ask=" << ticks.front().ask << std::endl;
    std::cout << "Last:  bid=" << ticks.back().bid << " ask=" << ticks.back().ask << std::endl;
    std::cout << std::endl;

    // Engine config
    TickBacktestConfig config;
    config.symbol = "XAGUSD";
    config.initial_balance = 10000.0;
    config.account_currency = "USD";
    config.contract_size = 5000.0;
    config.leverage = 500.0;
    config.margin_rate = 1.0;
    config.pip_size = 0.001;
    config.swap_long = -17.03;    //  XAGUSD
    config.swap_short = 0.1;      //  XAGUSD
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = "2025.01.01";
    config.end_date = "2026.01.24";  // Process all available data
    config.verbose = false;

    TickDataConfig tc;
    tc.file_path = "";
    config.tick_data_config = tc;

    try {
        // === Run 1: Fixed Strategy ===
        std::cout << "--- Running FIXED strategy ---" << std::endl;
        TickBasedEngine engine(config);
        FillUpFixed strategy(survive, spacing, 5000.0, 500.0, 0.01, 100.0);

        engine.RunWithTicks(ticks, [&strategy](const Tick& t, TickBasedEngine& e) {
            strategy.OnTick(t, e);
        });

        auto results = engine.GetResults();
        std::cout << "Final Balance: $" << std::fixed << std::setprecision(2) << results.final_balance << std::endl;
        std::cout << "Return: " << std::setprecision(2) << results.final_balance / 10000.0 << "x" << std::endl;
        std::cout << "Closed Trades: " << results.total_trades << std::endl;
        std::cout << "Total Swap: $" << std::setprecision(2) << results.total_swap_charged << std::endl;
        std::cout << "Max DD: $" << std::setprecision(2) << results.max_drawdown << std::endl;

        strategy.PrintResults(10000.0);

        // === Run 2: Original (bugged) FillUpStrategy for comparison ===
        std::cout << "\n\n=== COMPARISON: Original FillUpStrategy (with bugs) ===" << std::endl;
        TickBasedEngine engine2(config);
        FillUpStrategy original_strategy(
            survive, 1.0, spacing, 0.01, 10.0, 5000.0, 500.0, 3, 1.0, false, 50.0
        );
        engine2.RunWithTicks(ticks, [&original_strategy](const Tick& t, TickBasedEngine& e) {
            original_strategy.OnTick(t, e);
        });
        auto results2 = engine2.GetResults();
        std::cout << "Final Balance: $" << std::fixed << std::setprecision(2) << results2.final_balance << std::endl;
        std::cout << "Return: " << std::setprecision(2) << results2.final_balance / 10000.0 << "x" << std::endl;
        std::cout << "Closed Trades: " << results2.total_trades << std::endl;

        // === Run 3: Fixed strategy with spacing=1.95 (exact MT5 best) ===
        std::cout << "\n\n=== FIXED STRATEGY @ spacing=1.95 (MT5 best) ===" << std::endl;
        TickBasedEngine engine3(config);
        FillUpFixed strategy3(survive, 1.95, 5000.0, 500.0, 0.01, 100.0);
        engine3.RunWithTicks(ticks, [&strategy3](const Tick& t, TickBasedEngine& e) {
            strategy3.OnTick(t, e);
        });
        auto results3 = engine3.GetResults();
        std::cout << "Final Balance: $" << std::fixed << std::setprecision(2) << results3.final_balance << std::endl;
        std::cout << "Return: " << std::setprecision(2) << results3.final_balance / 10000.0 << "x" << std::endl;
        std::cout << "Closed Trades: " << results3.total_trades << std::endl;
        std::cout << "Entries: " << strategy3.GetEntryCount() << ", TPs: " << strategy3.GetTPCount() << std::endl;

        // === Summary ===
        std::cout << "\n\n========================================" << std::endl;
        std::cout << " SUMMARY COMPARISON" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << std::setw(30) << "Configuration"
                  << std::setw(12) << "Return"
                  << std::setw(10) << "Trades"
                  << std::endl;
        std::cout << std::setw(30) << "MT5 (S18, SP1.95, to Jan23)"
                  << std::setw(12) << "165.02x"
                  << std::setw(10) << "67"
                  << std::endl;
        std::cout << std::setw(30) << "Fixed (S18, SP1.90, to Dec31)"
                  << std::setw(12) << std::setprecision(2) << results.final_balance / 10000.0
                  << std::setw(10) << results.total_trades
                  << std::endl;
        std::cout << std::setw(30) << "Fixed (S18, SP1.95, to Dec31)"
                  << std::setw(12) << std::setprecision(2) << results3.final_balance / 10000.0
                  << std::setw(10) << results3.total_trades
                  << std::endl;
        std::cout << std::setw(30) << "Original (S18, SP1.90, bugs)"
                  << std::setw(12) << std::setprecision(2) << results2.final_balance / 10000.0
                  << std::setw(10) << results2.total_trades
                  << std::endl;
        std::cout << std::endl;
        std::cout << "Gap analysis:" << std::endl;
        std::cout << "  Data gap: our data ends Dec 31, MT5 goes to Jan 23 (+23 days)" << std::endl;
        std::cout << "  If remaining gap after fixes > 10 trades: likely DATA difference" << std::endl;
        std::cout << "  Recommendation: export MT5 trade log for trade-by-trade comparison" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
