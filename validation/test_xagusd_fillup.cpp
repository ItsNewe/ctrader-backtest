/**
 * XAGUSD FillUpOscillation Parameter Sweep
 *
 * Tests whether ANY parameter combination can be profitable on silver.
 *
 * Challenges with XAGUSD:
 * - contract_size = 5000 (vs 100 for gold)
 * - swap_long = -25.44 points, swap_short = +13.72
 * - Daily swap at 0.01 lots = -25.44 * 0.01 * 5000 * 0.01 = -$12.72/day (devastating)
 * - Price ~$28-35 (vs $2600-4300 for gold)
 *
 * Tests:
 * Part 1: FillUpOscillation ADAPTIVE_SPACING (BUY grid) parameter sweep
 * Part 2: Custom SELL-grid strategy (earns positive swap)
 */

#include "../include/fill_up_oscillation.h"
#include "../include/tick_based_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <algorithm>
#include <cfloat>
#include <cmath>

using namespace backtest;

// ============================================================================
// Custom Short-Grid Strategy for XAGUSD
// Opens SELL positions when price rises, TP when price drops back.
// Earns positive swap (+13.72 points/lot/day) while waiting.
// ============================================================================
class ShortGridStrategy {
public:
    ShortGridStrategy(double survive_pct, double base_spacing,
                      double min_volume, double max_volume,
                      double contract_size, double leverage,
                      double volatility_lookback_hours = 4.0)
        : survive_pct_(survive_pct),
          base_spacing_(base_spacing),
          min_volume_(min_volume),
          max_volume_(max_volume),
          contract_size_(contract_size),
          leverage_(leverage),
          volatility_lookback_hours_(volatility_lookback_hours),
          highest_sell_(DBL_MIN),
          lowest_sell_(DBL_MAX),
          volume_of_open_trades_(0.0),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          peak_equity_(0.0),
          current_spacing_(base_spacing),
          recent_high_(0.0),
          recent_low_(DBL_MAX),
          ticks_processed_(0)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();
        ticks_processed_++;

        if (peak_equity_ == 0.0) {
            peak_equity_ = current_equity_;
        }
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }

        // Update volatility tracking for adaptive spacing
        UpdateVolatility();

        // Update adaptive spacing
        UpdateAdaptiveSpacing();

        // Track open positions
        Iterate(engine);

        // Open new positions
        OpenNew(engine);
    }

    double GetCurrentSpacing() const { return current_spacing_; }

