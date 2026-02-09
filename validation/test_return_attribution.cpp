#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace backtest;

// Load tick data
std::vector<Tick> g_ticks;

void LoadTicks(const std::string& path) {
    std::cout << "Loading tick data..." << std::endl;
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open: " + path);
    }

    std::string line;
    std::getline(file, line);  // Skip header

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
        g_ticks.push_back(tick);
    }
    std::cout << "Loaded " << g_ticks.size() << " ticks" << std::endl;
}

// Strategy with tracking
struct Position {
    double entry_price;
    double lot_size;
    double highest_price;
    double trail_stop;
    bool trailing_active;
};

class TrailingStrategy {
public:
    double spacing_, min_profit_, trail_dist_, update_thresh_;
    int max_pos_;
    double survive_pct_, stop_opening_pct_;
    double contract_size_ = 100.0;
    std::map<int, Position> positions_;
    double lowest_buy_ = 999999, current_bid_ = 0, current_ask_ = 0;
    double price_peak_ = 0;
    int next_ticket_ = 1;

    // Attribution tracking
    double total_oscillation_profit_ = 0;  // Profit from TP hits
    int tp_hits_ = 0;

    TrailingStrategy(double sp, double mp, double td, double ut, int mx,
                     double survive = 13.0, double stop_open = 8.0)
        : spacing_(sp), min_profit_(mp), trail_dist_(td), update_thresh_(ut),
          max_pos_(mx), survive_pct_(survive), stop_opening_pct_(stop_open) {}

    double CalculateLotSize(double equity, double price) {
        double price_drop = price * survive_pct_ / 100.0;
        double num_positions = std::min((double)max_pos_, price_drop / spacing_);
        double avg_loss_per_lot = (price_drop / 2.0) * contract_size_;
        double target_risk = equity * 0.80;
        double lot = target_risk / (num_positions * avg_loss_per_lot);
        return std::min(10.0, std::max(0.01, lot));
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_bid_ = tick.bid;
        current_ask_ = tick.ask;

        if (current_bid_ > price_peak_) {
            price_peak_ = current_bid_;
        }

        std::vector<int> to_close;
        lowest_buy_ = 999999;

        for (auto& [ticket, pos] : positions_) {
            double profit = (current_bid_ - pos.entry_price) * pos.lot_size * 100.0;

            if (!pos.trailing_active && profit >= min_profit_) {
                pos.trailing_active = true;
                pos.highest_price = current_bid_;
                pos.trail_stop = current_bid_ - trail_dist_;
            }

            if (pos.trailing_active) {
                if (current_bid_ >= pos.highest_price + update_thresh_) {
                    pos.highest_price = current_bid_;
                    pos.trail_stop = current_bid_ - trail_dist_;
                }
                if (current_bid_ <= pos.trail_stop) {
                    // Track oscillation profit (this is profit from trailing stop hit)
                    double closed_profit = (current_bid_ - pos.entry_price) * pos.lot_size * contract_size_;
                    total_oscillation_profit_ += closed_profit;
                    tp_hits_++;
                    to_close.push_back(ticket);
                    continue;
                }
            }
            lowest_buy_ = std::min(lowest_buy_, pos.entry_price);
        }

        for (int ticket : to_close) {
            for (const Trade* trade : engine.GetOpenPositions()) {
                if (std::abs(trade->entry_price - positions_[ticket].entry_price) < 0.01) {
                    engine.ClosePosition(const_cast<Trade*>(trade));
                    break;
                }
            }
            positions_.erase(ticket);
        }

        int pos_count = positions_.size();
        if (pos_count >= max_pos_) return;

        double drop_from_peak_pct = (price_peak_ - current_bid_) / price_peak_ * 100.0;
        if (drop_from_peak_pct >= stop_opening_pct_) return;

        bool should_open = (pos_count == 0) || (current_ask_ <= lowest_buy_ - spacing_);
        if (should_open) {
            double equity = engine.GetEquity();
            double lot = CalculateLotSize(equity, current_ask_);
            Trade* trade = engine.OpenMarketOrder("BUY", lot, 0.0, 0.0);
            if (trade) {
                Position pos = {trade->entry_price, trade->lot_size, current_bid_, 0, false};
                positions_[next_ticket_++] = pos;
            }
        }
    }

    double GetOscillationProfit() const { return total_oscillation_profit_; }
    int GetTPHits() const { return tp_hits_; }
};

// Simple buy-and-hold strategy for comparison
class BuyAndHoldStrategy {
public:
    double survive_pct_, contract_size_ = 100.0;
    bool has_position_ = false;
    double entry_price_ = 0, lot_size_ = 0;

    BuyAndHoldStrategy(double survive = 13.0) : survive_pct_(survive) {}

    double CalculateLotSize(double equity, double price) {
        // Same sizing as trailing strategy for fair comparison
        double price_drop = price * survive_pct_ / 100.0;
        double num_positions = 1;  // Only 1 position for buy-and-hold
        double avg_loss_per_lot = price_drop * contract_size_;
        double target_risk = equity * 0.80;
        double lot = target_risk / (num_positions * avg_loss_per_lot);
        return std::min(10.0, std::max(0.01, lot));
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        if (!has_position_) {
            double equity = engine.GetEquity();
            double lot = CalculateLotSize(equity, tick.ask);
            Trade* trade = engine.OpenMarketOrder("BUY", lot, 0.0, 0.0);
            if (trade) {
                has_position_ = true;
                entry_price_ = trade->entry_price;
                lot_size_ = trade->lot_size;
            }
        }
        // Just hold - never close
    }
};

