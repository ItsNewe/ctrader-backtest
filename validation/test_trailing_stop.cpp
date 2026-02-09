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

class TrailingStrategy {
public:
    double spacing_;
    double min_profit_to_trail_;  // Minimum profit before trailing starts
    double trail_distance_;       // How far behind to trail
    double survive_pct_;
    
    std::map<int, TrailingPosition> positions_;
    double lowest_buy_ = 999999;
    double current_bid_ = 0;
    double current_ask_ = 0;
    int next_ticket_ = 1;
    
    TrailingStrategy(double spacing, double min_profit, double trail_dist, double survive_pct)
        : spacing_(spacing), min_profit_to_trail_(min_profit), 
          trail_distance_(trail_dist), survive_pct_(survive_pct) {}
    
    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_bid_ = tick.bid;
        current_ask_ = tick.ask;
        
        // Update positions and check trailing stops
        std::vector<int> to_close;
        lowest_buy_ = 999999;
        
        for (auto& [ticket, pos] : positions_) {
            double profit = (current_bid_ - pos.entry_price) * pos.lot_size * 100.0;
            
            // Update highest price seen
            if (current_bid_ > pos.highest_price) {
                pos.highest_price = current_bid_;
            }
            
            // Activate trailing once minimum profit reached
            if (!pos.trailing_active && profit >= min_profit_to_trail_) {
                pos.trailing_active = true;
            }
            
            // Check trailing stop
            if (pos.trailing_active) {
                double trail_stop = pos.highest_price - trail_distance_;
                if (current_bid_ <= trail_stop) {
                    to_close.push_back(ticket);
                    continue;
                }
            }
            
            lowest_buy_ = std::min(lowest_buy_, pos.entry_price);
        }
        
        // Close positions that hit trailing stop
        for (int ticket : to_close) {
            // Find and close the actual engine position
            for (const Trade* trade : engine.GetOpenPositions()) {
                if (std::abs(trade->entry_price - positions_[ticket].entry_price) < 0.01 &&
                    std::abs(trade->lot_size - positions_[ticket].lot_size) < 0.001) {
                    engine.ClosePosition(const_cast<Trade*>(trade));
                    break;
                }
            }
            positions_.erase(ticket);
        }
        
        // Open new positions
        OpenNew(engine);
    }
    
    void OpenNew(TickBasedEngine& engine) {
        int pos_count = positions_.size();
        
        bool should_open = false;
        if (pos_count == 0) {
            should_open = true;
        } else if (current_ask_ <= lowest_buy_ - spacing_) {
            should_open = true;
        }
        
        if (should_open) {
            double lot_size = CalculateLotSize(engine, pos_count);
            if (lot_size >= 0.01) {
                // Open without TP (TP = 0 means no TP)
                Trade* trade = engine.OpenMarketOrder("BUY", lot_size, 0.0, 0.0);
                if (trade) {
                    TrailingPosition pos;
                    pos.entry_price = trade->entry_price;
                    pos.lot_size = trade->lot_size;
                    pos.highest_price = current_bid_;
                    pos.trailing_active = false;
                    pos.ticket = next_ticket_++;
                    positions_[pos.ticket] = pos;
                }
            }
        }
    }
    
    double CalculateLotSize(TickBasedEngine& engine, int pos_count) {
        double equity = engine.GetEquity();
        double end_price = (pos_count == 0) 
            ? current_ask_ * (1.0 - survive_pct_/100.0)
            : lowest_buy_ * (1.0 - survive_pct_/100.0);
        
        double distance = current_ask_ - end_price;
        double num_trades = std::max(1.0, std::floor(distance / spacing_));
        
        // Simple sizing: divide available margin
        double available = equity * 0.5;  // Use 50% of equity
        double lot_size = available / (num_trades * current_ask_ * 100.0 / 500.0);
        
        return std::min(10.0, std::max(0.01, lot_size));
    }
};

void testConfig(double spacing, double min_profit, double trail_dist, 
                const std::string& data_path, const std::string& label) {
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
        TrailingStrategy strategy(spacing, min_profit, trail_dist, 13.0);

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        double ret = res.final_balance / 10000.0;
        double peak = res.final_balance + res.max_drawdown;
        double dd_pct = (peak > 0) ? (res.max_drawdown / peak * 100.0) : 0;

        std::cout << label << ": " << std::fixed << std::setprecision(2) 
                  << ret << "x, DD=" << std::setprecision(0) << dd_pct << "%, "
                  << res.total_trades << " trades" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

int main() {
    std::string data = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    
    std::cout << "=== Trailing Stop Strategy Test (2025) ===" << std::endl;
    std::cout << "Spacing $0.30, varying trail parameters:" << std::endl;
    std::cout << std::endl;
    
    // Test different min_profit and trail_distance combinations
    testConfig(0.30, 0.50, 0.30, data, "MinProfit=$0.50, Trail=$0.30");
    testConfig(0.30, 1.00, 0.50, data, "MinProfit=$1.00, Trail=$0.50");
    testConfig(0.30, 2.00, 1.00, data, "MinProfit=$2.00, Trail=$1.00");
    testConfig(0.30, 5.00, 2.00, data, "MinProfit=$5.00, Trail=$2.00");
    
    std::cout << std::endl;
    std::cout << "For comparison - Fixed TP strategy: 8.80x" << std::endl;
    
    return 0;
}
