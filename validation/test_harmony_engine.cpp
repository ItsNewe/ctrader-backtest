/**
 * Harmony Strategy using TickBasedEngine
 *
 * Uses the EXACT MT5 lot sizing formula and proper swap handling via TickBasedEngine.
 * Tests "up while up" (no TP) and "up while down" (no TP) strategies.
 */

#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cfloat>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

using namespace backtest;

/**
 * UpWhileUp Strategy - MT5 Accurate with TickBasedEngine
 * Opens BUY at every new high, no TP (positions held forever)
 */
class UpWhileUpEngine {
public:
    struct Config {
        double survive_pct = 12.0;
        double min_volume = 0.01;
        double max_volume = 100.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        double margin_stop_out_level = 20.0;
        double initial_margin_rate = 1.0;
    };

    struct Stats {
        int entries = 0;
        double total_volume = 0.0;
        double max_volume = 0.0;
    };

private:
    Config config_;
    Stats stats_;
    double checked_last_open_price_ = DBL_MIN;
    double volume_of_open_trades_ = 0.0;

public:
    UpWhileUpEngine(const Config& config) : config_(config) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double current_ask = tick.ask;
        double spread = tick.ask - tick.bid;

        // Scan positions to get current state
        volume_of_open_trades_ = 0.0;
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                volume_of_open_trades_ += trade->lot_size;
                used_margin += trade->lot_size * config_.contract_size * current_ask / config_.leverage * config_.initial_margin_rate;
                checked_last_open_price_ = std::max(checked_last_open_price_, trade->entry_price);
            }
        }
        stats_.max_volume = std::max(stats_.max_volume, volume_of_open_trades_);

        // Only open if price is at new high
        if (volume_of_open_trades_ == 0.0 || checked_last_open_price_ < current_ask) {
            checked_last_open_price_ = current_ask;

            double equity = engine.GetEquity();
            double current_margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;

            double equity_at_target = equity * config_.margin_stop_out_level / current_margin_level;
            double equity_difference = equity - equity_at_target;
            double price_difference = (volume_of_open_trades_ > 0)
                ? equity_difference / (volume_of_open_trades_ * config_.contract_size)
                : 0.0;

            double end_price = current_ask * ((100.0 - config_.survive_pct) / 100.0);
            double distance = current_ask - end_price;

            // MT5 safety check: only open if we can survive the drop
            if (volume_of_open_trades_ == 0.0 || (current_ask - price_difference) < end_price) {
                double lots = CalculateLotSize(equity, used_margin, current_ask, distance, spread);

                if (lots >= config_.min_volume) {
                    // No TP - positions held forever
                    Trade* trade = engine.OpenMarketOrder("BUY", lots, 0.0, 0.0);
                    if (trade) {
                        volume_of_open_trades_ += lots;
                        stats_.entries++;
                        stats_.total_volume += lots;
                    }
                }
            }
        }
    }

    double CalculateLotSize(double equity, double used_margin, double current_ask,
                            double distance, double spread) {
        // MT5 CFDLEVERAGE formula
        double numerator = 100.0 * equity * config_.leverage
                         - 100.0 * config_.contract_size * distance * volume_of_open_trades_ * config_.leverage
                         - config_.leverage * config_.margin_stop_out_level * used_margin;

        double denominator = config_.contract_size * (
            100.0 * distance * config_.leverage
            + 100.0 * spread * config_.leverage
            + current_ask * config_.initial_margin_rate * config_.margin_stop_out_level
        );

        if (denominator <= 0) return 0.0;

        double lots = numerator / denominator;
        lots = std::max(0.0, lots);
        lots = std::min(lots, config_.max_volume);
        lots = std::floor(lots * 100) / 100;

        return lots;
    }

    const Stats& GetStats() const { return stats_; }
};

/**
 * UpWhileDown Strategy - Opens as price drops, no TP
 */
class UpWhileDownEngine {
public:
    struct Config {
        double survive_pct = 12.0;
        double min_volume = 0.01;
        double max_volume = 100.0;
        double contract_size = 100.0;
        double leverage = 500.0;
        double margin_stop_out_level = 20.0;
        double initial_margin_rate = 1.0;
    };