private:
    double survive_pct_;
    double base_spacing_;
    double min_volume_;
    double max_volume_;
    double contract_size_;
    double leverage_;
    double volatility_lookback_hours_;

    double highest_sell_;
    double lowest_sell_;
    double volume_of_open_trades_;

    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;
    double peak_equity_;

    double current_spacing_;
    double recent_high_;
    double recent_low_;
    long ticks_processed_;

    void UpdateVolatility() {
        long ticks_per_hour = 720000;
        double effective_lookback = volatility_lookback_hours_;
        long volatility_ticks = (long)(effective_lookback * ticks_per_hour);
        if (volatility_ticks > 0 && ticks_processed_ % volatility_ticks == 0) {
            recent_high_ = current_bid_;
            recent_low_ = current_bid_;
        }
        recent_high_ = std::max(recent_high_, current_bid_);
        recent_low_ = std::min(recent_low_, current_bid_);
    }

    void UpdateAdaptiveSpacing() {
        double range = recent_high_ - recent_low_;
        if (range > 0 && recent_high_ > 0 && current_bid_ > 0) {
            // For silver: typical 4h volatility = 0.5% of price
            // At $30: typical_vol = $0.15
            double typical_vol = current_bid_ * 0.005;
            double vol_ratio = range / typical_vol;
            vol_ratio = std::max(0.5, std::min(3.0, vol_ratio));

            double new_spacing = base_spacing_ * vol_ratio;
            // Clamp for silver scale (price ~$30, not $3000)
            double min_spacing = base_spacing_ * 0.5;
            double max_spacing = base_spacing_ * 3.0;
            new_spacing = std::max(min_spacing, std::min(max_spacing, new_spacing));

            if (std::abs(new_spacing - current_spacing_) > base_spacing_ * 0.1) {
                current_spacing_ = new_spacing;
            }
        }
    }

    void Iterate(TickBasedEngine& engine) {
        highest_sell_ = DBL_MIN;
        lowest_sell_ = DBL_MAX;
        volume_of_open_trades_ = 0.0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "SELL") {
                volume_of_open_trades_ += trade->lot_size;
                highest_sell_ = std::max(highest_sell_, trade->entry_price);
                lowest_sell_ = std::min(lowest_sell_, trade->entry_price);
            }
        }
    }

    double CalculateLotSize(TickBasedEngine& engine, int positions_total) {
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            used_margin += trade->lot_size * contract_size_ * trade->entry_price / leverage_;
        }

        double margin_stop_out = 20.0;

        // For SELL grid: survive_pct means price can rise by this % before ruin
        double end_price = (positions_total == 0)
            ? current_bid_ * ((100.0 + survive_pct_) / 100.0)
            : lowest_sell_ * ((100.0 + survive_pct_) / 100.0);

        double distance = end_price - current_bid_;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        // For SELL positions, loss occurs when price rises
        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * contract_size_;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = min_volume_;
        double d_equity = contract_size_ * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * contract_size_ / leverage_;

        double max_mult = max_volume_ / min_volume_;
        for (double mult = max_mult; mult >= 1.0; mult -= 0.1) {
            double test_equity = equity_at_target - mult * d_equity;
            double test_margin = used_margin + mult * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                trade_size = mult * min_volume_;
                break;
            }
        }

        return std::min(trade_size, max_volume_);
    }

    bool Open(double lots, TickBasedEngine& engine) {
        if (lots < min_volume_) return false;

        double final_lots = std::min(lots, max_volume_);
        final_lots = std::round(final_lots * 100.0) / 100.0;

        // SELL: entry at bid, TP below entry (price drops)
        double tp = current_bid_ - current_spread_ - current_spacing_;
        Trade* trade = engine.OpenMarketOrder("SELL", final_lots, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = engine.GetOpenPositions().size();

        if (positions_total == 0) {
            double lots = CalculateLotSize(engine, positions_total);
            if (Open(lots, engine)) {
                highest_sell_ = current_bid_;
                lowest_sell_ = current_bid_;
            }
        } else {
            // Open new sell when price rises by spacing (grid fills upward)
            if (highest_sell_ != DBL_MIN && current_bid_ >= highest_sell_ + current_spacing_) {
                double lots = CalculateLotSize(engine, positions_total);
                if (Open(lots, engine)) {
                    highest_sell_ = current_bid_;
                }
            }
            // Also open when price drops enough below lowest (grid fills downward)
            if (lowest_sell_ != DBL_MAX && current_bid_ <= lowest_sell_ - current_spacing_) {
                double lots = CalculateLotSize(engine, positions_total);
                if (Open(lots, engine)) {
                    lowest_sell_ = current_bid_;
                }
            }
        }
    }
};

