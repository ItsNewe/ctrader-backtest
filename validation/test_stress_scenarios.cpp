/**
 * Stress Test Scenarios for Strategy Robustness Testing
 *
 * Tests FillUpOscillation strategy (survive=13%, spacing=1.5, lookback=1h) against:
 * 1. Flash crash: -10% in 1 hour, then 50% recovery in 2 hours
 * 2. Sustained crash: -30% over 5 days, no recovery
 * 3. V-shaped recovery: -20% in 2 days, +25% in 3 days
 * 4. Choppy decline: oscillating down -1% per day for 30 days
 */

#include "../include/fill_up_oscillation.h"
#include "../include/synthetic_tick_generator.h"
#include "../include/tick_data.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>
#include <sstream>

using namespace backtest;

/**
 * Simplified Synthetic Engine - mimics TickBasedEngine for synthetic tick data
 * Handles margin, equity, and position tracking without file I/O
 */
class SyntheticEngine {
public:
    SyntheticEngine(double initial_balance, double contract_size, double leverage)
        : balance_(initial_balance),
          equity_(initial_balance),
          initial_balance_(initial_balance),
          contract_size_(contract_size),
          leverage_(leverage),
          next_trade_id_(1),
          peak_equity_(initial_balance),
          max_drawdown_(0.0),
          max_drawdown_pct_(0.0),
          stop_out_occurred_(false) {}

    // Trading operations
    Trade* OpenMarketOrder(const std::string& direction, double lot_size,
                           double stop_loss = 0.0, double take_profit = 0.0) {
        Trade* trade = new Trade();
        trade->id = next_trade_id_++;
        trade->symbol = "XAUUSD";
        trade->direction = direction;
        trade->entry_price = (direction == "BUY") ? current_tick_.ask : current_tick_.bid;
        trade->entry_time = current_tick_.timestamp;
        trade->lot_size = lot_size;
        trade->stop_loss = stop_loss;
        trade->take_profit = take_profit;
        trade->commission = 0.0;

        open_positions_.push_back(trade);
        return trade;
    }

    bool ClosePosition(Trade* trade, const std::string& reason = "Manual") {
        if (!trade) return false;

        trade->exit_price = (trade->direction == "BUY") ? current_tick_.bid : current_tick_.ask;
        trade->exit_time = current_tick_.timestamp;
        trade->exit_reason = reason;

        // Calculate P/L
        double price_diff = trade->exit_price - trade->entry_price;
        if (trade->direction == "SELL") price_diff = -price_diff;
        trade->profit_loss = price_diff * trade->lot_size * contract_size_;

        balance_ += trade->profit_loss;
        closed_trades_.push_back(*trade);

        // Remove from open positions
        open_positions_.erase(
            std::remove(open_positions_.begin(), open_positions_.end(), trade),
            open_positions_.end()
        );

        delete trade;
        return true;
    }

    void ProcessTick(const Tick& tick) {
        current_tick_ = tick;
        UpdateEquity(tick);
        CheckMarginStopOut(tick);
        ProcessOpenPositions(tick);
    }

    // Getters
    double GetBalance() const { return balance_; }
    double GetEquity() const { return equity_; }
    const Tick& GetCurrentTick() const { return current_tick_; }
    const std::vector<Trade*>& GetOpenPositions() const { return open_positions_; }
    bool IsStopOutOccurred() const { return stop_out_occurred_; }
    double GetMaxDrawdown() const { return max_drawdown_; }
    double GetMaxDrawdownPercent() const { return max_drawdown_pct_; }
    double GetPeakEquity() const { return peak_equity_; }
    double GetInitialBalance() const { return initial_balance_; }

private:
    double balance_;
    double equity_;
    double initial_balance_;
    double contract_size_;
    double leverage_;
    int next_trade_id_;
    double peak_equity_;
    double max_drawdown_;
    double max_drawdown_pct_;
    bool stop_out_occurred_;

    Tick current_tick_;
    std::vector<Trade*> open_positions_;
    std::vector<Trade> closed_trades_;

