/**
 * Kill Switch Real Data Test
 *
 * Tests kill switch impact on actual 2025 XAUUSD tick data
 * Measures the return cost of protection during normal market conditions
 */

#include "../include/fill_up_oscillation.h"
#include "../include/kill_switch.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <algorithm>

using namespace backtest;

/**
 * Modified FillUpOscillation with Kill Switch integration
 */
class FillUpWithKillSwitch {
public:
    FillUpWithKillSwitch(double survive_pct, double base_spacing,
                         double min_volume, double max_volume,
                         double contract_size, double leverage,
                         bool enable_kill_switch,
                         const KillSwitch::Config& ks_config = KillSwitch::Config())
        : survive_pct_(survive_pct),
          base_spacing_(base_spacing),
          min_volume_(min_volume),
          max_volume_(max_volume),
          contract_size_(contract_size),
          leverage_(leverage),
          enable_kill_switch_(enable_kill_switch),
          kill_switch_(ks_config),
          lowest_buy_(1e30),
          highest_buy_(-1e30),
          volume_of_open_trades_(0.0),
          tick_count_(0),
          emergency_closes_(0),
          pause_ticks_(0),
          caution_ticks_(0),
          normal_ticks_(0),
          emergency_ticks_(0),
          entries_blocked_(0) {}

    void OnTick(const Tick& tick, TickBasedEngine& engine) {
        tick_count_++;

        double current_equity = engine.GetEquity();
        double used_margin = 0.0;
        for (const Trade* t : engine.GetOpenPositions()) {
            used_margin += t->lot_size * contract_size_ * t->entry_price / leverage_;
        }

        // Update kill switch
        KillSwitch::Level level = KillSwitch::NORMAL;
        if (enable_kill_switch_) {
            level = kill_switch_.Update(tick.bid, current_equity, used_margin, tick_count_);

            // Track time in each level
            switch (level) {
                case KillSwitch::NORMAL: normal_ticks_++; break;
                case KillSwitch::CAUTION: caution_ticks_++; break;
                case KillSwitch::PAUSE: pause_ticks_++; break;
                case KillSwitch::EMERGENCY:
                    // Close all positions (only count if there are positions to close)
                    if (!engine.GetOpenPositions().empty()) {
                        for (const Trade* t : engine.GetOpenPositions()) {
                            engine.ClosePosition(const_cast<Trade*>(t), "KILL_SWITCH_EMERGENCY");
                        }
                        emergency_closes_++;
                    }
                    emergency_ticks_++;  // Track time in emergency state
                    return;
            }

            // Pause - no new trades
            if (level >= KillSwitch::PAUSE) {
                entries_blocked_++;
                return;
            }
        }

        // Update position tracking
        UpdatePositionTracking(engine);

        // Open new positions (with size multiplier from kill switch)
        double size_mult = enable_kill_switch_ ? kill_switch_.GetSizeMultiplier() : 1.0;
        OpenNewPositions(tick, engine, size_mult);
    }

    // Statistics
    int GetEmergencyCloses() const { return emergency_closes_; }
    long GetEmergencyTicks() const { return emergency_ticks_; }
    long GetPauseTicks() const { return pause_ticks_; }
    long GetCautionTicks() const { return caution_ticks_; }
    long GetNormalTicks() const { return normal_ticks_; }
    long GetEntriesBlocked() const { return entries_blocked_; }
    const KillSwitch::State& GetKillSwitchState() const { return kill_switch_.GetState(); }

private:
    double survive_pct_;
    double base_spacing_;
    double min_volume_;
    double max_volume_;
    double contract_size_;
    double leverage_;
    bool enable_kill_switch_;
    KillSwitch kill_switch_;
    double lowest_buy_;
    double highest_buy_;
    double volume_of_open_trades_;
    long tick_count_;
    int emergency_closes_;
    long pause_ticks_;
    long caution_ticks_;
    long normal_ticks_;
    long emergency_ticks_;
    long entries_blocked_;

