#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>

using namespace backtest;

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
    std::map<int, Position> positions_;
    double lowest_buy_ = 999999, current_bid_ = 0, current_ask_ = 0;
    int next_ticket_ = 1, broker_updates_ = 0;

    TrailingStrategy(double sp, double mp, double td, double ut, int mx)
        : spacing_(sp), min_profit_(mp), trail_dist_(td), update_thresh_(ut), max_pos_(mx) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_bid_ = tick.bid;
        current_ask_ = tick.ask;

        std::vector<int> to_close;
        lowest_buy_ = 999999;

        for (auto& [ticket, pos] : positions_) {
            double profit = (current_bid_ - pos.entry_price) * pos.lot_size * 100.0;

            if (!pos.trailing_active && profit >= min_profit_) {
                pos.trailing_active = true;
                pos.highest_price = current_bid_;
                pos.trail_stop = current_bid_ - trail_dist_;
                broker_updates_++;
            }

            if (pos.trailing_active) {
                if (current_bid_ > pos.highest_price) {
                    if (current_bid_ >= pos.highest_price + update_thresh_) {
                        pos.highest_price = current_bid_;
                        pos.trail_stop = current_bid_ - trail_dist_;
                        broker_updates_++;
                    }
                }
                if (current_bid_ <= pos.trail_stop) {
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

        bool should_open = (pos_count == 0) || (current_ask_ <= lowest_buy_ - spacing_);
        if (should_open) {
            double equity = engine.GetEquity();
            double lot = std::min(0.5, std::max(0.01, (equity * 0.01) / (current_ask_ * 0.2)));
            Trade* trade = engine.OpenMarketOrder("BUY", lot, 0.0, 0.0);
            if (trade) {
                Position pos = {trade->entry_price, trade->lot_size, current_bid_, 0, false};
                positions_[next_ticket_++] = pos;
                broker_updates_++;
            }
        }
    }
    int GetUpdates() const { return broker_updates_; }
};

void test(double spacing, double min_profit, double trail_dist, double update_thresh, int max_pos) {
    TickDataConfig tc;
    tc.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tc.format = TickDataFormat::MT5_CSV;
    tc.load_all_into_memory = false;

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
    cfg.tick_data_config = tc;
    cfg.verbose = false;

    try {
        TickBasedEngine engine(cfg);
        TrailingStrategy strat(spacing, min_profit, trail_dist, update_thresh, max_pos);
        engine.Run([&](const Tick& t, TickBasedEngine& e) { strat.OnTick(t, e); });

        auto res = engine.GetResults();
        double ret = res.final_balance / 10000.0;
        double peak = res.final_balance + res.max_drawdown;
        double dd = (peak > 0) ? (res.max_drawdown / peak * 100.0) : 0;

        std::cout << "Sp=$" << std::fixed << std::setprecision(1) << spacing
                  << " MinP=$" << min_profit
                  << " Trail=$" << trail_dist
                  << " Upd=$" << update_thresh
                  << " MaxP=" << max_pos
                  << " -> " << std::setprecision(2) << ret << "x"
                  << " DD=" << std::setprecision(0) << dd << "%"
                  << " Upd/day=" << strat.GetUpdates()/252
                  << std::endl;
    } catch (...) {
        std::cout << "Sp=$" << spacing << " MinP=$" << min_profit << " ... FAILED" << std::endl;
    }
}

int main() {
    std::cout << "=== Trailing Stop Parameter Sweep ===" << std::endl << std::endl;

    // Vary spacing with working params from before
    std::cout << "Varying spacing (MinProfit=$5, Trail=$3, UpdateThresh=$2, MaxPos=15):" << std::endl;
    for (double sp : {1.0, 2.0, 3.0, 5.0}) {
        test(sp, 5.0, 3.0, 2.0, 15);
    }

    std::cout << std::endl << "Varying trail distance:" << std::endl;
    for (double td : {2.0, 3.0, 5.0, 10.0}) {
        test(2.0, 5.0, td, 2.0, 15);
    }

    std::cout << std::endl << "Varying min profit to activate:" << std::endl;
    for (double mp : {2.0, 5.0, 10.0, 20.0}) {
        test(2.0, mp, 3.0, 2.0, 15);
    }

    std::cout << std::endl << "Varying update threshold:" << std::endl;
    for (double ut : {1.0, 2.0, 3.0, 5.0}) {
        test(2.0, 5.0, 3.0, ut, 15);
    }

    std::cout << std::endl << "Best combo attempts:" << std::endl;
    test(3.0, 10.0, 5.0, 3.0, 10);
    test(5.0, 10.0, 5.0, 5.0, 10);
    test(5.0, 20.0, 10.0, 5.0, 10);

    return 0;
}