    void UpdateEquity(const Tick& tick) {
        equity_ = balance_;

        for (Trade* trade : open_positions_) {
            double current_price = (trade->direction == "BUY") ? tick.bid : tick.ask;
            double price_diff = current_price - trade->entry_price;
            if (trade->direction == "SELL") price_diff = -price_diff;
            double unrealized_pl = price_diff * trade->lot_size * contract_size_;
            equity_ += unrealized_pl;
        }

        // Track max drawdown
        if (equity_ > peak_equity_) {
            peak_equity_ = equity_;
        }
        double current_drawdown = peak_equity_ - equity_;
        if (current_drawdown > max_drawdown_) {
            max_drawdown_ = current_drawdown;
            max_drawdown_pct_ = (peak_equity_ > 0) ? (current_drawdown / peak_equity_) * 100.0 : 0.0;
        }
    }

    void CheckMarginStopOut(const Tick& tick) {
        if (open_positions_.empty() || stop_out_occurred_) return;

        double used_margin = 0.0;
        for (Trade* trade : open_positions_) {
            double current_price = (trade->direction == "BUY") ? tick.ask : tick.bid;
            double position_margin = trade->lot_size * contract_size_ * current_price / leverage_;
            used_margin += position_margin;
        }

        if (used_margin <= 0) return;

        double margin_level = (equity_ / used_margin) * 100.0;
        const double STOP_OUT_LEVEL = 20.0;

        if (margin_level < STOP_OUT_LEVEL) {
            // Close all positions
            while (!open_positions_.empty()) {
                ClosePosition(open_positions_[0], "STOP OUT");
            }
            stop_out_occurred_ = true;
        }
    }

    void ProcessOpenPositions(const Tick& tick) {
        std::vector<Trade*> positions_to_close;

        for (Trade* trade : open_positions_) {
            if (trade->direction == "BUY") {
                if (trade->stop_loss > 0 && tick.bid <= trade->stop_loss) {
                    positions_to_close.push_back(trade);
                } else if (trade->take_profit > 0 && tick.bid >= trade->take_profit) {
                    positions_to_close.push_back(trade);
                }
            } else {
                if (trade->stop_loss > 0 && tick.ask >= trade->stop_loss) {
                    positions_to_close.push_back(trade);
                } else if (trade->take_profit > 0 && tick.ask <= trade->take_profit) {
                    positions_to_close.push_back(trade);
                }
            }
        }

        for (Trade* trade : positions_to_close) {
            std::string reason = (trade->direction == "BUY")
                ? ((trade->take_profit > 0 && tick.bid >= trade->take_profit) ? "TP" : "SL")
                : ((trade->take_profit > 0 && tick.ask <= trade->take_profit) ? "TP" : "SL");
            ClosePosition(trade, reason);
        }
    }
};

/**
 * Enhanced Tick Generator for specific stress scenarios
 */
class StressScenarioGenerator {
public:
    StressScenarioGenerator(double start_price = 2600.0, double spread = 0.25)
        : current_price_(start_price),
          spread_(spread),
          tick_count_(0),
          base_timestamp_days_(1),
          base_timestamp_seconds_(0) {}

    // Clear and reset
    void Clear() {
        ticks_.clear();
        tick_count_ = 0;
        base_timestamp_days_ = 1;
        base_timestamp_seconds_ = 0;
    }

    void SetStartPrice(double price) {
        current_price_ = price;
    }

    /**
     * Scenario 1: Flash Crash
     * -10% in 1 hour, then 50% recovery (of the drop) in 2 hours
     */
    void GenerateFlashCrash() {
        Clear();
        double start_price = 2600.0;
        current_price_ = start_price;

        // Generate some stable pre-crash trading (1 hour of sideways)
        GenerateSideways(3600, 5.0);  // 1 tick per second for 1 hour

        // Flash crash: -10% in 1 hour (3600 ticks)
        double crash_target = start_price * 0.90;  // -10% = $2340
        double drop = current_price_ - crash_target;

        // Accelerating crash
        for (int i = 0; i < 3600; i++) {
            double progress = (double)i / 3600.0;
            double acceleration = 1.0 + progress * 2.0;  // Accelerates toward end
            double tick_drop = (drop / 3600.0) * acceleration * 0.5;
            current_price_ -= tick_drop;
            current_price_ = std::max(current_price_, 100.0);
            AddTick();
        }

        // Record bottom
        double bottom = current_price_;
        double drop_amount = start_price - bottom;

        // Recovery: 50% of drop in 2 hours (7200 ticks)
        double recovery_target = bottom + drop_amount * 0.50;
        GenerateTrend(7200, recovery_target - current_price_, 0.5);
    }

