/**
 * test_v4_v5_combinedju_forced.cpp
 *
 * PARALLEL test of forced entry across V4, V5, and CombinedJu strategies.
 * Tests 2024 and 2025 data to validate regime independence.
 *
 * Key discoveries being tested:
 * - Forced entry (when lot sizing returns 0, force MinVolume anyway)
 *   dramatically improves returns for strategies with adaptive spacing
 * - Does this apply to V4, V5, and CombinedJu as well?
 */

#include "../include/fill_up_strategy_v4.h"
#include "../include/fill_up_strategy_v5.h"
#include "../include/strategy_combined_ju.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <chrono>

using namespace backtest;

// Shared tick data - loaded ONCE
std::vector<Tick> g_ticks_2024;
std::vector<Tick> g_ticks_2025;

enum StrategyType {
    STRAT_V4,
    STRAT_V5,
    STRAT_COMBINED_JU,
    STRAT_COMBINED_JU_NO_VELOCITY,
    STRAT_COMBINED_JU_LINEAR_TP,
    STRAT_COMBINED_JU_BARBELL
};

struct TestConfig {
    std::string name;
    StrategyType strategy;
    bool force_min_volume_entry;
    std::string year;
};

struct TestResult {
    std::string name;
    std::string year;
    double final_balance;
    double return_multiple;
    double max_dd_pct;
    int total_trades;
    int peak_positions;
    bool completed;
};

// Thread-safe work queue
std::mutex g_queue_mutex;
std::mutex g_results_mutex;
std::queue<TestConfig> g_work_queue;
std::vector<TestResult> g_results;
std::atomic<int> g_completed(0);
int g_total_tasks = 0;

void LoadTickData() {
    auto start = std::chrono::high_resolution_clock::now();

    // Load 2024
    std::cout << "Loading 2024 tick data..." << std::endl;
    {
        TickDataConfig cfg;
        cfg.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\historical\\XAUUSD_TICKS_2024.csv";
        cfg.format = TickDataFormat::MT5_CSV;
        TickDataManager mgr(cfg);
        Tick tick;
        while (mgr.GetNextTick(tick)) {
            g_ticks_2024.push_back(tick);
        }
    }
    std::cout << "  Loaded " << g_ticks_2024.size() << " ticks (2024)" << std::endl;

    // Load 2025+Jan2026
    std::cout << "Loading 2025 tick data..." << std::endl;
    {
        std::vector<std::string> files = {
            "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv",
            "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_JAN2026.csv"
        };
        for (const auto& file : files) {
            TickDataConfig cfg;
            cfg.file_path = file;
            cfg.format = TickDataFormat::MT5_CSV;
            TickDataManager mgr(cfg);
            Tick tick;
            while (mgr.GetNextTick(tick)) {
                g_ticks_2025.push_back(tick);
            }
        }
        std::sort(g_ticks_2025.begin(), g_ticks_2025.end(),
                  [](const Tick& a, const Tick& b) { return a.timestamp < b.timestamp; });
    }
    std::cout << "  Loaded " << g_ticks_2025.size() << " ticks (2025+Jan2026)" << std::endl;

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    std::cout << "Total load time: " << duration << "s" << std::endl;
}

// V4 with optional forced entry
class FillUpStrategyV4Forced {
public:
    FillUpStrategyV4Forced(const FillUpStrategyV4::Config& config, bool force_entry)
        : config_(config), force_entry_(force_entry),
          lowest_buy_(DBL_MAX), highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0), peak_equity_(0.0),
          max_number_of_open_(0), partial_close_done_(false),
          all_closed_(false), cooldown_remaining_(0),
          current_spacing_(config.spacing), effective_max_positions_(config.max_positions) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();

        price_history_.push_back(tick.bid);
        if (price_history_.size() > (size_t)config_.volatility_window) {
            price_history_.pop_front();
        }

        if (engine.GetOpenPositions().empty()) {
            if (peak_equity_ != current_balance_) {
                peak_equity_ = current_balance_;
                partial_close_done_ = false;
                all_closed_ = false;
            }
        }

        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
            partial_close_done_ = false;
            all_closed_ = false;
        }

        double current_drawdown_pct = 0.0;
        if (peak_equity_ > 0) {
            current_drawdown_pct = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
        }

        max_number_of_open_ = std::max(max_number_of_open_, (int)engine.GetOpenPositions().size());

        if (config_.adaptive_positions) {
            if (current_drawdown_pct > config_.stop_new_at_dd) {
                effective_max_positions_ = config_.max_pos_at_5pct_dd;
            } else if (current_drawdown_pct > 3.0) {
                effective_max_positions_ = config_.max_pos_at_3pct_dd;
            } else {
                effective_max_positions_ = config_.max_positions;
            }
        }

        if (config_.volatility_spacing) {
            double vol = CalculateVolatility();
            double vol_mult = 1.0 + std::min(vol * 2.0, config_.volatility_mult_max - 1.0);
            current_spacing_ = config_.spacing * vol_mult;
        }

        if (cooldown_remaining_ > 0) {
            cooldown_remaining_--;
            return;
        }

        if (current_drawdown_pct > config_.close_all_at_dd && !all_closed_ && !engine.GetOpenPositions().empty()) {
            CloseAllPositions(engine);
            all_closed_ = true;
            cooldown_remaining_ = config_.recovery_cooldown;
            peak_equity_ = engine.GetBalance();
            return;
        }

        if (current_drawdown_pct > config_.partial_close_at_dd && !partial_close_done_ && engine.GetOpenPositions().size() > 1) {
            CloseWorstPositions(engine, config_.partial_close_pct);
            partial_close_done_ = true;
        }

        Iterate(engine);

        if (current_drawdown_pct < config_.stop_new_at_dd) {
            OpenNew(engine);
        }
    }

    int GetMaxNumberOfOpen() const { return max_number_of_open_; }