// ============================================================================
// Modified FillUpOscillation for Silver
// The stock version clamps spacing to $0.50-$5.00 which is too wide for silver.
// This version uses appropriate silver-scale clamping.
// ============================================================================
class FillUpOscillationSilver {
public:
    FillUpOscillationSilver(double survive_pct, double base_spacing,
                            double min_volume, double max_volume,
                            double contract_size, double leverage,
                            double volatility_lookback_hours = 4.0)
        : survive_pct_(survive_pct),
          base_spacing_(base_spacing),
          min_volume_(min_volume),
          max_volume_(max_volume),
          contract_size_(contract_size),
          leverage_(leverage),
          volatility_lookback_hours_(volatility_lookback_hours),
          lowest_buy_(DBL_MAX),
          highest_buy_(DBL_MIN),
          volume_of_open_trades_(0.0),
          current_ask_(0.0),
          current_bid_(0.0),
          current_spread_(0.0),
          current_equity_(0.0),
          current_balance_(0.0),
          peak_equity_(0.0),
          current_spacing_(base_spacing),
          recent_high_(0.0),
          recent_low_(DBL_MAX),
          ticks_processed_(0)
    {
    }

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        current_ask_ = tick.ask;
        current_bid_ = tick.bid;
        current_spread_ = tick.spread();
        current_equity_ = engine.GetEquity();
        current_balance_ = engine.GetBalance();
        ticks_processed_++;

        if (peak_equity_ == 0.0) {
            peak_equity_ = current_equity_;
        }
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }

        UpdateVolatility();
        UpdateAdaptiveSpacing();
        Iterate(engine);
        OpenNew(engine);
    }

    double GetCurrentSpacing() const { return current_spacing_; }