    /**
     * Scenario 2: Sustained Crash
     * -30% over 5 days with no recovery
     */
    void GenerateSustainedCrash() {
        Clear();
        double start_price = 2600.0;
        current_price_ = start_price;

        // 5 days = 5 * 24 * 3600 = 432000 seconds
        // Use 1 tick per 10 seconds for performance = 43200 ticks
        int total_ticks = 43200;
        double target = start_price * 0.70;  // -30%
        double total_drop = current_price_ - target;

        // Gradual decline with small bounces
        int segment_ticks = total_ticks / 10;  // 10 segments
        double drop_per_segment = total_drop / 10;

        for (int s = 0; s < 10; s++) {
            // Drop 80% of segment drop
            GenerateTrend(segment_ticks * 8 / 10, -drop_per_segment * 1.0, 0.3);
            // Small bounce (recover 5%)
            GenerateTrend(segment_ticks * 2 / 10, drop_per_segment * 0.05, 0.1);
        }
    }

    /**
     * Scenario 3: V-Shaped Recovery
     * -20% in 2 days, +25% in 3 days (from bottom)
     */
    void GenerateVRecovery() {
        Clear();
        double start_price = 2600.0;
        current_price_ = start_price;

        // Pre-crash stability (4 hours)
        GenerateSideways(1440, 3.0);

        // Down leg: -20% in 2 days
        // 2 days = 17280 ticks at 1 per 10 seconds
        int down_ticks = 17280;
        double bottom_target = start_price * 0.80;  // -20% = $2080
        GenerateTrend(down_ticks, bottom_target - current_price_, 0.5);

        double bottom = current_price_;

        // Up leg: +25% from bottom in 3 days
        // 3 days = 25920 ticks at 1 per 10 seconds
        int up_ticks = 25920;
        double recovery_target = bottom * 1.25;  // +25% from bottom = $2600
        GenerateTrend(up_ticks, recovery_target - current_price_, 0.5);
    }

    /**
     * Scenario 4: Choppy Decline
     * Oscillating down -1% per day for 30 days
     */
    void GenerateChoppyDecline() {
        Clear();
        double start_price = 2600.0;
        current_price_ = start_price;

        // 30 days, using 1440 ticks per day (1 per minute)
        int ticks_per_day = 1440;

        for (int day = 0; day < 30; day++) {
            double day_start = current_price_;
            double day_end_target = day_start * 0.99;  // -1% per day
            double daily_drop = day_start - day_end_target;

            // Each day: oscillate up and down but net -1%
            // 4 cycles per day
            int cycle_ticks = ticks_per_day / 4;

            for (int c = 0; c < 4; c++) {
                // Oscillation amplitude decreases through the day
                double osc_amplitude = (30 - day) * 0.5 + 5;  // $5 to $20

                // Up move
                GenerateTrend(cycle_ticks / 2, osc_amplitude - daily_drop / 4, 0.1);
                // Down move (nets negative)
                GenerateTrend(cycle_ticks / 2, -osc_amplitude - daily_drop / 4, 0.1);
            }
        }
    }

    const std::vector<Tick>& GetTicks() const { return ticks_; }
    double GetCurrentPrice() const { return current_price_; }

private:
    double current_price_;
    double spread_;
    size_t tick_count_;
    int base_timestamp_days_;
    int base_timestamp_seconds_;
    std::vector<Tick> ticks_;

    void AddTick() {
        Tick tick;
        tick.bid = current_price_;
        tick.ask = current_price_ + spread_;
        tick.timestamp = GenerateTimestamp();
        tick.volume = 1;
        ticks_.push_back(tick);
    }

