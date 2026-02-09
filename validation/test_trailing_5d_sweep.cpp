#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <algorithm>

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
                if (current_bid_ >= pos.highest_price + update_thresh_) {
                    pos.highest_price = current_bid_;
                    pos.trail_stop = current_bid_ - trail_dist_;
                    broker_updates_++;
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

struct Result {
    int max_pos;
    double spacing, min_profit, trail_dist, update_thresh;
    double ret, dd;
    int trades, updates_per_day;
    bool stopped_out;
};

Result test(int max_pos, double spacing, double min_profit, double trail_dist, double update_thresh) {
    Result r = {max_pos, spacing, min_profit, trail_dist, update_thresh, 0, 0, 0, 0, false};

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
        r.ret = res.final_balance / 10000.0;
        double peak = res.final_balance + res.max_drawdown;
        r.dd = (peak > 0) ? (res.max_drawdown / peak * 100.0) : 0;
        r.trades = res.total_trades;
        r.updates_per_day = strat.GetUpdates() / 252;
        r.stopped_out = (r.dd > 95);
    } catch (...) {
        r.stopped_out = true;
    }
    return r;
}

int main() {
    std::cout << "=== 5-Dimensional Trailing Stop Sweep ===" << std::endl;
    std::cout << std::endl;

    std::vector<int> max_positions = {5, 10, 15, 20};
    std::vector<double> spacings = {1.0, 2.0, 3.0, 5.0};
    std::vector<double> min_profits = {5.0, 10.0, 20.0};
    std::vector<double> trail_dists = {5.0, 10.0, 20.0};
    std::vector<double> update_threshs = {2.0, 5.0};

    std::vector<Result> results;
    int total = max_positions.size() * spacings.size() * min_profits.size() *
                trail_dists.size() * update_threshs.size();
    int count = 0;

    std::cout << "Testing " << total << " combinations..." << std::endl;

    for (int mp : max_positions) {
        for (double sp : spacings) {
            for (double minp : min_profits) {
                for (double td : trail_dists) {
                    for (double ut : update_threshs) {
                        Result r = test(mp, sp, minp, td, ut);
                        results.push_back(r);
                        count++;
                        if (count % 50 == 0) {
                            std::cout << "Progress: " << count << "/" << total << std::endl;
                        }
                    }
                }
            }
        }
    }

    // Sort by return
    std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
        return a.ret > b.ret;
    });

    std::cout << std::endl;
    std::cout << "=== Top 30 Configurations ===" << std::endl;
    std::cout << std::left
              << std::setw(6) << "MaxP"
              << std::setw(7) << "Space"
              << std::setw(7) << "MinPr"
              << std::setw(7) << "Trail"
              << std::setw(6) << "UpdTh"
              << std::right
              << std::setw(10) << "Return"
              << std::setw(8) << "MaxDD"
              << std::setw(8) << "Trades"
              << std::setw(8) << "Upd/D"
              << std::endl;
    std::cout << std::string(68, '-') << std::endl;

    int shown = 0;
    for (const auto& r : results) {
        if (r.stopped_out) continue;
        std::cout << std::left << std::fixed
                  << std::setw(6) << r.max_pos
                  << "$" << std::setw(6) << std::setprecision(0) << r.spacing
                  << "$" << std::setw(6) << r.min_profit
                  << "$" << std::setw(6) << r.trail_dist
                  << "$" << std::setw(5) << r.update_thresh
                  << std::right
                  << std::setw(9) << std::setprecision(2) << r.ret << "x"
                  << std::setw(7) << std::setprecision(0) << r.dd << "%"
                  << std::setw(8) << r.trades
                  << std::setw(8) << r.updates_per_day
                  << std::endl;
        if (++shown >= 30) break;
    }

    // Count stopped out
    int stopped = 0;
    for (const auto& r : results) if (r.stopped_out) stopped++;

    std::cout << std::endl;
    std::cout << "Survived: " << (total - stopped) << "/" << total << " configurations" << std::endl;
    std::cout << std::endl;
    std::cout << "For comparison - Fixed TP $0.30: 8.80x" << std::endl;

    return 0;
}