    void UpdatePositionTracking(TickBasedEngine& engine) {
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

    double CalculateLotSize(const Tick& tick, TickBasedEngine& engine, int positions_total, double size_mult) {
        double current_ask = tick.ask;
        double current_equity = engine.GetEquity();

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

        // Apply size multiplier from kill switch
        trade_size *= size_mult;

        return std::min(trade_size, max_volume_);
    }

    void OpenNewPositions(const Tick& tick, TickBasedEngine& engine, double size_mult) {
        int positions_total = engine.GetOpenPositions().size();
        double current_ask = tick.ask;
        double spread = tick.spread();

        if (positions_total == 0) {
            double lots = CalculateLotSize(tick, engine, positions_total, size_mult);
            if (lots >= min_volume_) {
                double tp = current_ask + spread + base_spacing_;
                engine.OpenMarketOrder("BUY", lots, 0.0, tp);
                highest_buy_ = current_ask;
                lowest_buy_ = current_ask;
            }
        } else {
            if (lowest_buy_ >= current_ask + base_spacing_) {
                double lots = CalculateLotSize(tick, engine, positions_total, size_mult);
                if (lots >= min_volume_) {
                    double tp = current_ask + spread + base_spacing_;
                    engine.OpenMarketOrder("BUY", lots, 0.0, tp);
                    lowest_buy_ = current_ask;
                }
            } else if (highest_buy_ <= current_ask - base_spacing_) {
                double lots = CalculateLotSize(tick, engine, positions_total, size_mult);
                if (lots >= min_volume_) {
                    double tp = current_ask + spread + base_spacing_;
                    engine.OpenMarketOrder("BUY", lots, 0.0, tp);
                    highest_buy_ = current_ask;
                }
            }
        }
    }
};

struct TestResult {
    bool kill_switch_enabled;
    double final_balance;
    double max_drawdown;
    double max_drawdown_pct;
    int total_trades;
    double total_swap;
    int emergency_closes;      // Actual close events
    long emergency_ticks;      // Time in emergency state
    long pause_ticks;
    long caution_ticks;
    long normal_ticks;
    long entries_blocked;
};

TestResult RunTest(bool enable_kill_switch, const KillSwitch::Config& ks_config) {
    TestResult result;
    result.kill_switch_enabled = enable_kill_switch;

    // Configure tick data
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;
    tick_config.load_all_into_memory = false;

    // Configure backtest
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
    config.end_date = "2025.12.29";
    config.tick_data_config = tick_config;

    try {
        TickBasedEngine engine(config);

        FillUpWithKillSwitch strategy(
            13.0,    // survive_pct
            1.5,     // base_spacing
            0.01,    // min_volume
            10.0,    // max_volume
            100.0,   // contract_size
            500.0,   // leverage
            enable_kill_switch,
            ks_config
        );

        engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
            strategy.OnTick(tick, eng);
        });

        auto results = engine.GetResults();
        result.final_balance = results.final_balance;
        result.max_drawdown = results.max_drawdown;
        // Calculate max DD % from max_drawdown and peak equity (approximate using initial + profit as peak)
        double peak_estimate = std::max(results.initial_balance, results.final_balance + results.max_drawdown);
        result.max_drawdown_pct = (peak_estimate > 0) ? (results.max_drawdown / peak_estimate * 100.0) : 0.0;
        result.total_trades = results.total_trades;
        result.total_swap = results.total_swap_charged;