    std::string GenerateTimestamp() {
        // Generate timestamp advancing by 1 second per tick
        int total_seconds = base_timestamp_seconds_ + tick_count_++;
        int days = total_seconds / 86400;
        int remaining = total_seconds % 86400;
        int hours = remaining / 3600;
        remaining %= 3600;
        int minutes = remaining / 60;
        int seconds = remaining % 60;

        int day = base_timestamp_days_ + days;

        std::ostringstream oss;
        oss << "2025.01." << std::setfill('0') << std::setw(2) << day
            << " " << std::setw(2) << hours
            << ":" << std::setw(2) << minutes
            << ":" << std::setw(2) << seconds << ".000";
        return oss.str();
    }

    void GenerateSideways(int num_ticks, double range) {
        double center = current_price_;
        std::mt19937 rng(42);
        std::uniform_real_distribution<double> dist(-range / 2, range / 2);

        for (int i = 0; i < num_ticks; i++) {
            current_price_ = center + dist(rng);
            current_price_ = std::max(current_price_, 100.0);
            AddTick();
        }
    }

    void GenerateTrend(int num_ticks, double total_move, double noise_level) {
        if (num_ticks <= 0) return;

        double move_per_tick = total_move / num_ticks;
        std::mt19937 rng(tick_count_);
        std::normal_distribution<double> noise(0.0, noise_level);

        for (int i = 0; i < num_ticks; i++) {
            current_price_ += move_per_tick + noise(rng);
            current_price_ = std::max(current_price_, 100.0);
            AddTick();
        }
    }
};

/**
 * Test result structure
 */
struct StressTestResult {
    std::string scenario_name;
    double start_price;
    double end_price;
    double price_change_pct;
    double initial_equity;
    double final_equity;
    double max_drawdown_pct;
    double recovery_pct;  // How much of drawdown was recovered
    bool margin_call;
    size_t tick_count;
    std::string status;
};

/**
 * Run a single stress test scenario
 */
StressTestResult RunStressTest(const std::string& scenario_name,
                                const std::vector<Tick>& ticks,
                                double initial_balance = 10000.0) {
    StressTestResult result;
    result.scenario_name = scenario_name;
    result.initial_equity = initial_balance;
    result.tick_count = ticks.size();
    result.margin_call = false;

    if (ticks.empty()) {
        result.status = "NO DATA";
        return result;
    }

    result.start_price = ticks.front().bid;
    result.end_price = ticks.back().bid;
    result.price_change_pct = ((result.end_price - result.start_price) / result.start_price) * 100.0;

    // Create engine
    SyntheticEngine engine(initial_balance, 100.0, 500.0);  // XAUUSD settings

    // Create strategy: survive=13%, spacing=1.5, lookback=1h
    FillUpOscillation strategy(
        13.0,               // survive_pct
        1.5,                // base_spacing
        0.01,               // min_volume
        10.0,               // max_volume
        100.0,              // contract_size
        500.0,              // leverage
        FillUpOscillation::BASELINE,  // mode (baseline for consistent testing)
        0.1,                // antifragile_scale
        30.0,               // velocity_threshold
        1.0                 // volatility_lookback_hours
    );

    double peak_equity = initial_balance;
    double lowest_equity = initial_balance;

    // Process all ticks
    for (const auto& tick : ticks) {
        engine.ProcessTick(tick);

        if (engine.IsStopOutOccurred()) {
            result.margin_call = true;
            break;
        }

        // Call strategy
        strategy.OnTick(tick,
            *reinterpret_cast<TickBasedEngine*>(&engine));  // Cast trick - same interface

        // Track equity
        double equity = engine.GetEquity();
        if (equity > peak_equity) peak_equity = equity;
        if (equity < lowest_equity) lowest_equity = equity;
    }

    result.final_equity = engine.GetEquity();
    result.max_drawdown_pct = engine.GetMaxDrawdownPercent();

    // Calculate recovery percentage
    if (peak_equity > lowest_equity) {
        double total_dd = peak_equity - lowest_equity;
        double recovered = result.final_equity - lowest_equity;
        result.recovery_pct = (recovered / total_dd) * 100.0;
    } else {
        result.recovery_pct = 100.0;
    }

    // Determine status
    if (result.margin_call) {
        result.status = "MARGIN CALL";
    } else if (result.final_equity < initial_balance * 0.5) {
        result.status = "SEVERE LOSS";
    } else if (result.final_equity < initial_balance) {
        result.status = "LOSS";
    } else {
        result.status = "SURVIVED";
    }

    return result;
}