    struct Stats {
        int entries = 0;
        double total_volume = 0.0;
        double max_volume = 0.0;
        double spacing = 0.0;
    };

private:
    Config config_;
    Stats stats_;
    double lowest_buy_ = DBL_MAX;
    double highest_buy_ = DBL_MIN;
    double volume_of_open_trades_ = 0.0;
    double trade_size_buy_ = 0.0;
    double spacing_buy_ = 0.0;
    int count_buy_ = 0;
    bool initialized_ = false;

public:
    UpWhileDownEngine(const Config& config) : config_(config) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        double spread = tick.ask - tick.bid;

        // Scan positions
        ScanPositions(engine, tick.ask);

        // Calculate sizing on first tick
        if (!initialized_) {
            CalculateSizing(tick, engine, spread);
            initialized_ = true;
        }

        // Open new positions
        OpenNew(tick, engine);
    }

    const Stats& GetStats() const { return stats_; }

private:
    void ScanPositions(TickBasedEngine& engine, double current_ask) {
        volume_of_open_trades_ = 0.0;
        count_buy_ = 0;
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                lowest_buy_ = std::min(lowest_buy_, trade->entry_price);
                highest_buy_ = std::max(highest_buy_, trade->entry_price);
                count_buy_++;
                volume_of_open_trades_ += trade->lot_size;
            }
        }
        stats_.max_volume = std::max(stats_.max_volume, volume_of_open_trades_);
    }

    void CalculateSizing(const Tick& tick, TickBasedEngine& engine, double spread) {
        double equity = engine.GetEquity();
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                used_margin += trade->lot_size * config_.contract_size * tick.ask / config_.leverage * config_.initial_margin_rate;
            }
        }

        double current_margin_level = (used_margin > 0) ? (equity / used_margin * 100.0) : 10000.0;
        double equity_at_target = equity * config_.margin_stop_out_level / current_margin_level;

        double end_price = tick.ask * ((100.0 - config_.survive_pct) / 100.0);
        double starting_price = tick.ask;
        double len = starting_price - end_price;

        if (current_margin_level > config_.margin_stop_out_level && len > 0) {
            double trade_size = config_.min_volume;

            double local_used_margin = ((trade_size * config_.contract_size * starting_price) / config_.leverage * config_.initial_margin_rate +
                                        (trade_size * config_.contract_size * end_price) / config_.leverage * config_.initial_margin_rate) / 2.0;

            double d_equity = config_.contract_size * trade_size * len + trade_size * spread * config_.contract_size;

            double number_of_trades;
            if (used_margin == 0) {
                number_of_trades = std::floor(equity_at_target / (config_.margin_stop_out_level / 100.0 * local_used_margin + d_equity));
            } else {
                number_of_trades = std::floor((equity_at_target - config_.margin_stop_out_level / 100.0 * used_margin)
                                              / (config_.margin_stop_out_level / 100.0 * local_used_margin + d_equity));
            }

            number_of_trades = std::max(1.0, number_of_trades);

            double proportion = number_of_trades / len;

            if (proportion >= 1.0) {
                spacing_buy_ = 1.0;
                trade_size_buy_ = std::floor(proportion) * config_.min_volume;
            } else {
                trade_size_buy_ = config_.min_volume;
                spacing_buy_ = std::round((len / number_of_trades) * 100.0) / 100.0;
            }

            stats_.spacing = spacing_buy_;
        }
    }

    void OpenNew(const Tick& tick, TickBasedEngine& engine) {
        double current_ask = tick.ask;

        if (count_buy_ == 0) {
            // First entry
            if (trade_size_buy_ >= config_.min_volume) {
                Trade* trade = engine.OpenMarketOrder("BUY", trade_size_buy_, 0.0, 0.0);
                if (trade) {
                    highest_buy_ = current_ask;
                    lowest_buy_ = current_ask;
                    count_buy_++;
                    volume_of_open_trades_ += trade_size_buy_;
                    stats_.entries++;
                    stats_.total_volume += trade_size_buy_;
                }
            }
        } else {
            // Only open if price dropped below lowest - spacing
            if (spacing_buy_ > 0 && lowest_buy_ != DBL_MAX && lowest_buy_ > current_ask + spacing_buy_) {
                if (trade_size_buy_ >= config_.min_volume) {
                    Trade* trade = engine.OpenMarketOrder("BUY", trade_size_buy_, 0.0, 0.0);
                    if (trade) {
                        lowest_buy_ = current_ask;
                        count_buy_++;
                        volume_of_open_trades_ += trade_size_buy_;
                        stats_.entries++;
                        stats_.total_volume += trade_size_buy_;
                    }
                }
            }
        }
    }
};