private:
    FillUpStrategyV4::Config config_;
    bool force_entry_;

    double lowest_buy_;
    double highest_buy_;
    double closest_above_;
    double closest_below_;
    double volume_of_open_trades_;
    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;
    double peak_equity_;
    int max_number_of_open_;
    bool partial_close_done_;
    bool all_closed_;
    int cooldown_remaining_;
    std::deque<double> price_history_;
    double current_spacing_;
    int effective_max_positions_;

    double CalculateVolatility() {
        if (price_history_.size() < 10) return 0.0;
        double sum = 0.0, sum_sq = 0.0;
        int n = price_history_.size() - 1;
        for (size_t i = 1; i < price_history_.size(); i++) {
            double change = std::abs(price_history_[i] - price_history_[i-1]);
            sum += change;
            sum_sq += change * change;
        }
        double mean = sum / n;
        double variance = (sum_sq / n) - (mean * mean);
        return std::sqrt(std::max(0.0, variance));
    }

    void CloseAllPositions(TickBasedEngine& engine) {
        auto& positions = engine.GetOpenPositions();
        while (!positions.empty()) {
            engine.ClosePosition(positions[0]);
        }
    }

    void CloseWorstPositions(TickBasedEngine& engine, double pct) {
        auto& positions = engine.GetOpenPositions();
        if (positions.size() <= 1) return;
        std::vector<std::pair<double, Trade*>> pl_and_trade;
        for (Trade* t : positions) {
            double pl = (current_bid_ - t->entry_price) * t->lot_size * config_.contract_size;
            pl_and_trade.push_back({pl, t});
        }
        std::sort(pl_and_trade.begin(), pl_and_trade.end());
        int to_close = (int)(pl_and_trade.size() * pct);
        to_close = std::max(1, to_close);
        for (int i = 0; i < to_close; i++) {
            engine.ClosePosition(pl_and_trade[i].second);
        }
    }

    void Iterate(TickBasedEngine& engine) {
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;
        closest_above_ = DBL_MAX;
        closest_below_ = DBL_MIN;
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

    bool Open(double local_unit, TickBasedEngine& engine) {
        if (local_unit < config_.min_volume) {
            if (force_entry_) {
                local_unit = config_.min_volume;
            } else {
                return false;
            }
        }
        double final_unit = std::min(local_unit, config_.max_volume);
        final_unit = std::round(final_unit * 100.0) / 100.0;
        double tp = current_ask_ + current_spread_ + current_spacing_;
        Trade* trade = engine.OpenMarketOrder("BUY", final_unit, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = engine.GetOpenPositions().size();
        if (positions_total >= effective_max_positions_) return;

        double lot_size = config_.min_volume;
        if (positions_total == 0) {
            if (Open(lot_size, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                if (Open(lot_size, engine)) {
                    lowest_buy_ = current_ask_;
                }
            }
            else if (highest_buy_ <= current_ask_ - current_spacing_) {
                if (Open(lot_size, engine)) {
                    highest_buy_ = current_ask_;
                }
            }
            else if ((closest_above_ >= current_spacing_) && (closest_below_ >= current_spacing_)) {
                Open(lot_size, engine);
            }
        }
    }
};

// V5 with optional forced entry
class FillUpStrategyV5Forced {
public:
    FillUpStrategyV5Forced(const FillUpStrategyV5::Config& config, bool force_entry)
        : config_(config), force_entry_(force_entry),
          lowest_buy_(DBL_MAX), highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0), trade_size_buy_(0.0),
          peak_equity_(0.0), max_number_of_open_(0),
          partial_close_done_(false), all_closed_(false),
          buffer_index_(0), ticks_seen_(0), running_sum_(0.0), sma_value_(0.0) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();

        UpdateSMA(tick);

        if (engine.GetOpenPositions().empty()) {
            if (peak_equity_ != current_balance_) {
                peak_equity_ = current_balance_;
                partial_close_done_ = false;
                all_closed_ = false;
            }
        }

        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
            partial_close_done_ = false;
            all_closed_ = false;
        }

        double current_drawdown_pct = 0.0;
        if (peak_equity_ > 0) {
            current_drawdown_pct = (peak_equity_ - current_equity_) / peak_equity_ * 100.0;
        }

        max_number_of_open_ = std::max(max_number_of_open_, (int)engine.GetOpenPositions().size());

        if (current_drawdown_pct > config_.close_all_at_dd && !all_closed_ && !engine.GetOpenPositions().empty()) {
            CloseAllPositions(engine);
            all_closed_ = true;
            peak_equity_ = engine.GetBalance();
            return;
        }

        if (current_drawdown_pct > config_.partial_close_at_dd && !partial_close_done_ && engine.GetOpenPositions().size() > 1) {
            CloseHalfPositions(engine);
            partial_close_done_ = true;
        }

        Iterate(engine);

        bool trend_ok = IsTrendOk();
        if (current_drawdown_pct < config_.stop_new_at_dd && trend_ok) {
            OpenNew(engine, current_drawdown_pct);
        }
    }

    int GetMaxNumberOfOpen() const { return max_number_of_open_; }