/**
 * Alternative approach: Run with proper FillUpOscillation that works with SyntheticEngine
 * We need a strategy adapter that works with our simplified engine
 */
class StressTestStrategy {
public:
    StressTestStrategy(double survive_pct, double base_spacing,
                       double min_volume, double max_volume,
                       double contract_size, double leverage)
        : survive_pct_(survive_pct),
          base_spacing_(base_spacing),
          min_volume_(min_volume),
          max_volume_(max_volume),
          contract_size_(contract_size),
          leverage_(leverage),
          lowest_buy_(1e30),
          highest_buy_(-1e30),
          volume_of_open_trades_(0.0) {}

    void OnTick(const Tick& tick, SyntheticEngine& engine) {
        // Update position tracking
        Iterate(engine);

        // Open new positions
        OpenNew(tick, engine);
    }

private:
    double survive_pct_;
    double base_spacing_;
    double min_volume_;
    double max_volume_;
    double contract_size_;
    double leverage_;
    double lowest_buy_;
    double highest_buy_;
    double volume_of_open_trades_;

    void Iterate(SyntheticEngine& engine) {
        lowest_buy_ = 1e30;
        highest_buy_ = -1e30;
        volume_of_open_trades_ = 0.0;

        for (const Trade* trade : engine.GetOpenPositions()) {
            if (trade->direction == "BUY") {
                volume_of_open_trades_ += trade->lot_size;
                if (trade->entry_price < lowest_buy_) lowest_buy_ = trade->entry_price;
                if (trade->entry_price > highest_buy_) highest_buy_ = trade->entry_price;
            }
        }
    }