private:
    double survive_pct_;
    double base_spacing_;
    double min_volume_;
    double max_volume_;
    double contract_size_;
    double leverage_;
    double volatility_lookback_hours_;

    double lowest_buy_;
    double highest_buy_;
    double volume_of_open_trades_;

    double current_ask_;
    double current_bid_;
    double current_spread_;
    double current_equity_;
    double current_balance_;
    double peak_equity_;

    double current_spacing_;
    double recent_high_;
    double recent_low_;
    long ticks_processed_;

    void UpdateVolatility() {
        long ticks_per_hour = 720000;
        double effective_lookback = volatility_lookback_hours_;
        long volatility_ticks = (long)(effective_lookback * ticks_per_hour);
        if (volatility_ticks > 0 && ticks_processed_ % volatility_ticks == 0) {
            recent_high_ = current_bid_;
            recent_low_ = current_bid_;
        }
        recent_high_ = std::max(recent_high_, current_bid_);
        recent_low_ = std::min(recent_low_, current_bid_);
    }

    void UpdateAdaptiveSpacing() {
        double range = recent_high_ - recent_low_;
        if (range > 0 && recent_high_ > 0 && current_bid_ > 0) {
            // For silver at ~$30: typical 4h vol = 0.5% = $0.15
            double typical_vol = current_bid_ * 0.005;
            double vol_ratio = range / typical_vol;
            vol_ratio = std::max(0.5, std::min(3.0, vol_ratio));

            double new_spacing = base_spacing_ * vol_ratio;
            // Silver-appropriate clamping: min = base*0.5, max = base*3.0
            double min_spacing = base_spacing_ * 0.5;
            double max_spacing = base_spacing_ * 3.0;
            new_spacing = std::max(min_spacing, std::min(max_spacing, new_spacing));

            if (std::abs(new_spacing - current_spacing_) > base_spacing_ * 0.1) {
                current_spacing_ = new_spacing;
            }
        }
    }

    void Iterate(TickBasedEngine& engine) {
        lowest_buy_ = DBL_MAX;
        highest_buy_ = DBL_MIN;
        volume_of_open_trades_ = 0.0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                volume_of_open_trades_ += trade->lot_size;
                lowest_buy_ = std::min(lowest_buy_, trade->entry_price);
                highest_buy_ = std::max(highest_buy_, trade->entry_price);
            }
        }
    }

    double CalculateLotSize(TickBasedEngine& engine, int positions_total) {
        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            used_margin += trade->lot_size * contract_size_ * trade->entry_price / leverage_;
        }

        double margin_stop_out = 20.0;

        double end_price = (positions_total == 0)
            ? current_ask_ * ((100.0 - survive_pct_) / 100.0)
            : highest_buy_ * ((100.0 - survive_pct_) / 100.0);

        double distance = current_ask_ - end_price;
        double number_of_trades = std::floor(distance / current_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = current_equity_ - volume_of_open_trades_ * distance * contract_size_;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = min_volume_;
        double d_equity = contract_size_ * trade_size * current_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
        double d_margin = number_of_trades * trade_size * contract_size_ / leverage_;

        double max_mult = max_volume_ / min_volume_;
        for (double mult = max_mult; mult >= 1.0; mult -= 0.1) {
            double test_equity = equity_at_target - mult * d_equity;
            double test_margin = used_margin + mult * d_margin;
            if (test_margin > 0 && (test_equity / test_margin * 100.0) > margin_stop_out) {
                trade_size = mult * min_volume_;
                break;
            }
        }

        return std::min(trade_size, max_volume_);
    }

    bool Open(double lots, TickBasedEngine& engine) {
        if (lots < min_volume_) return false;

        double final_lots = std::min(lots, max_volume_);
        final_lots = std::round(final_lots * 100.0) / 100.0;

        double tp = current_ask_ + current_spread_ + current_spacing_;
        Trade* trade = engine.OpenMarketOrder("BUY", final_lots, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(TickBasedEngine& engine) {
        int positions_total = engine.GetOpenPositions().size();

        if (positions_total == 0) {
            double lots = CalculateLotSize(engine, positions_total);
            if (Open(lots, engine)) {
                highest_buy_ = current_ask_;
                lowest_buy_ = current_ask_;
            }
        } else {
            if (lowest_buy_ != DBL_MAX && lowest_buy_ >= current_ask_ + current_spacing_) {
                double lots = CalculateLotSize(engine, positions_total);
                if (Open(lots, engine)) {
                    lowest_buy_ = current_ask_;
                }
            } else if (highest_buy_ != DBL_MIN && highest_buy_ <= current_ask_ - current_spacing_) {
                double lots = CalculateLotSize(engine, positions_total);
                if (Open(lots, engine)) {
                    highest_buy_ = current_ask_;
                }
            }
        }
    }
};

// ============================================================================
// Result structure
// ============================================================================
struct TestResult {
    std::string direction;  // "LONG" or "SHORT"
    double survive_pct;
    double base_spacing;
    double lookback_hours;
    double final_balance;
    double return_multiple;
    double max_dd_pct;
    size_t total_trades;
    double total_swap;
    bool stopped_out;
};

// ============================================================================
// Run a single LONG grid test
// ============================================================================
TestResult RunLongTest(double survive_pct, double base_spacing, double lookback_hours) {
    TestResult result;
    result.direction = "LONG";
    result.survive_pct = survive_pct;
    result.base_spacing = base_spacing;
    result.lookback_hours = lookback_hours;

    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

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
    config.end_date = "2025.12.30";
    config.verbose = false;
    config.tick_data_config = tick_config;

    try {
        TickBasedEngine engine(config);

        FillUpOscillationSilver strategy(
            survive_pct,
            base_spacing,
            0.01,     // min_volume
            10.0,     // max_volume
            5000.0,   // contract_size
            500.0,    // leverage
            lookback_hours
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        result.final_balance = results.final_balance;
        result.return_multiple = results.final_balance / config.initial_balance;
        result.max_dd_pct = (results.max_drawdown / std::max(1.0, results.final_balance + results.max_drawdown)) * 100.0;
        // Use the engine's tracked max DD percent
        if (results.max_drawdown > 0) {
            // max_drawdown is in dollars; peak was at (final_balance + max_drawdown) approximately
            // Actually the engine tracks max_drawdown_percent_ internally, but it's not exposed directly
            // We'll calculate from the dollar DD and assume peak was close to (initial + profit + DD)
            double approx_peak = results.final_balance + results.max_drawdown;
            result.max_dd_pct = (results.max_drawdown / approx_peak) * 100.0;
        }
        result.total_trades = results.total_trades;
        result.total_swap = results.total_swap_charged;
        result.stopped_out = engine.IsStopOutOccurred();

    } catch (const std::exception& e) {
        result.final_balance = 0.0;
        result.return_multiple = 0.0;
        result.max_dd_pct = 100.0;
        result.total_trades = 0;
        result.total_swap = 0.0;
        result.stopped_out = true;
    }

    return result;
}

// ============================================================================
// Run a single SHORT grid test
// ============================================================================
TestResult RunShortTest(double survive_pct, double base_spacing, double lookback_hours) {
    TestResult result;
    result.direction = "SHORT";
    result.survive_pct = survive_pct;
    result.base_spacing = base_spacing;
    result.lookback_hours = lookback_hours;

    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\XAGUSD\\XAGUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

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
    config.end_date = "2025.12.30";
    config.verbose = false;
    config.tick_data_config = tick_config;

    try {
        TickBasedEngine engine(config);

        ShortGridStrategy strategy(
            survive_pct,
            base_spacing,
            0.01,     // min_volume
            10.0,     // max_volume
            5000.0,   // contract_size
            500.0,    // leverage
            lookback_hours
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        result.final_balance = results.final_balance;
        result.return_multiple = results.final_balance / config.initial_balance;
        if (results.max_drawdown > 0) {
            double approx_peak = results.final_balance + results.max_drawdown;
            result.max_dd_pct = (results.max_drawdown / approx_peak) * 100.0;
        } else {
            result.max_dd_pct = 0.0;
        }
        result.total_trades = results.total_trades;
        result.total_swap = results.total_swap_charged;
        result.stopped_out = engine.IsStopOutOccurred();

    } catch (const std::exception& e) {
        result.final_balance = 0.0;
        result.return_multiple = 0.0;
        result.max_dd_pct = 100.0;
        result.total_trades = 0;
        result.total_swap = 0.0;
        result.stopped_out = true;
    }

    return result;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "  XAGUSD FillUpOscillation Parameter Sweep" << std::endl;
    std::cout << "  Data: XAGUSD_TICKS_2025.csv" << std::endl;
    std::cout << "  Period: 2025.01.01 - 2025.12.30" << std::endl;
    std::cout << "  Initial Balance: $10,000" << std::endl;
    std::cout << "  Contract Size: 5000, Leverage: 500" << std::endl;
    std::cout << "  Swap Long: -25.44 pts, Swap Short: +13.72 pts" << std::endl;
    std::cout << "  Expected daily swap (0.01 lots long): -$12.72" << std::endl;
    std::cout << "================================================================" << std::endl;

    // Parameter sweep values
    std::vector<double> survive_pcts = {5.0, 10.0, 15.0, 20.0, 30.0, 50.0};
    std::vector<double> spacings = {0.01, 0.02, 0.05, 0.10, 0.20, 0.50};
    std::vector<double> lookbacks = {1.0, 4.0};

    std::vector<TestResult> all_results;

    // ========================================================================
    // Part 1: LONG Grid Sweep (FillUpOscillationSilver - Adaptive Spacing)
    // ========================================================================
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "  PART 1: LONG GRID (BUY) - Adaptive Spacing Mode" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    std::cout << std::endl;
    std::cout << std::left
              << std::setw(8) << "Surv%"
              << std::setw(10) << "Spacing"
              << std::setw(8) << "LB(h)"
              << std::setw(14) << "FinalBal"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Trades"
              << std::setw(14) << "Swap"
              << std::setw(10) << "Status"
              << std::endl;
    std::cout << std::string(94, '-') << std::endl;

    int test_num = 0;
    int total_long_tests = survive_pcts.size() * spacings.size() * lookbacks.size();

    for (double survive_pct : survive_pcts) {
        for (double spacing : spacings) {
            for (double lookback : lookbacks) {
                test_num++;
                std::cerr << "\rLONG test " << test_num << "/" << total_long_tests
                          << " (s=" << survive_pct << "%, sp=$" << spacing
                          << ", lb=" << lookback << "h)   " << std::flush;

                TestResult result = RunLongTest(survive_pct, spacing, lookback);
                all_results.push_back(result);

                std::cout << std::left << std::fixed
                          << std::setw(8) << std::setprecision(0) << survive_pct
                          << "$" << std::setw(9) << std::setprecision(2) << spacing
                          << std::setw(8) << std::setprecision(0) << lookback
                          << "$" << std::setw(13) << std::setprecision(2) << result.final_balance
                          << std::setw(10) << std::setprecision(3) << result.return_multiple
                          << std::setw(10) << std::setprecision(1) << result.max_dd_pct
                          << std::setw(10) << result.total_trades
                          << "$" << std::setw(13) << std::setprecision(2) << result.total_swap
                          << (result.stopped_out ? "STOPPED" : "OK")
                          << std::endl;
            }
        }
    }
    std::cerr << "\r" << std::string(80, ' ') << "\r" << std::flush;

    // ========================================================================
    // Part 2: SHORT Grid Sweep (Custom ShortGridStrategy - earns positive swap)
    // ========================================================================
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "  PART 2: SHORT GRID (SELL) - Earns Positive Swap" << std::endl;
    std::cout << "  Expected daily swap (0.01 lots short): +$6.86/day" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    std::cout << std::endl;
    std::cout << std::left
              << std::setw(8) << "Surv%"
              << std::setw(10) << "Spacing"
              << std::setw(8) << "LB(h)"
              << std::setw(14) << "FinalBal"
              << std::setw(10) << "Return"
              << std::setw(10) << "MaxDD%"
              << std::setw(10) << "Trades"
              << std::setw(14) << "Swap"
              << std::setw(10) << "Status"
              << std::endl;
    std::cout << std::string(94, '-') << std::endl;

    test_num = 0;
    int total_short_tests = survive_pcts.size() * spacings.size() * lookbacks.size();

    for (double survive_pct : survive_pcts) {
        for (double spacing : spacings) {
            for (double lookback : lookbacks) {
                test_num++;
                std::cerr << "\rSHORT test " << test_num << "/" << total_short_tests
                          << " (s=" << survive_pct << "%, sp=$" << spacing
                          << ", lb=" << lookback << "h)   " << std::flush;

                TestResult result = RunShortTest(survive_pct, spacing, lookback);
                all_results.push_back(result);

                std::cout << std::left << std::fixed
                          << std::setw(8) << std::setprecision(0) << survive_pct
                          << "$" << std::setw(9) << std::setprecision(2) << spacing
                          << std::setw(8) << std::setprecision(0) << lookback
                          << "$" << std::setw(13) << std::setprecision(2) << result.final_balance
                          << std::setw(10) << std::setprecision(3) << result.return_multiple
                          << std::setw(10) << std::setprecision(1) << result.max_dd_pct
                          << std::setw(10) << result.total_trades
                          << "$" << std::setw(13) << std::setprecision(2) << result.total_swap
                          << (result.stopped_out ? "STOPPED" : "OK")
                          << std::endl;
            }
        }
    }
    std::cerr << "\r" << std::string(80, ' ') << "\r" << std::flush;

    // ========================================================================
    // Summary: Top 10 results by return (profitable only)
    // ========================================================================
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "  SUMMARY: TOP 10 BY RETURN (all configs)" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    // Sort by return multiple descending
    std::sort(all_results.begin(), all_results.end(),
              [](const TestResult& a, const TestResult& b) {
                  return a.return_multiple > b.return_multiple;
              });

    std::cout << std::endl;
    std::cout << std::left
              << std::setw(7) << "Dir"
              << std::setw(7) << "Surv%"
              << std::setw(9) << "Spacing"
              << std::setw(7) << "LB(h)"
              << std::setw(14) << "FinalBal"
              << std::setw(9) << "Return"
              << std::setw(9) << "MaxDD%"
              << std::setw(9) << "Trades"
              << std::setw(12) << "Swap"
              << std::setw(8) << "Status"
              << std::endl;
    std::cout << std::string(91, '-') << std::endl;

    int shown = 0;
    for (const auto& r : all_results) {
        if (shown >= 10) break;
        std::cout << std::left << std::fixed
                  << std::setw(7) << r.direction
                  << std::setw(7) << std::setprecision(0) << r.survive_pct
                  << "$" << std::setw(8) << std::setprecision(2) << r.base_spacing
                  << std::setw(7) << std::setprecision(0) << r.lookback_hours
                  << "$" << std::setw(13) << std::setprecision(2) << r.final_balance
                  << std::setw(9) << std::setprecision(3) << r.return_multiple
                  << std::setw(9) << std::setprecision(1) << r.max_dd_pct
                  << std::setw(9) << r.total_trades
                  << "$" << std::setw(11) << std::setprecision(2) << r.total_swap
                  << (r.stopped_out ? "STOPPED" : "OK")
                  << std::endl;
        shown++;
    }

    // ========================================================================
    // Statistics
    // ========================================================================
    int profitable_long = 0, profitable_short = 0;
    int stopped_long = 0, stopped_short = 0;
    double best_long_return = 0.0, best_short_return = 0.0;

    for (const auto& r : all_results) {
        if (r.direction == "LONG") {
            if (r.return_multiple > 1.0 && !r.stopped_out) profitable_long++;
            if (r.stopped_out) stopped_long++;
            if (r.return_multiple > best_long_return) best_long_return = r.return_multiple;
        } else {
            if (r.return_multiple > 1.0 && !r.stopped_out) profitable_short++;
            if (r.stopped_out) stopped_short++;
            if (r.return_multiple > best_short_return) best_short_return = r.return_multiple;
        }
    }

    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "  OVERALL STATISTICS" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "LONG  configs: " << total_long_tests << " total, "
              << profitable_long << " profitable, "
              << stopped_long << " stopped out" << std::endl;
    std::cout << "  Best LONG return: " << best_long_return << "x" << std::endl;
    std::cout << "SHORT configs: " << total_short_tests << " total, "
              << profitable_short << " profitable, "
              << stopped_short << " stopped out" << std::endl;
    std::cout << "  Best SHORT return: " << best_short_return << "x" << std::endl;
    std::cout << std::endl;

    if (profitable_long == 0 && profitable_short == 0) {
        std::cout << "CONCLUSION: NO profitable configuration found for XAGUSD." << std::endl;
        std::cout << "  The devastating swap costs (-$12.72/day per 0.01 lots long)" << std::endl;
        std::cout << "  and high contract_size (5000) make grid trading unviable on silver." << std::endl;
    } else {
        std::cout << "CONCLUSION: " << (profitable_long + profitable_short)
                  << " profitable configuration(s) found!" << std::endl;
        if (profitable_short > profitable_long) {
            std::cout << "  SHORT grid benefits from positive swap (+$6.86/day per 0.01 lots)" << std::endl;
        }
    }

    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "  SWAP ANALYSIS" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    std::cout << "  Swap Mode 1 (Points): swap_pts * 0.01 * contract_size * lot_size" << std::endl;
    std::cout << "  LONG  swap per day at 0.01 lots: -25.44 * 0.01 * 5000 * 0.01 = -$12.72" << std::endl;
    std::cout << "  SHORT swap per day at 0.01 lots: +13.72 * 0.01 * 5000 * 0.01 = +$6.86" << std::endl;
    std::cout << "  Annual LONG swap (0.01 lots): -$12.72 * 365 = -$4,643" << std::endl;
    std::cout << "  Annual SHORT swap (0.01 lots): +$6.86 * 365 = +$2,504" << std::endl;
    std::cout << "  (With triple-swap Wed: multiply by 7/5 for weekly average)" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    return 0;
}
