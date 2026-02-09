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
    long last_update_tick;
    int last_update_hour;
};

enum TrailMode {
    SERVER_SIDE,
    PRICE_THRESHOLD,
    BAR_CLOSE,
    LOCAL_TRACKING
};

class TrailingStrategy {
public:
    double spacing_;
    double min_profit_to_trail_;
    double trail_distance_;
    double survive_pct_;
    int max_positions_;
    TrailMode mode_;
    double update_threshold_;

    std::map<int, Position> positions_;
    double lowest_buy_ = 999999;
    double current_bid_ = 0;
    double current_ask_ = 0;
    int next_ticket_ = 1;
    long tick_count_ = 0;
    int current_hour_ = -1;

    int broker_updates_ = 0;
    int orders_opened_ = 0;
    int orders_closed_ = 0;

    TrailingStrategy(double spacing, double min_profit, double trail_dist,
                     double survive_pct, int max_pos, TrailMode mode,
                     double update_threshold = 0.5)
        : spacing_(spacing), min_profit_to_trail_(min_profit),
          trail_distance_(trail_dist), survive_pct_(survive_pct),
          max_positions_(max_pos), mode_(mode), update_threshold_(update_threshold) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_bid_ = tick.bid;
        current_ask_ = tick.ask;
        tick_count_++;

        int hour = 0, day = 0;
        sscanf(tick.timestamp.c_str(), "%*d.%*d.%d %d", &day, &hour);
        int hour_id = day * 24 + hour;
        bool new_hour = (hour_id != current_hour_);
        current_hour_ = hour_id;

        std::vector<int> to_close;
        lowest_buy_ = 999999;

        for (auto& [ticket, pos] : positions_) {
            double profit = (current_bid_ - pos.entry_price) * pos.lot_size * 100.0;

            if (!pos.trailing_active && profit >= min_profit_to_trail_) {
                pos.trailing_active = true;
                pos.highest_price = current_bid_;
                pos.trail_stop = current_bid_ - trail_distance_;
                pos.last_update_tick = tick_count_;
                pos.last_update_hour = hour_id;

                if (mode_ != LOCAL_TRACKING) {
                    broker_updates_++;
                }
            }

            if (pos.trailing_active) {
                bool should_update_broker = false;

                if (current_bid_ > pos.highest_price) {
                    double old_highest = pos.highest_price;
                    pos.highest_price = current_bid_;
                    double new_trail = current_bid_ - trail_distance_;

                    switch (mode_) {
                        case SERVER_SIDE:
                            pos.trail_stop = new_trail;
                            break;

                        case PRICE_THRESHOLD:
                            if (current_bid_ >= old_highest + update_threshold_) {
                                pos.trail_stop = new_trail;
                                should_update_broker = true;
                            }
                            break;

                        case BAR_CLOSE:
                            pos.trail_stop = new_trail;
                            break;

                        case LOCAL_TRACKING:
                            pos.trail_stop = new_trail;
                            break;
                    }
                }

                if (mode_ == BAR_CLOSE && new_hour && pos.last_update_hour != hour_id) {
                    if (pos.trail_stop > pos.entry_price) {
                        should_update_broker = true;
                        pos.last_update_hour = hour_id;
                    }
                }

                if (should_update_broker) {
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
                if (std::abs(trade->entry_price - positions_[ticket].entry_price) < 0.01 &&
                    std::abs(trade->lot_size - positions_[ticket].lot_size) < 0.001) {
                    engine.ClosePosition(const_cast<Trade*>(trade));
                    orders_closed_++;
                    break;
                }
            }
            positions_.erase(ticket);
        }

        OpenNew(engine);
    }

    void OpenNew(TickBasedEngine& engine) {
        int pos_count = positions_.size();
        if (pos_count >= max_positions_) return;

        bool should_open = false;
        if (pos_count == 0) {
            should_open = true;
        } else if (current_ask_ <= lowest_buy_ - spacing_) {
            should_open = true;
        }

        if (should_open) {
            double lot_size = CalculateLotSize(engine);
            if (lot_size >= 0.01) {
                Trade* trade = engine.OpenMarketOrder("BUY", lot_size, 0.0, 0.0);
                if (trade) {
                    Position pos;
                    pos.entry_price = trade->entry_price;
                    pos.lot_size = trade->lot_size;
                    pos.highest_price = current_bid_;
                    pos.trail_stop = 0;
                    pos.trailing_active = false;
                    pos.last_update_tick = tick_count_;
                    pos.last_update_hour = current_hour_;
                    positions_[next_ticket_++] = pos;

                    orders_opened_++;
                    broker_updates_++;
                }
            }
        }
    }

    double CalculateLotSize(TickBasedEngine& engine) {
        double equity = engine.GetEquity();
        double lot_size = (equity * 0.01) / (current_ask_ * 100.0 / 500.0);
        return std::min(0.5, std::max(0.01, lot_size));
    }

    int GetBrokerUpdates() const { return broker_updates_; }
};

void testMode(TrailMode mode, const std::string& mode_name, double update_threshold,
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
        TrailingStrategy strategy(2.0, 5.0, 3.0, 13.0, 15, mode, update_threshold);

        engine.Run([&](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto res = engine.GetResults();
        double ret = res.final_balance / 10000.0;
        double peak = res.final_balance + res.max_drawdown;
        double dd_pct = (peak > 0) ? (res.max_drawdown / peak * 100.0) : 0;

        int trading_days = 252;
        double updates_per_day = (double)strategy.GetBrokerUpdates() / trading_days;

        std::cout << std::left << std::setw(20) << mode_name
                  << std::right << std::fixed
                  << std::setw(8) << std::setprecision(2) << ret << "x"
                  << std::setw(8) << std::setprecision(0) << dd_pct << "%"
                  << std::setw(10) << res.total_trades
                  << std::setw(12) << strategy.GetBrokerUpdates()
                  << std::setw(10) << std::setprecision(0) << updates_per_day << "/day"
                  << std::endl;

    } catch (const std::exception& e) {
        std::cout << std::left << std::setw(20) << mode_name << " ERROR: " << e.what() << std::endl;
    }
}

int main() {
    std::string data = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

    std::cout << "=== Trailing Stop Approaches Comparison ===" << std::endl;
    std::cout << "Settings: Spacing=$2, MinProfit=$5, TrailDist=$3, MaxPos=15" << std::endl;
    std::cout << std::endl;

    std::cout << std::left << std::setw(20) << "Approach"
              << std::right << std::setw(8) << "Return"
              << std::setw(8) << "MaxDD"
              << std::setw(10) << "Trades"
              << std::setw(12) << "BrokerUpd"
              << std::setw(12) << "Upd/Day"
              << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    testMode(SERVER_SIDE, "1. Server-side", 0, data);
    testMode(PRICE_THRESHOLD, "2. Price +$0.50", 0.50, data);
    testMode(PRICE_THRESHOLD, "2. Price +$1.00", 1.00, data);
    testMode(PRICE_THRESHOLD, "2. Price +$2.00", 2.00, data);
    testMode(BAR_CLOSE, "3. Hourly bar", 0, data);
    testMode(LOCAL_TRACKING, "4. Local track", 0, data);

    std::cout << std::endl;
    std::cout << "For comparison - Fixed TP $2.00 spacing: ~2.84x" << std::endl;

    return 0;
}