    double CalculateLotSize(const Tick& tick, SyntheticEngine& engine, int positions_total) {
        double current_ask = tick.ask;
        double current_equity = engine.GetEquity();
        double current_spread = tick.spread();

        double used_margin = 0.0;
        for (const Trade* trade : engine.GetOpenPositions()) {
            used_margin += trade->lot_size * contract_size_ * trade->entry_price / leverage_;
        }

        double margin_stop_out = 20.0;

        double end_price = (positions_total == 0)
            ? current_ask * ((100.0 - survive_pct_) / 100.0)
            : highest_buy_ * ((100.0 - survive_pct_) / 100.0);

        double distance = current_ask - end_price;
        double number_of_trades = std::floor(distance / base_spacing_);
        if (number_of_trades <= 0) number_of_trades = 1;

        double equity_at_target = current_equity - volume_of_open_trades_ * distance * contract_size_;
        if (used_margin > 0 && (equity_at_target / used_margin * 100.0) < margin_stop_out) {
            return 0.0;
        }

        double trade_size = min_volume_;
        double d_equity = contract_size_ * trade_size * base_spacing_ * (number_of_trades * (number_of_trades + 1) / 2);
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

    bool Open(const Tick& tick, double lots, SyntheticEngine& engine) {
        if (lots < min_volume_) return false;

        double final_lots = std::min(lots, max_volume_);
        final_lots = std::round(final_lots * 100.0) / 100.0;

        double tp = tick.ask + tick.spread() + base_spacing_;
        Trade* trade = engine.OpenMarketOrder("BUY", final_lots, 0.0, tp);
        return (trade != nullptr);
    }

    void OpenNew(const Tick& tick, SyntheticEngine& engine) {
        int positions_total = engine.GetOpenPositions().size();
        double current_ask = tick.ask;

        if (positions_total == 0) {
            double lots = CalculateLotSize(tick, engine, positions_total);
            if (Open(tick, lots, engine)) {
                highest_buy_ = current_ask;
                lowest_buy_ = current_ask;
            }
        } else {
            if (lowest_buy_ >= current_ask + base_spacing_) {
                double lots = CalculateLotSize(tick, engine, positions_total);
                if (Open(tick, lots, engine)) {
                    lowest_buy_ = current_ask;
                }
            } else if (highest_buy_ <= current_ask - base_spacing_) {
                double lots = CalculateLotSize(tick, engine, positions_total);
                if (Open(tick, lots, engine)) {
                    highest_buy_ = current_ask;
                }
            }
        }
    }
};

/**
 * Run stress test with the proper strategy adapter
 */
StressTestResult RunStressTestProper(const std::string& scenario_name,
                                      const std::vector<Tick>& ticks,
                                      double initial_balance = 10000.0) {
    StressTestResult result;
    result.scenario_name = scenario_name;
    result.initial_equity = initial_balance;
    result.tick_count = ticks.size();
    result.margin_call = false;

    if (ticks.empty()) {
        result.status = "NO DATA";
        return result;
    }

    result.start_price = ticks.front().bid;
    result.end_price = ticks.back().bid;
    result.price_change_pct = ((result.end_price - result.start_price) / result.start_price) * 100.0;

    // Create engine
    SyntheticEngine engine(initial_balance, 100.0, 500.0);  // XAUUSD settings

    // Create strategy: survive=13%, spacing=1.5
    StressTestStrategy strategy(
        13.0,    // survive_pct
        1.5,     // base_spacing
        0.01,    // min_volume
        10.0,    // max_volume
        100.0,   // contract_size
        500.0    // leverage
    );

    double peak_equity = initial_balance;
    double lowest_equity = initial_balance;

    // Process all ticks
    for (const auto& tick : ticks) {
        engine.ProcessTick(tick);

        if (engine.IsStopOutOccurred()) {
            result.margin_call = true;
            break;
        }

        // Call strategy
        strategy.OnTick(tick, engine);

        // Track equity
        double equity = engine.GetEquity();
        if (equity > peak_equity) peak_equity = equity;
        if (equity < lowest_equity) lowest_equity = equity;
    }

    result.final_equity = engine.GetEquity();
    result.max_drawdown_pct = engine.GetMaxDrawdownPercent();

    // Calculate recovery percentage
    if (peak_equity > lowest_equity) {
        double total_dd = peak_equity - lowest_equity;
        double recovered = result.final_equity - lowest_equity;
        result.recovery_pct = (recovered / total_dd) * 100.0;
    } else {
        result.recovery_pct = 100.0;
    }

    // Determine status
    if (result.margin_call) {
        result.status = "MARGIN CALL";
    } else if (result.final_equity < initial_balance * 0.5) {
        result.status = "SEVERE LOSS";
    } else if (result.final_equity < initial_balance) {
        result.status = "LOSS";
    } else {
        result.status = "SURVIVED";
    }

    return result;
}

void PrintResult(const StressTestResult& r) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n" << std::string(60, '-') << std::endl;
    std::cout << "Scenario: " << r.scenario_name << std::endl;
    std::cout << std::string(60, '-') << std::endl;
    std::cout << "  Price: $" << r.start_price << " -> $" << r.end_price
              << " (" << (r.price_change_pct >= 0 ? "+" : "") << r.price_change_pct << "%)" << std::endl;
    std::cout << "  Ticks processed: " << r.tick_count << std::endl;
    std::cout << "  Equity: $" << r.initial_equity << " -> $" << r.final_equity
              << " (" << (r.final_equity >= r.initial_equity ? "+" : "")
              << ((r.final_equity - r.initial_equity) / r.initial_equity * 100.0) << "%)" << std::endl;
    std::cout << "  Max Drawdown: " << r.max_drawdown_pct << "%" << std::endl;
    std::cout << "  Recovery: " << r.recovery_pct << "%" << std::endl;
    std::cout << "  Margin Call: " << (r.margin_call ? "YES" : "NO") << std::endl;
    std::cout << "  STATUS: " << r.status << std::endl;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "    STRESS TEST SCENARIOS - Strategy Robustness Analysis" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "\nStrategy: FillUpOscillation (survive=13%, spacing=$1.5)" << std::endl;
    std::cout << "Initial Balance: $10,000" << std::endl;
    std::cout << "Leverage: 1:500, Contract Size: 100" << std::endl;