void PrintResults(const std::string& name, double survive_pct,
                  const TickBasedEngine::BacktestResults& results,
                  int entries, double total_vol, double max_vol,
                  double initial_balance) {
    double ret = results.final_balance / initial_balance;
    std::cout << std::left << std::setw(14) << name
              << std::setw(8) << survive_pct
              << std::setw(10) << (std::to_string(ret).substr(0, 6) + "x")
              << std::setw(10) << (std::to_string(results.max_drawdown_pct).substr(0, 5) + "%")
              << std::setw(10) << entries
              << std::setw(10) << std::fixed << std::setprecision(2) << total_vol
              << std::setw(10) << max_vol
              << std::setw(12) << ("$" + std::to_string((int)results.total_swap_charged))
              << std::endl;
}

int main() {
    std::cout << "=== Harmony Strategy with TickBasedEngine ===" << std::endl;
    std::cout << "Using MT5 lot sizing formula + proper swap handling" << std::endl;
    std::cout << std::endl;

    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    double initial_balance = 10000.0;

    TickBacktestConfig base_config;
    base_config.symbol = "XAUUSD";
    base_config.initial_balance = initial_balance;
    base_config.account_currency = "USD";
    base_config.contract_size = 100.0;
    base_config.leverage = 500.0;
    base_config.margin_rate = 1.0;
    base_config.pip_size = 0.01;
    base_config.swap_long = -66.99;
    base_config.swap_short = 41.2;
    base_config.swap_mode = 1;
    base_config.swap_3days = 3;
    base_config.tick_data_config = tick_config;
    base_config.verbose = false;

    std::vector<double> survive_values = {12.0, 13.0, 14.0, 15.0};

    std::cout << "=== Results ===" << std::endl;
    std::cout << std::left << std::setw(14) << "Strategy"
              << std::setw(8) << "Surv%"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD"
              << std::setw(10) << "Entries"
              << std::setw(10) << "TotalVol"
              << std::setw(10) << "MaxVol"
              << std::setw(12) << "SwapCost"
              << std::endl;
    std::cout << std::string(84, '-') << std::endl;

    for (double surv : survive_values) {
        // Test UpWhileUp
        {
            TickBacktestConfig config = base_config;
            TickBasedEngine engine(config);

            UpWhileUpEngine::Config strat_cfg;
            strat_cfg.survive_pct = surv;
            UpWhileUpEngine strategy(strat_cfg);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto results = engine.GetResults();
            auto stats = strategy.GetStats();
            PrintResults("UpWhileUp", surv, results, stats.entries, stats.total_volume, stats.max_volume, initial_balance);
        }

        // Test UpWhileDown
        {
            TickBacktestConfig config = base_config;
            TickBasedEngine engine(config);

            UpWhileDownEngine::Config strat_cfg;
            strat_cfg.survive_pct = surv;
            UpWhileDownEngine strategy(strat_cfg);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });

            auto results = engine.GetResults();
            auto stats = strategy.GetStats();
            PrintResults("UpWhileDown", surv, results, stats.entries, stats.total_volume, stats.max_volume, initial_balance);
        }

        std::cout << std::endl;
    }

    std::cout << "=== Notes ===" << std::endl;
    std::cout << "- TickBasedEngine charges swap daily (-$66.99/lot/day for XAUUSD BUY)" << std::endl;
    std::cout << "- MT5 Reference: UpWhileUp at 12% = 208x (may not charge swap same way)" << std::endl;
    std::cout << "- Positions have NO TP - held until end of test" << std::endl;

    return 0;
}