int main() {
    std::cout << "=== Return Attribution Analysis ===" << std::endl;
    std::cout << "Decomposing returns: Oscillation vs Trend" << std::endl;
    std::cout << std::endl;

    // Load tick data
    std::string tick_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    LoadTicks(tick_path);

    // Get price range
    double start_price = 0, end_price = 0;
    for (const auto& tick : g_ticks) {
        if (tick.timestamp >= "2025.01.01" && start_price == 0) {
            start_price = tick.bid;
        }
        if (tick.timestamp >= "2025.01.01" && tick.timestamp < "2025.12.30") {
            end_price = tick.bid;
        }
    }

    double price_change_pct = (end_price - start_price) / start_price * 100.0;
    std::cout << "Gold 2025: $" << std::fixed << std::setprecision(0) << start_price
              << " -> $" << end_price << " (+" << std::setprecision(1) << price_change_pct << "%)" << std::endl;
    std::cout << std::endl;

    // Engine config
    TickBacktestConfig cfg;
    cfg.symbol = "XAUUSD";
    cfg.initial_balance = 10000.0;
    cfg.account_currency = "USD";
    cfg.contract_size = 100.0;
    cfg.leverage = 500.0;
    cfg.margin_rate = 1.0;
    cfg.pip_size = 0.01;
    cfg.swap_long = -66.99;
    cfg.swap_short = 41.2;
    cfg.swap_mode = 1;
    cfg.swap_3days = 3;
    cfg.start_date = "2025.01.01";
    cfg.end_date = "2025.12.30";
    cfg.verbose = false;

    TickDataConfig tc;
    tc.file_path = "";
    tc.load_all_into_memory = false;
    cfg.tick_data_config = tc;

    // Test 1: Trailing Stop Strategy (best config)
    std::cout << "--- Test 1: Trailing Stop Strategy ---" << std::endl;
    std::cout << "Config: MaxPos=15, Spacing=$2, MinProfit=$10, Trail=$10, UpdTh=$2" << std::endl;
    {
        TickBasedEngine engine(cfg);
        TrailingStrategy strat(2.0, 10.0, 10.0, 2.0, 15, 13.0, 8.0);

        engine.RunWithTicks(g_ticks, [&strat](const Tick& t, TickBasedEngine& e) {
            strat.OnTick(t, e);
        });

        auto res = engine.GetResults();
        double ret = res.final_balance / 10000.0;

        std::cout << "Final Balance: $" << std::fixed << std::setprecision(0) << res.final_balance << std::endl;
        std::cout << "Return: " << std::setprecision(2) << ret << "x" << std::endl;
        std::cout << "Trailing Stop Hits: " << strat.GetTPHits() << std::endl;
        std::cout << "Oscillation Profit: $" << std::setprecision(0) << strat.GetOscillationProfit() << std::endl;
        std::cout << "Swap Fees: $" << res.total_swap_charged << std::endl;
        std::cout << std::endl;
    }

    // Test 2: Buy and Hold (same initial sizing)
    std::cout << "--- Test 2: Buy and Hold (same sizing) ---" << std::endl;
    {
        TickBasedEngine engine(cfg);
        BuyAndHoldStrategy strat(13.0);

        engine.RunWithTicks(g_ticks, [&strat](const Tick& t, TickBasedEngine& e) {
            strat.OnTick(t, e);
        });

        auto res = engine.GetResults();
        double ret = res.final_balance / 10000.0;

        std::cout << "Final Balance: $" << std::fixed << std::setprecision(0) << res.final_balance << std::endl;
        std::cout << "Return: " << std::setprecision(2) << ret << "x" << std::endl;
        std::cout << "Swap Fees: $" << res.total_swap_charged << std::endl;
        std::cout << std::endl;
    }

    // Test 3: Buy and Hold with 10x leverage (to match aggressive returns)
    std::cout << "--- Test 3: Buy and Hold (aggressive sizing) ---" << std::endl;
    {
        TickBasedEngine engine(cfg);

        // Just buy once with aggressive lot size
        engine.RunWithTicks(g_ticks, [&](const Tick& t, TickBasedEngine& e) {
            if (e.GetOpenPositions().empty()) {
                // Aggressive: use 5% of price as survive (very high leverage)
                double price_drop = t.ask * 0.05;
                double lot = (10000.0 * 0.80) / (price_drop * 100.0);
                lot = std::min(10.0, std::max(0.01, lot));
                e.OpenMarketOrder("BUY", lot, 0.0, 0.0);
            }
        });

        auto res = engine.GetResults();
        double ret = res.final_balance / 10000.0;

        std::cout << "Final Balance: $" << std::fixed << std::setprecision(0) << res.final_balance << std::endl;
        std::cout << "Return: " << std::setprecision(2) << ret << "x" << std::endl;
        std::cout << "Swap Fees: $" << res.total_swap_charged << std::endl;
        std::cout << std::endl;
    }

    // Summary
    std::cout << "=== Attribution Summary ===" << std::endl;
    std::cout << "Gold price appreciation: +" << std::setprecision(1) << price_change_pct << "%" << std::endl;
    std::cout << std::endl;
    std::cout << "The trailing stop strategy achieves higher returns through:" << std::endl;
    std::cout << "1. Compounding: Re-investing profits into larger positions" << std::endl;
    std::cout << "2. Oscillation capture: Buying dips, selling rallies via trailing stops" << std::endl;
    std::cout << "3. Multiple positions: Grid of positions amplifies both effects" << std::endl;
    std::cout << std::endl;
    std::cout << "Buy-and-hold shows pure trend return (with same initial sizing)." << std::endl;
    std::cout << "Difference = Alpha from oscillation trading + compounding effect." << std::endl;

    return 0;
}
