#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>

using namespace backtest;

struct TrailingPosition {
    double entry_price;
    double lot_size;
    double highest_price;
    bool trailing_active;
    int ticket;
};

class TrailingStrategyV2 {
public:
    double spacing_;
    double min_profit_to_trail_;
    double trail_distance_;
    double survive_pct_;
    int max_positions_;
    
    std::map<int, TrailingPosition> positions_;
    double lowest_buy_ = 999999;
    double current_bid_ = 0;
    double current_ask_ = 0;
    int next_ticket_ = 1;
    
    // Broker update tracking
    int updates_today_ = 0;
    int total_updates_ = 0;
    int current_day_ = -1;
    int max_updates_per_day_ = 0;
    
    TrailingStrategyV2(double spacing, double min_profit, double trail_dist, 
                       double survive_pct, int max_pos)
        : spacing_(spacing), min_profit_to_trail_(min_profit), 
          trail_distance_(trail_dist), survive_pct_(survive_pct),
          max_positions_(max_pos) {}
    
    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_bid_ = tick.bid;
        current_ask_ = tick.ask;
        
        // Track day for update counting
        int day = 0;
        sscanf(tick.timestamp.c_str(), "%*d.%*d.%d", &day);
        if (day != current_day_) {
            if (updates_today_ > max_updates_per_day_) {
                max_updates_per_day_ = updates_today_;
            }
            updates_today_ = 0;
            current_day_ = day;
        }
        
        std::vector<int> to_close;
        lowest_buy_ = 999999;
        
        for (auto& [ticket, pos] : positions_) {
            double profit = (current_bid_ - pos.entry_price) * pos.lot_size * 100.0;
            
            if (current_bid_ > pos.highest_price) {
                pos.highest_price = current_bid_;
                if (pos.trailing_active) {
                    // This would be a SL modification = 1 broker update
                    updates_today_++;
                    total_updates_++;
                }
            }
            
            if (!pos.trailing_active && profit >= min_profit_to_trail_) {
                pos.trailing_active = true;
                updates_today_++;  // Activating trailing = 1 update
                total_updates_++;
            }
            
            if (pos.trailing_active) {
                double trail_stop = pos.highest_price - trail_distance_;
                if (current_bid_ <= trail_stop) {
                    to_close.push_back(ticket);
                    continue;
                }
            }
            
            lowest_buy_ = std::min(lowest_buy_, pos.entry_price);
        }
        
        for (int ticket : to_close) {
            for (const Trade* trade : engine.GetOpenPositions()) {
                if (std::abs(trade->entry_price - positions_[ticket].entry_price) < 0.01 &&
                    std::abs(trade->lot_size - positions_[ticket].lot_size) < 0.001) {
                    engine.ClosePosition(const_cast<Trade*>(trade));
                    break;
                }
            }
            positions_.erase(ticket);
        }
        
        OpenNew(engine);
    }
    
    void OpenNew(TickBasedEngine& engine) {
        int pos_count = positions_.size();
        
        // Limit max positions
        if (pos_count >= max_positions_) return;
        
        bool should_open = false;
        if (pos_count == 0) {
            should_open = true;
        } else if (current_ask_ <= lowest_buy_ - spacing_) {
            should_open = true;
        }
        
        if (should_open) {
            double lot_size = CalculateLotSize(engine, pos_count);
            if (lot_size >= 0.01) {
                Trade* trade = engine.OpenMarketOrder("BUY", lot_size, 0.0, 0.0);
                if (trade) {
                    TrailingPosition pos;
                    pos.entry_price = trade->entry_price;
                    pos.lot_size = trade->lot_size;
                    pos.highest_price = current_bid_;
                    pos.trailing_active = false;
                    pos.ticket = next_ticket_++;
                    positions_[pos.ticket] = pos;
                    
                    updates_today_++;  // New order = 1 update
                    total_updates_++;
                }
            }
        }
    }
    
    double CalculateLotSize(TickBasedEngine& engine, int pos_count) {
        double equity = engine.GetEquity();
        
        // More conservative sizing
        double lot_size = (equity * 0.02) / (current_ask_ * 100.0 / 500.0);
        return std::min(1.0, std::max(0.01, lot_size));
    }
    
    int GetMaxUpdatesPerDay() const { return max_updates_per_day_; }
    int GetTotalUpdates() const { return total_updates_; }
};

void testConfig(double spacing, double min_profit, double trail_dist, int max_pos,
                const std::string& data_path) {
    TickDataConfig tick_config;
    tick_config.file_path = data_path;
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
    config.verbose = false;

    try {
        TickBasedEngine engine(config);
        TrailingStrategyV2 strategy(spacing, min_profit, trail_dist, 13.0, max_pos);

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        double ret = res.final_balance / 10000.0;
        double peak = res.final_balance + res.max_drawdown;
        double dd_pct = (peak > 0) ? (res.max_drawdown / peak * 100.0) : 0;

        std::cout << "MaxPos=" << max_pos 
                  << ", MinProfit=$" << std::fixed << std::setprecision(1) << min_profit
                  << ", Trail=$" << trail_dist
                  << " -> " << std::setprecision(2) << ret << "x"
                  << ", DD=" << std::setprecision(0) << dd_pct << "%"
                  << ", Trades=" << res.total_trades
                  << ", MaxUpdates/day=" << strategy.GetMaxUpdatesPerDay()
                  << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

int main() {
    std::string data = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    
    std::cout << "=== Trailing Stop V2 (with position limits) ===" << std::endl;
    std::cout << "Spacing $1.00" << std::endl;
    std::cout << std::endl;
    
    // Test with position limits
    testConfig(1.00, 2.0, 1.0, 10, data);
    testConfig(1.00, 2.0, 1.0, 20, data);
    testConfig(1.00, 2.0, 1.0, 50, data);
    testConfig(1.00, 5.0, 2.0, 20, data);
    testConfig(1.00, 10.0, 5.0, 20, data);
    
    std::cout << std::endl;
    std::cout << "For comparison - Fixed TP $1.00 spacing: 6.25x" << std::endl;
    
    return 0;
}