        result.emergency_closes = strategy.GetEmergencyCloses();
        result.emergency_ticks = strategy.GetEmergencyTicks();
        result.pause_ticks = strategy.GetPauseTicks();
        result.caution_ticks = strategy.GetCautionTicks();
        result.normal_ticks = strategy.GetNormalTicks();
        result.entries_blocked = strategy.GetEntriesBlocked();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        result.final_balance = 0;
    }

    return result;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "    KILL SWITCH - Real 2025 XAUUSD Data Test" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "\nStrategy: FillUp (survive=13%, spacing=$1.5)" << std::endl;
    std::cout << "Initial Balance: $10,000 | Leverage: 1:500\n" << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    // Test configurations
    struct TestConfig {
        std::string name;
        KillSwitch::Config ks_config;
    };

    std::vector<TestConfig> configs;

    // Grid-tuned kill switch - normal operation has 40-60% DD
    // Only triggers on true crash scenarios
    KillSwitch::Config grid_tuned;
    grid_tuned.caution_dd_pct = 50.0;       // 50% DD = caution (reduce size)
    grid_tuned.pause_dd_pct = 60.0;         // 60% DD = pause (stop entries)
    grid_tuned.emergency_dd_pct = 70.0;     // 70% DD = emergency (close all)
    grid_tuned.pause_daily_loss_pct = 20.0; // 20% daily loss = pause
    grid_tuned.emergency_daily_loss_pct = 30.0;  // 30% daily loss = emergency
    grid_tuned.pause_consecutive_down = 4;   // 4 hours consecutive down
    grid_tuned.emergency_consecutive_down = 6;   // 6 hours consecutive down
    grid_tuned.crash_velocity_pct_hour = 3.0;    // 3%/hour = crash
    grid_tuned.emergency_velocity_pct_hour = 5.0; // 5%/hour = emergency
    configs.push_back({"Grid-Tuned KS", grid_tuned});

    // Very conservative - only intervene on extreme crashes
    KillSwitch::Config extreme_only;
    extreme_only.caution_dd_pct = 60.0;
    extreme_only.pause_dd_pct = 70.0;
    extreme_only.emergency_dd_pct = 80.0;
    extreme_only.pause_daily_loss_pct = 25.0;
    extreme_only.emergency_daily_loss_pct = 40.0;
    extreme_only.pause_consecutive_down = 5;
    extreme_only.emergency_consecutive_down = 8;
    extreme_only.crash_velocity_pct_hour = 4.0;
    extreme_only.emergency_velocity_pct_hour = 6.0;
    configs.push_back({"Extreme-Only KS", extreme_only});

    // Velocity-focused - primarily watch for fast crashes
    KillSwitch::Config velocity_focus;
    velocity_focus.caution_dd_pct = 55.0;
    velocity_focus.pause_dd_pct = 65.0;
    velocity_focus.emergency_dd_pct = 75.0;
    velocity_focus.pause_daily_loss_pct = 15.0;  // More sensitive to daily loss
    velocity_focus.emergency_daily_loss_pct = 25.0;
    velocity_focus.pause_consecutive_down = 3;   // More sensitive to consecutive
    velocity_focus.emergency_consecutive_down = 5;
    velocity_focus.crash_velocity_pct_hour = 2.0;  // More sensitive to velocity
    velocity_focus.emergency_velocity_pct_hour = 4.0;
    configs.push_back({"Velocity-Focus KS", velocity_focus});

    // NEW: DD Confirmation - require DD to persist for 2 hours before triggering PAUSE/EMERGENCY
    // This prevents brief spikes from triggering the kill switch
    KillSwitch::Config dd_confirm_2h;
    dd_confirm_2h.caution_dd_pct = 50.0;
    dd_confirm_2h.pause_dd_pct = 60.0;
    dd_confirm_2h.emergency_dd_pct = 70.0;
    dd_confirm_2h.pause_daily_loss_pct = 20.0;
    dd_confirm_2h.emergency_daily_loss_pct = 30.0;
    dd_confirm_2h.pause_consecutive_down = 4;
    dd_confirm_2h.emergency_consecutive_down = 6;
    dd_confirm_2h.crash_velocity_pct_hour = 3.0;      // Same as Grid-Tuned
    dd_confirm_2h.emergency_velocity_pct_hour = 5.0;  // Same as Grid-Tuned
    dd_confirm_2h.dd_confirmation_hours = 2;  // DD must persist 2 hours
    dd_confirm_2h.use_smoothed_dd = false;
    configs.push_back({"DD-Confirm-2h", dd_confirm_2h});

    // NEW: DD Confirmation - 4 hours for more permissive operation
    KillSwitch::Config dd_confirm_4h;
    dd_confirm_4h.caution_dd_pct = 50.0;
    dd_confirm_4h.pause_dd_pct = 60.0;
    dd_confirm_4h.emergency_dd_pct = 70.0;
    dd_confirm_4h.pause_daily_loss_pct = 20.0;
    dd_confirm_4h.emergency_daily_loss_pct = 30.0;
    dd_confirm_4h.pause_consecutive_down = 4;
    dd_confirm_4h.emergency_consecutive_down = 6;
    dd_confirm_4h.crash_velocity_pct_hour = 3.0;      // Same as Grid-Tuned
    dd_confirm_4h.emergency_velocity_pct_hour = 5.0;  // Same as Grid-Tuned
    dd_confirm_4h.dd_confirmation_hours = 4;  // DD must persist 4 hours
    dd_confirm_4h.use_smoothed_dd = false;
    configs.push_back({"DD-Confirm-4h", dd_confirm_4h});

    // NEW: Smoothed DD - use exponential moving average to reduce noise
    KillSwitch::Config smoothed_dd;
    smoothed_dd.caution_dd_pct = 50.0;
    smoothed_dd.pause_dd_pct = 60.0;
    smoothed_dd.emergency_dd_pct = 70.0;
    smoothed_dd.pause_daily_loss_pct = 20.0;
    smoothed_dd.emergency_daily_loss_pct = 30.0;
    smoothed_dd.pause_consecutive_down = 4;
    smoothed_dd.emergency_consecutive_down = 6;
    smoothed_dd.crash_velocity_pct_hour = 3.0;      // Same as Grid-Tuned
    smoothed_dd.emergency_velocity_pct_hour = 5.0;  // Same as Grid-Tuned
    smoothed_dd.dd_confirmation_hours = 0;  // No confirmation delay
    smoothed_dd.use_smoothed_dd = true;
    smoothed_dd.dd_smoothing_alpha = 0.1;  // Slow smoothing (more memory)
    configs.push_back({"Smoothed-DD-0.1", smoothed_dd});

    // NEW: Combined - confirmation + smoothing for maximum filtering
    KillSwitch::Config combined;
    combined.caution_dd_pct = 50.0;
    combined.pause_dd_pct = 60.0;
    combined.emergency_dd_pct = 70.0;
    combined.pause_daily_loss_pct = 20.0;
    combined.emergency_daily_loss_pct = 30.0;
    combined.pause_consecutive_down = 4;
    combined.emergency_consecutive_down = 6;
    combined.crash_velocity_pct_hour = 3.0;      // Same as Grid-Tuned
    combined.emergency_velocity_pct_hour = 5.0;  // Same as Grid-Tuned
    combined.dd_confirmation_hours = 2;  // 2 hour confirmation
    combined.use_smoothed_dd = true;
    combined.dd_smoothing_alpha = 0.2;  // Medium smoothing
    configs.push_back({"Combined-2h-0.2", combined});

    // NEW: Very permissive - only intervene on true catastrophic conditions
    // Hypothesis: The PAUSE is triggered by consecutive_down, not DD
    KillSwitch::Config very_permissive;
    very_permissive.caution_dd_pct = 60.0;       // Higher thresholds
    very_permissive.pause_dd_pct = 70.0;
    very_permissive.emergency_dd_pct = 85.0;
    very_permissive.pause_daily_loss_pct = 30.0; // Much higher
    very_permissive.emergency_daily_loss_pct = 40.0;
    very_permissive.pause_consecutive_down = 8;   // Much higher (8 hours!)
    very_permissive.emergency_consecutive_down = 12;
    very_permissive.crash_velocity_pct_hour = 5.0;    // Less sensitive
    very_permissive.emergency_velocity_pct_hour = 8.0;
    very_permissive.dd_confirmation_hours = 4;
    very_permissive.use_smoothed_dd = true;
    very_permissive.dd_smoothing_alpha = 0.1;
    configs.push_back({"Very-Permissive", very_permissive});

    // NEW: Minimal intervention - only emergency shutdown conditions
    KillSwitch::Config minimal;
    minimal.caution_dd_pct = 80.0;       // Very high
    minimal.pause_dd_pct = 90.0;         // Almost never trigger
    minimal.emergency_dd_pct = 95.0;
    minimal.pause_daily_loss_pct = 50.0;
    minimal.emergency_daily_loss_pct = 60.0;
    minimal.pause_consecutive_down = 24;  // Full day of drops (won't happen normally)
    minimal.emergency_consecutive_down = 48;
    minimal.crash_velocity_pct_hour = 10.0;   // Very insensitive
    minimal.emergency_velocity_pct_hour = 15.0;
    minimal.dd_confirmation_hours = 8;
    configs.push_back({"Minimal-KS", minimal});

    // Run without kill switch first
    std::cout << "[1/" << (configs.size() + 1) << "] Running WITHOUT kill switch..." << std::endl;
    TestResult no_ks = RunTest(false, KillSwitch::Config());
    std::cout << "  Final: $" << std::fixed << std::setprecision(0) << no_ks.final_balance
              << " | Max DD: " << std::setprecision(1) << no_ks.max_drawdown_pct << "%" << std::endl;

    // Run with each kill switch config
    std::vector<TestResult> ks_results;
    for (size_t i = 0; i < configs.size(); i++) {
        std::cout << "[" << (i + 2) << "/" << (configs.size() + 1) << "] Running " << configs[i].name << "..." << std::endl;
        TestResult result = RunTest(true, configs[i].ks_config);
        result.kill_switch_enabled = true;
        ks_results.push_back(result);
        std::cout << "  Final: $" << std::fixed << std::setprecision(0) << result.final_balance
                  << " | Max DD: " << std::setprecision(1) << result.max_drawdown_pct << "%" << std::endl;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    // Results table
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "                              COMPARISON TABLE" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    std::cout << std::setw(18) << std::left << "Configuration"
              << std::setw(12) << std::right << "Final $"
              << std::setw(10) << "Return"
              << std::setw(10) << "Max DD"
              << std::setw(10) << "Trades"
              << std::setw(12) << "Swap"
              << std::setw(10) << "EmergClose"
              << std::setw(12) << "Pause Ticks" << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    // Print baseline
    std::cout << std::setw(18) << std::left << "NO Kill Switch"
              << std::setw(11) << std::right << "$" << std::fixed << std::setprecision(0) << no_ks.final_balance
              << std::setw(9) << std::setprecision(2) << (no_ks.final_balance / 10000.0) << "x"
              << std::setw(9) << std::setprecision(1) << no_ks.max_drawdown_pct << "%"
              << std::setw(10) << no_ks.total_trades
              << std::setw(11) << "$" << std::setprecision(0) << no_ks.total_swap
              << std::setw(10) << "-"
              << std::setw(12) << "-" << std::endl;

    // Print each KS config
    for (size_t i = 0; i < configs.size(); i++) {
        const TestResult& r = ks_results[i];
        double return_diff = (r.final_balance - no_ks.final_balance) / no_ks.final_balance * 100.0;

        std::cout << std::setw(18) << std::left << configs[i].name
                  << std::setw(11) << std::right << "$" << std::fixed << std::setprecision(0) << r.final_balance
                  << std::setw(9) << std::setprecision(2) << (r.final_balance / 10000.0) << "x"
                  << std::setw(9) << std::setprecision(1) << r.max_drawdown_pct << "%"
                  << std::setw(10) << r.total_trades
                  << std::setw(11) << "$" << std::setprecision(0) << r.total_swap
                  << std::setw(10) << r.emergency_closes
                  << std::setw(12) << r.pause_ticks << std::endl;
    }

    std::cout << std::string(100, '=') << std::endl;

    // Analysis
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "                    ANALYSIS" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    std::cout << "\nBaseline (No Kill Switch):" << std::endl;
    std::cout << "  Return: " << std::fixed << std::setprecision(2) << (no_ks.final_balance / 10000.0) << "x" << std::endl;
    std::cout << "  Max DD: " << std::setprecision(1) << no_ks.max_drawdown_pct << "%" << std::endl;
    std::cout << "  Risk-Adj: " << std::setprecision(2) << ((no_ks.final_balance / 10000.0 - 1) * 100.0 / no_ks.max_drawdown_pct) << std::endl;

    std::cout << "\nKill Switch Impact:" << std::endl;
    for (size_t i = 0; i < configs.size(); i++) {
        const TestResult& r = ks_results[i];
        double return_diff = (r.final_balance - no_ks.final_balance) / no_ks.final_balance * 100.0;
        double dd_diff = no_ks.max_drawdown_pct - r.max_drawdown_pct;
        double risk_adj_base = ((no_ks.final_balance / 10000.0 - 1) * 100.0 / no_ks.max_drawdown_pct);
        double risk_adj_ks = ((r.final_balance / 10000.0 - 1) * 100.0 / r.max_drawdown_pct);

        long total_ticks = r.emergency_ticks + r.pause_ticks + r.caution_ticks + r.normal_ticks;
        std::cout << "\n  " << configs[i].name << ":" << std::endl;
        std::cout << "    Return change: " << (return_diff >= 0 ? "+" : "") << std::setprecision(1) << return_diff << "%" << std::endl;
        std::cout << "    DD reduction: " << (dd_diff >= 0 ? "-" : "+") << std::abs(dd_diff) << " points" << std::endl;
        std::cout << "    Risk-Adj change: " << std::setprecision(2) << risk_adj_base << " -> " << risk_adj_ks
                  << " (" << (risk_adj_ks > risk_adj_base ? "BETTER" : "WORSE") << ")" << std::endl;
        std::cout << "    Emergency closes: " << r.emergency_closes << std::endl;
        std::cout << "    Time in EMERGENCY: " << std::setprecision(2) << (r.emergency_ticks / (double)total_ticks * 100.0) << "%" << std::endl;
        std::cout << "    Time in PAUSE: " << std::setprecision(2) << (r.pause_ticks / (double)total_ticks * 100.0) << "%" << std::endl;
        std::cout << "    Time in NORMAL: " << std::setprecision(2) << (r.normal_ticks / (double)total_ticks * 100.0) << "%" << std::endl;
    }

    std::cout << "\nTotal test time: " << duration.count() << " seconds" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    return 0;
}