    StressScenarioGenerator generator;
    std::vector<StressTestResult> results;

    // Scenario 1: Flash Crash
    std::cout << "\n[1/4] Generating Flash Crash scenario..." << std::endl;
    generator.GenerateFlashCrash();
    std::cout << "  Generated " << generator.GetTicks().size() << " ticks" << std::endl;
    results.push_back(RunStressTestProper("Flash Crash (-10% in 1h, 50% recovery in 2h)",
                                           generator.GetTicks()));
    PrintResult(results.back());

    // Scenario 2: Sustained Crash
    std::cout << "\n[2/4] Generating Sustained Crash scenario..." << std::endl;
    generator.GenerateSustainedCrash();
    std::cout << "  Generated " << generator.GetTicks().size() << " ticks" << std::endl;
    results.push_back(RunStressTestProper("Sustained Crash (-30% over 5 days, no recovery)",
                                           generator.GetTicks()));
    PrintResult(results.back());

    // Scenario 3: V-Shaped Recovery
    std::cout << "\n[3/4] Generating V-Shaped Recovery scenario..." << std::endl;
    generator.GenerateVRecovery();
    std::cout << "  Generated " << generator.GetTicks().size() << " ticks" << std::endl;
    results.push_back(RunStressTestProper("V-Shaped Recovery (-20% in 2d, +25% in 3d)",
                                           generator.GetTicks()));
    PrintResult(results.back());

    // Scenario 4: Choppy Decline
    std::cout << "\n[4/4] Generating Choppy Decline scenario..." << std::endl;
    generator.GenerateChoppyDecline();
    std::cout << "  Generated " << generator.GetTicks().size() << " ticks" << std::endl;
    results.push_back(RunStressTestProper("Choppy Decline (-1%/day oscillating for 30 days)",
                                           generator.GetTicks()));
    PrintResult(results.back());

    // Summary table
    std::cout << "\n\n" << std::string(90, '=') << std::endl;
    std::cout << "                           SUMMARY TABLE" << std::endl;
    std::cout << std::string(90, '=') << std::endl;
    std::cout << std::setw(45) << std::left << "Scenario"
              << std::setw(12) << std::right << "Final $"
              << std::setw(10) << "Max DD%"
              << std::setw(12) << "Recovery%"
              << std::setw(12) << "Status" << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    int survived_count = 0;
    int margin_call_count = 0;

    for (const auto& r : results) {
        std::cout << std::setw(45) << std::left << r.scenario_name
                  << std::setw(12) << std::right << std::fixed << std::setprecision(0) << r.final_equity
                  << std::setw(9) << std::setprecision(1) << r.max_drawdown_pct << "%"
                  << std::setw(11) << std::setprecision(1) << r.recovery_pct << "%"
                  << std::setw(12) << r.status << std::endl;

        if (r.status == "SURVIVED") survived_count++;
        if (r.margin_call) margin_call_count++;
    }

    std::cout << std::string(90, '=') << std::endl;

    // Final verdict
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "                    FINAL VERDICT" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "  Scenarios Survived: " << survived_count << "/" << results.size() << std::endl;
    std::cout << "  Margin Calls: " << margin_call_count << "/" << results.size() << std::endl;

    if (margin_call_count > 0) {
        std::cout << "\n  FAILURES:" << std::endl;
        for (const auto& r : results) {
            if (r.margin_call) {
                std::cout << "    - " << r.scenario_name << std::endl;
            }
        }
    }

    if (survived_count == (int)results.size()) {
        std::cout << "\n  Strategy PASSED all stress tests!" << std::endl;
    } else {
        std::cout << "\n  Strategy FAILED " << (results.size() - survived_count)
                  << " stress test(s)." << std::endl;
        std::cout << "  Consider increasing survive_pct or reducing position sizes." << std::endl;
    }

    std::cout << std::string(60, '=') << std::endl;

    return (margin_call_count > 0) ? 1 : 0;
}