private:
    FillUpStrategyV5::Config config_;
    bool force_entry_;

    double lowest_buy_;
    double highest_buy_;
    double closest_above_;
    double closest_below_;
    double volume_of_open_trades_;
    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;
    double trade_size_buy_;
    double peak_equity_;
    int max_number_of_open_;
    bool partial_close_done_;
    bool all_closed_;
    std::vector<double> price_buffer_;
    size_t buffer_index_;
    size_t ticks_seen_;
    double running_sum_;
    double sma_value_;

    void UpdateSMA(const Tick& tick) {
        double price = tick.bid;
        if (price_buffer_.empty()) {
            price_buffer_.resize(config_.ma_period, 0.0);
            buffer_index_ = 0;
            ticks_seen_ = 0;
            running_sum_ = 0.0;
        }
        if (ticks_seen_ >= (size_t)config_.ma_period) {
            running_sum_ -= price_buffer_[buffer_index_];
        }
        running_sum_ += price;
        price_buffer_[buffer_index_] = price;
        ticks_seen_++;
        buffer_index_ = (buffer_index_ + 1) % config_.ma_period;
        if (ticks_seen_ >= (size_t)config_.ma_period) {
            sma_value_ = running_sum_ / config_.ma_period;
        } else {
            sma_value_ = 0.0;
        }
    }

    bool IsTrendOk() const {
        if (sma_value_ == 0.0) return false;
        return current_bid_ > sma_value_;
    }

    void CloseAllPositions(TickBasedEngine& engine) {
        auto& positions = engine.GetOpenPositions();
        while (!positions.empty()) {
            engine.ClosePosition(positions[0]);
        }
    }

    void CloseHalfPositions(TickBasedEngine& engine) {
        auto& positions = engine.GetOpenPositions();
        if (positions.size() <= 1) return;
        std::vector<std::pair<double, Trade*>> pl_and_trade;
        for (Trade* t : positions) {
            double pl = (current_bid_ - t->entry_price) * t->lot_size * config_.contract_size;
            pl_and_trade.push_back({pl, t});
        }
        std::sort(pl_and_trade.begin(), pl_and_trade.end());
        int to_close = pl_and_trade.size() / 2;
        for (int i = 0; i < to_close; i++) {
            engine.ClosePosition(pl_and_trade[i].second);
        }
    }

    void Iterate(TickBasedEngine& engine) {
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;
        closest_above_ = DBL_MAX;
        closest_below_ = DBL_MIN;
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

    void SizingBuy(TickBasedEngine& engine, int positions_total, double current_dd_pct) {
        trade_size_buy_ = 0.0;
        if (positions_total >= config_.max_positions) return;

        double size_reduction = 1.0;
        if (current_dd_pct > config_.reduce_size_at_dd) {
            double dd_range = config_.stop_new_at_dd - config_.reduce_size_at_dd;
            double dd_progress = (current_dd_pct - config_.reduce_size_at_dd) / dd_range;
            size_reduction = 1.0 - (dd_progress * 0.75);
            size_reduction = std::max(0.25, size_reduction);
        }

        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            used_margin += trade->lot_size * config_.contract_size * trade->entry_price / config_.leverage;
        }

        double margin_stop_out_level = 20.0;
        double current_margin_level = (used_margin > 0) ? (current_equity_ / used_margin * 100.0) : 10000.0;
        double equity_at_target = current_equity_ * margin_stop_out_level / current_margin_level;
        double equity_difference = current_equity_ - equity_at_target;

        double price_difference = 0.0;
        if (volume_of_open_trades_ > 0) {
            price_difference = equity_difference / (volume_of_open_trades_ * config_.contract_size);
        }

        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - config_.survive_pct) / 100.0)
            : highest_buy_ * ((100.0 - config_.survive_pct) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / config_.spacing);
        number_of_trades = std::min(number_of_trades, (double)(config_.max_positions - positions_total));

        if ((positions_total == 0) || ((current_ask_ - price_difference) < end_price)) {
            equity_at_target = current_equity_ - volume_of_open_trades_ * std::abs(distance) * config_.contract_size;
            double margin_level = (used_margin > 0) ? (equity_at_target / used_margin * 100.0) : 10000.0;
            double trade_size = config_.min_volume;

            if (margin_level > margin_stop_out_level) {
                double d_equity = config_.contract_size * trade_size * (number_of_trades * (number_of_trades + 1) / 2);
                double d_spread = number_of_trades * trade_size * current_spread_ * config_.contract_size;
                d_equity += d_spread;
                double local_used_margin = trade_size * config_.contract_size / config_.leverage;
                local_used_margin = number_of_trades * local_used_margin;

                double multiplier = 0.0;
                double equity_backup = equity_at_target;
                double used_margin_backup = used_margin;
                double max = config_.max_volume / config_.min_volume;

                equity_at_target -= max * d_equity;
                used_margin += max * local_used_margin;

                if (margin_stop_out_level < (equity_at_target / used_margin * 100.0)) {
                    multiplier = max;
                } else {
                    used_margin = used_margin_backup;
                    equity_at_target = equity_backup;
                    for (double increment = max; increment >= 1; increment = increment / 10) {
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
                trade_size_buy_ = multiplier * config_.min_volume;
                trade_size_buy_ *= size_reduction;
                trade_size_buy_ = std::min(trade_size_buy_, config_.max_volume);
                trade_size_buy_ = std::max(trade_size_buy_, config_.min_volume);
            }
        }
    }

    bool Open(double local_unit, TickBasedEngine& engine) {
        if (local_unit < config_.min_volume) {
            if (force_entry_) {
                local_unit = config_.min_volume;
            } else {
                return false;
            }
        }
        double final_unit = std::min(local_unit, config_.max_volume);
        final_unit = std::round(final_unit * 100.0) / 100.0;
        double tp = current_ask_ + current_spread_ + (config_.spacing * config_.tp_multiplier);
        Trade* trade = engine.OpenMarketOrder("BUY", final_unit, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine, double current_dd_pct) {
        int positions_total = engine.GetOpenPositions().size();
        if (positions_total >= config_.max_positions) return;

        if (positions_total == 0) {
            SizingBuy(engine, positions_total, current_dd_pct);
            if (Open(trade_size_buy_, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            if (lowest_buy_ >= current_ask_ + config_.spacing) {
                SizingBuy(engine, positions_total, current_dd_pct);
                if (Open(trade_size_buy_, engine)) {
                    lowest_buy_ = current_ask_;
                }
            }
            else if (highest_buy_ <= current_ask_ - config_.spacing) {
                SizingBuy(engine, positions_total, current_dd_pct);
                if (Open(trade_size_buy_, engine)) {
                    highest_buy_ = current_ask_;
                }
            }
            else if ((closest_above_ >= config_.spacing) && (closest_below_ >= config_.spacing)) {
                SizingBuy(engine, positions_total, current_dd_pct);
                Open(trade_size_buy_, engine);
            }
        }
    }
};

// CombinedJu with configurable forced entry
class StrategyCombinedJuForced {
public:
    StrategyCombinedJuForced(const StrategyCombinedJu::Config& config, bool force_entry)
        : config_(config), force_entry_(force_entry),
          current_spacing_(config.base_spacing), lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN), volume_of_open_trades_(0.0),
          recent_high_(0.0), recent_low_(DBL_MAX),
          last_vol_reset_seconds_(0), first_entry_price_(0.0),
          position_count_(0), current_velocity_pct_(0.0), max_positions_(0) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();

        UpdateVelocity();
        UpdateVolatility(tick);
        UpdateAdaptiveSpacing();
        Iterate(engine);
        OpenNew(engine);
    }

    int GetMaxPositions() const { return max_positions_; }

private:
    StrategyCombinedJu::Config config_;
    bool force_entry_;

    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;
    double current_spacing_;
    double lowest_buy_;
    double highest_buy_;
    double volume_of_open_trades_;
    double recent_high_;
    double recent_low_;
    long last_vol_reset_seconds_;
    double first_entry_price_;
    int position_count_;
    std::deque<double> price_window_;
    double current_velocity_pct_;
    int max_positions_;

    static long ParseTimestampToSeconds(const std::string& ts) {
        if (ts.size() < 19) return 0;
        int year = std::stoi(ts.substr(0, 4));
        int month = std::stoi(ts.substr(5, 2));
        int day = std::stoi(ts.substr(8, 2));
        int hour = std::stoi(ts.substr(11, 2));
        int minute = std::stoi(ts.substr(14, 2));
        int second = std::stoi(ts.substr(17, 2));
        int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
        long days = (long)(year - 2020) * 365 + (year - 2020) / 4;
        days += month_days[month - 1] + day;
        if (month > 2 && year % 4 == 0) days++;
        return days * 86400L + hour * 3600L + minute * 60L + second;
    }

    void UpdateVelocity() {
        price_window_.push_back(current_bid_);
        while ((int)price_window_.size() > config_.velocity_window) {
            price_window_.pop_front();
        }
        if ((int)price_window_.size() >= config_.velocity_window) {
            double old_price = price_window_.front();
            current_velocity_pct_ = (current_bid_ - old_price) / old_price * 100.0;
        }
    }

    void UpdateVolatility(const Tick& tick) {
        long current_seconds = ParseTimestampToSeconds(tick.timestamp);
        long lookback_seconds = (long)(config_.volatility_lookback_hours * 3600.0);
        if (last_vol_reset_seconds_ == 0 ||
            current_seconds - last_vol_reset_seconds_ >= lookback_seconds) {
            recent_high_ = current_bid_;
            recent_low_ = current_bid_;
            last_vol_reset_seconds_ = current_seconds;
        }
        recent_high_ = std::max(recent_high_, current_bid_);
        recent_low_ = std::min(recent_low_, current_bid_);
    }

    void UpdateAdaptiveSpacing() {
        double range = recent_high_ - recent_low_;
        if (range > 0 && recent_high_ > 0 && current_bid_ > 0) {
            double typical_vol = current_bid_ * (config_.typical_vol_pct / 100.0);
            double vol_ratio = range / typical_vol;
            vol_ratio = std::max(0.5, std::min(3.0, vol_ratio));
            double new_spacing = config_.base_spacing * vol_ratio;
            new_spacing = std::max(0.5, std::min(5.0, new_spacing));
            if (std::abs(new_spacing - current_spacing_) > 0.1) {
                current_spacing_ = new_spacing;
            }
        }
    }

    bool CheckVelocityZero() const {
        if (!config_.enable_velocity_filter) return true;
        return std::abs(current_velocity_pct_) < config_.velocity_threshold_pct;
    }

    double CalculateTP() const {
        if (first_entry_price_ <= 0) {
            return current_ask_ + current_spread_ + current_spacing_;
        }
        double deviation = std::abs(first_entry_price_ - current_ask_);
        switch (config_.tp_mode) {
            case StrategyCombinedJu::FIXED:
                return current_ask_ + current_spread_ + current_spacing_;
            case StrategyCombinedJu::SQRT: {
                double tp_addition = config_.tp_sqrt_scale * std::sqrt(deviation);
                return current_ask_ + current_spread_ + std::max(config_.tp_min, tp_addition);
            }
            case StrategyCombinedJu::LINEAR: {
                double tp_addition = config_.tp_linear_scale * deviation;
                return current_ask_ + current_spread_ + std::max(config_.tp_min, tp_addition);
            }
        }
        return current_ask_ + current_spread_ + current_spacing_;
    }

    double CalculateLotMultiplier() const {
        switch (config_.sizing_mode) {
            case StrategyCombinedJu::UNIFORM: return 1.0;
            case StrategyCombinedJu::LINEAR_SIZING:
                return 1.0 + position_count_ * config_.sizing_linear_scale;
            case StrategyCombinedJu::THRESHOLD_SIZING:
                return (position_count_ >= config_.sizing_threshold_pos)
                    ? config_.sizing_threshold_mult : 1.0;
        }
        return 1.0;
    }

    void Iterate(TickBasedEngine& engine) {
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;
        volume_of_open_trades_ = 0.0;
        position_count_ = 0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                volume_of_open_trades_ += trade->lot_size;
                lowest_buy_ = std::min(lowest_buy_, trade->entry_price);
                highest_buy_ = std::max(highest_buy_, trade->entry_price);
                position_count_++;
            }
        }
        if (position_count_ == 0) first_entry_price_ = 0.0;
        if (position_count_ > max_positions_) max_positions_ = position_count_;
    }

    double CalculateLotSize(TickBasedEngine& engine, int positions_total) {
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            used_margin += trade->lot_size * config_.contract_size *
                          trade->entry_price / config_.leverage;
        }
        double margin_stop_out = 20.0;
        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - config_.survive_pct) / 100.0)
            : highest_buy_ * ((100.0 - config_.survive_pct) / 100.0);
        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = current_equity_ -
                                 volume_of_open_trades_ * distance * config_.contract_size;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = config_.min_volume;
        double d_equity = config_.contract_size * trade_size * current_spacing_ *
                         (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * config_.contract_size / config_.leverage;

        double max_mult = config_.max_volume / config_.min_volume;
        for (double mult = max_mult; mult >= 1.0; mult -= 0.1) {
            double test_equity = equity_at_target - mult * d_equity;
            double test_margin = used_margin + mult * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                trade_size = mult * config_.min_volume;
                break;
            }
        }
        return std::min(trade_size, config_.max_volume);
    }

    bool Open(double lots, double tp, TickBasedEngine& engine) {
        if (lots < config_.min_volume) {
            if (force_entry_) {
                lots = config_.min_volume;
            } else {
                return false;
            }
        }
        double final_lots = std::min(lots, config_.max_volume);
        final_lots = std::round(final_lots * 100.0) / 100.0;
        Trade* trade = engine.OpenMarketOrder("BUY", final_lots, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = (int)engine.GetOpenPositions().size();

        if (positions_total == 0) {
            double lots = CalculateLotSize(engine, positions_total);
            double tp = current_ask_ + current_spread_ + current_spacing_;
            if (Open(lots, tp, engine)) {
                first_entry_price_ = current_ask_;
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            if (lowest_buy_ >= current_ask_ + current_spacing_) {
                if (!CheckVelocityZero()) return;
                double lots = CalculateLotSize(engine, positions_total);
                double lot_mult = CalculateLotMultiplier();
                if (lot_mult > 1.0) {
                    double safety_factor = 1.0 / (1.0 + position_count_ * 0.05);
                    lot_mult = 1.0 + (lot_mult - 1.0) * safety_factor;
                }
                lots *= lot_mult;
                lots = std::max(lots, config_.min_volume);
                lots = std::min(lots, config_.max_volume);
                double tp = CalculateTP();
                if (Open(lots, tp, engine)) {
                    lowest_buy_ = current_ask_;
                }
            }
        }
    }
};

TestResult RunTest(const TestConfig& cfg) {
    const auto& ticks = (cfg.year == "2024") ? g_ticks_2024 : g_ticks_2025;
    std::string start_date = (cfg.year == "2024") ? "2024.01.01" : "2025.01.01";
    std::string end_date = (cfg.year == "2024") ? "2024.12.31" : "2026.01.27";

    TickBacktestConfig config;
    config.symbol = "XAUUSD";
    config.initial_balance = 10000.0;
    config.contract_size = 100.0;
    config.leverage = 500.0;
    config.pip_size = 0.01;
    config.swap_long = -68.25;
    config.swap_short = 35.06;
    config.swap_mode = 1;
    config.swap_3days = 3;
    config.start_date = start_date;
    config.end_date = end_date;
    config.verbose = false;  // IMPORTANT: Disable verbose for parallel testing

    TickDataConfig tick_cfg;
    tick_cfg.format = TickDataFormat::MT5_CSV;
    config.tick_data_config = tick_cfg;

    TickBasedEngine engine(config);
    int peak_positions = 0;

    switch (cfg.strategy) {
        case STRAT_V4: {
            FillUpStrategyV4::Config v4cfg;
            v4cfg.spacing = 1.5;
            v4cfg.min_volume = 0.01;
            v4cfg.max_volume = 10.0;
            v4cfg.contract_size = 100.0;
            v4cfg.leverage = 500.0;
            FillUpStrategyV4Forced strategy(v4cfg, cfg.force_min_volume_entry);
            engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
            peak_positions = strategy.GetMaxNumberOfOpen();
            break;
        }
        case STRAT_V5: {
            FillUpStrategyV5::Config v5cfg;
            v5cfg.survive_pct = 13.0;
            v5cfg.spacing = 1.5;
            v5cfg.min_volume = 0.01;
            v5cfg.max_volume = 10.0;
            v5cfg.contract_size = 100.0;
            v5cfg.leverage = 500.0;
            v5cfg.ma_period = 11000;
            v5cfg.tp_multiplier = 2.0;
            FillUpStrategyV5Forced strategy(v5cfg, cfg.force_min_volume_entry);
            engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
            peak_positions = strategy.GetMaxNumberOfOpen();
            break;
        }
        case STRAT_COMBINED_JU: {
            StrategyCombinedJu::Config jucfg;
            jucfg.survive_pct = 13.0;
            jucfg.base_spacing = 1.5;
            jucfg.enable_velocity_filter = true;
            jucfg.tp_mode = StrategyCombinedJu::SQRT;
            jucfg.sizing_mode = StrategyCombinedJu::UNIFORM;
            StrategyCombinedJuForced strategy(jucfg, cfg.force_min_volume_entry);
            engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
            peak_positions = strategy.GetMaxPositions();
            break;
        }
        case STRAT_COMBINED_JU_NO_VELOCITY: {
            StrategyCombinedJu::Config jucfg;
            jucfg.survive_pct = 13.0;
            jucfg.base_spacing = 1.5;
            jucfg.enable_velocity_filter = false;  // Disabled
            jucfg.tp_mode = StrategyCombinedJu::SQRT;
            jucfg.sizing_mode = StrategyCombinedJu::UNIFORM;
            StrategyCombinedJuForced strategy(jucfg, cfg.force_min_volume_entry);
            engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
            peak_positions = strategy.GetMaxPositions();
            break;
        }
        case STRAT_COMBINED_JU_LINEAR_TP: {
            StrategyCombinedJu::Config jucfg;
            jucfg.survive_pct = 13.0;
            jucfg.base_spacing = 1.5;
            jucfg.enable_velocity_filter = true;
            jucfg.tp_mode = StrategyCombinedJu::LINEAR;  // Linear TP
            jucfg.sizing_mode = StrategyCombinedJu::UNIFORM;
            StrategyCombinedJuForced strategy(jucfg, cfg.force_min_volume_entry);
            engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
            peak_positions = strategy.GetMaxPositions();
            break;
        }
        case STRAT_COMBINED_JU_BARBELL: {
            StrategyCombinedJu::Config jucfg;
            jucfg.survive_pct = 13.0;
            jucfg.base_spacing = 1.5;
            jucfg.enable_velocity_filter = true;
            jucfg.tp_mode = StrategyCombinedJu::SQRT;
            jucfg.sizing_mode = StrategyCombinedJu::THRESHOLD_SIZING;  // Barbell
            jucfg.sizing_threshold_pos = 5;
            jucfg.sizing_threshold_mult = 2.0;
            StrategyCombinedJuForced strategy(jucfg, cfg.force_min_volume_entry);
            engine.RunWithTicks(ticks, [&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
            peak_positions = strategy.GetMaxPositions();
            break;
        }
    }

    auto results = engine.GetResults();

    TestResult result;
    result.name = cfg.name;
    result.year = cfg.year;
    result.final_balance = results.final_balance;
    result.return_multiple = results.final_balance / 10000.0;
    result.max_dd_pct = results.max_drawdown_pct;
    result.total_trades = results.total_trades;
    result.peak_positions = peak_positions;
    result.completed = true;

    return result;
}

void Worker() {
    while (true) {
        TestConfig task;
        {
            std::lock_guard<std::mutex> lock(g_queue_mutex);
            if (g_work_queue.empty()) return;
            task = g_work_queue.front();
            g_work_queue.pop();
        }

        TestResult result = RunTest(task);

        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_results.push_back(result);
        }

        int done = ++g_completed;
        std::cout << "\r  [" << done << "/" << g_total_tasks << "] "
                  << task.name << " " << task.year << ": "
                  << std::fixed << std::setprecision(2) << result.return_multiple << "x"
                  << std::string(20, ' ') << std::flush;
    }
}

int main() {
    std::cout << std::string(100, '=') << std::endl;
    std::cout << "V4, V5, AND COMBINED_JU FORCED ENTRY TEST" << std::endl;
    std::cout << "Testing forced entry across different strategies" << std::endl;
    std::cout << std::string(100, '=') << std::endl << std::endl;

    LoadTickData();

    // Build test configurations
    std::vector<std::pair<std::string, StrategyType>> strategies = {
        {"V4", STRAT_V4},
        {"V5", STRAT_V5},
        {"JU_FULL", STRAT_COMBINED_JU},
        {"JU_NO_VEL", STRAT_COMBINED_JU_NO_VELOCITY},
        {"JU_LINEAR", STRAT_COMBINED_JU_LINEAR_TP},
        {"JU_BARBELL", STRAT_COMBINED_JU_BARBELL}
    };

    for (const auto& strat : strategies) {
        for (bool force : {false, true}) {
            for (const std::string& year : {"2024", "2025"}) {
                TestConfig cfg;
                cfg.name = strat.first + (force ? "_FORCE" : "_NOFORCE");
                cfg.strategy = strat.second;
                cfg.force_min_volume_entry = force;
                cfg.year = year;
                g_work_queue.push(cfg);
            }
        }
    }

    g_total_tasks = g_work_queue.size();
    std::cout << "\nRunning " << g_total_tasks << " tests in parallel..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    unsigned int num_threads = std::thread::hardware_concurrency();
    std::cout << "Using " << num_threads << " threads" << std::endl;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(Worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();

    std::cout << "\n\nCompleted in " << duration << "s ("
              << std::fixed << std::setprecision(2)
              << (double)duration / g_total_tasks << "s/config)" << std::endl;

    // Sort results by strategy then force then year
    std::sort(g_results.begin(), g_results.end(), [](const TestResult& a, const TestResult& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.year < b.year;
    });

    // Print results table
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "RESULTS SUMMARY" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    std::cout << std::left << std::setw(25) << "Config"
              << std::right << std::setw(8) << "Year"
              << std::setw(12) << "Return"
              << std::setw(12) << "MaxDD"
              << std::setw(12) << "Trades"
              << std::setw(12) << "PeakPos"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    for (const auto& r : g_results) {
        std::cout << std::left << std::setw(25) << r.name
                  << std::right << std::setw(8) << r.year
                  << std::setw(11) << std::fixed << std::setprecision(2) << r.return_multiple << "x"
                  << std::setw(11) << r.max_dd_pct << "%"
                  << std::setw(12) << r.total_trades
                  << std::setw(12) << r.peak_positions
                  << std::endl;
    }

    // Forced entry comparison
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "FORCED ENTRY IMPACT BY STRATEGY" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    std::cout << std::left << std::setw(15) << "Strategy"
              << std::right << std::setw(12) << "Year"
              << std::setw(15) << "NoForce"
              << std::setw(15) << "Force"
              << std::setw(15) << "Change"
              << std::setw(15) << "NoForce DD"
              << std::setw(15) << "Force DD"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    for (const auto& strat : strategies) {
        for (const std::string& year : {"2024", "2025"}) {
            TestResult* noforce = nullptr;
            TestResult* force = nullptr;
            for (auto& r : g_results) {
                if (r.name == strat.first + "_NOFORCE" && r.year == year) noforce = &r;
                if (r.name == strat.first + "_FORCE" && r.year == year) force = &r;
            }
            if (noforce && force) {
                double change = (force->return_multiple / noforce->return_multiple - 1) * 100;
                std::cout << std::left << std::setw(15) << strat.first
                          << std::right << std::setw(12) << year
                          << std::setw(14) << std::fixed << std::setprecision(2) << noforce->return_multiple << "x"
                          << std::setw(14) << force->return_multiple << "x"
                          << std::setw(13) << std::showpos << change << "%" << std::noshowpos
                          << std::setw(14) << noforce->max_dd_pct << "%"
                          << std::setw(14) << force->max_dd_pct << "%"
                          << std::endl;
            }
        }
    }

    // 2-year sequential analysis
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "2-YEAR SEQUENTIAL ANALYSIS" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    for (const auto& strat : strategies) {
        for (bool force : {false, true}) {
            std::string name = strat.first + (force ? "_FORCE" : "_NOFORCE");
            TestResult* r2024 = nullptr;
            TestResult* r2025 = nullptr;
            for (auto& r : g_results) {
                if (r.name == name && r.year == "2024") r2024 = &r;
                if (r.name == name && r.year == "2025") r2025 = &r;
            }
            if (r2024 && r2025) {
                double two_year = r2024->return_multiple * r2025->return_multiple;
                double ratio = r2025->return_multiple / r2024->return_multiple;
                std::cout << std::left << std::setw(25) << name
                          << " 2024:" << std::setw(8) << std::fixed << std::setprecision(2) << r2024->return_multiple << "x"
                          << " 2025:" << std::setw(8) << r2025->return_multiple << "x"
                          << " 2yr:" << std::setw(10) << two_year << "x"
                          << " ratio:" << std::setw(8) << ratio << "x"
                          << std::endl;
            }
        }
    }

    // Best overall
    std::cout << "\n" << std::string(120, '=') << std::endl;
    std::cout << "BEST CONFIGURATIONS" << std::endl;
    std::cout << std::string(120, '=') << std::endl;

    double best_2yr = 0;
    std::string best_2yr_name;
    for (const auto& strat : strategies) {
        for (bool force : {false, true}) {
            std::string name = strat.first + (force ? "_FORCE" : "_NOFORCE");
            TestResult* r2024 = nullptr;
            TestResult* r2025 = nullptr;
            for (auto& r : g_results) {
                if (r.name == name && r.year == "2024") r2024 = &r;
                if (r.name == name && r.year == "2025") r2025 = &r;
            }
            if (r2024 && r2025) {
                double two_year = r2024->return_multiple * r2025->return_multiple;
                if (two_year > best_2yr) {
                    best_2yr = two_year;
                    best_2yr_name = name;
                }
            }
        }
    }
    std::cout << "Best 2-year sequential: " << best_2yr_name << " = " << best_2yr << "x" << std::endl;

    return 0;
}
