/**
 * Kill Switch Stress Test
 *
 * Compares FillUp strategy performance WITH and WITHOUT kill switch protection
 * across 4 stress scenarios:
 * 1. Flash crash: -10% in 1 hour, 50% recovery
 * 2. Sustained crash: -30% over 5 days
 * 3. V-shaped recovery: -20% in 2 days, +25% in 3 days
 * 4. Choppy decline: -1% per day for 30 days
 *
 * Goal: Demonstrate that kill switch prevents margin calls at acceptable return cost
 */

#include "../include/kill_switch.h"
#include "../include/tick_data.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>
#include <sstream>
#include <random>
#include <algorithm>

using namespace backtest;

//=============================================================================
// SYNTHETIC ENGINE (simplified backtest engine for stress testing)
//=============================================================================

class SyntheticEngine {
public:
    struct Position {
        int id;
        double entry_price;
        double lot_size;
        double take_profit;
        double stop_loss;
    };

    SyntheticEngine(double initial_balance, double contract_size, double leverage)
        : balance_(initial_balance),
          equity_(initial_balance),
          initial_balance_(initial_balance),
          contract_size_(contract_size),
          leverage_(leverage),
          next_id_(1),
          peak_equity_(initial_balance),
          max_drawdown_(0.0),
          max_drawdown_pct_(0.0),
          stop_out_occurred_(false),
          current_bid_(0.0),
          current_ask_(0.0),
          total_trades_(0),
          winning_trades_(0),
          total_profit_(0.0),
          total_loss_(0.0) {}

    Position* OpenBuy(double lot_size, double take_profit = 0.0, double stop_loss = 0.0) {
        Position pos;
        pos.id = next_id_++;
        pos.entry_price = current_ask_;
        pos.lot_size = lot_size;
        pos.take_profit = take_profit;
        pos.stop_loss = stop_loss;
        positions_.push_back(pos);
        total_trades_++;
        return &positions_.back();
    }

    void ClosePosition(int id, const std::string& reason = "") {
        (void)reason;  // Unused but kept for debugging
        for (auto it = positions_.begin(); it != positions_.end(); ++it) {
            if (it->id == id) {
                double pnl = (current_bid_ - it->entry_price) * it->lot_size * contract_size_;
                balance_ += pnl;
                if (pnl >= 0) {
                    winning_trades_++;
                    total_profit_ += pnl;
                } else {
                    total_loss_ += std::abs(pnl);
                }
                positions_.erase(it);
                break;
            }
        }
    }

    void CloseAllPositions(const std::string& reason = "") {
        while (!positions_.empty()) {
            ClosePosition(positions_.front().id, reason);
        }
    }

    void ProcessTick(double bid, double ask) {
        current_bid_ = bid;
        current_ask_ = ask;
        UpdateEquity();
        CheckStopOut();
        CheckTakeProfits();
    }

    // Getters
    double GetBalance() const { return balance_; }
    double GetEquity() const { return equity_; }
    double GetBid() const { return current_bid_; }
    double GetAsk() const { return current_ask_; }
    double GetMaxDrawdownPct() const { return max_drawdown_pct_; }
    double GetPeakEquity() const { return peak_equity_; }
    double GetInitialBalance() const { return initial_balance_; }
    bool IsStopOut() const { return stop_out_occurred_; }
    const std::vector<Position>& GetPositions() const { return positions_; }
    int GetTotalTrades() const { return total_trades_; }
    int GetWinningTrades() const { return winning_trades_; }
    double GetTotalProfit() const { return total_profit_; }
    double GetTotalLoss() const { return total_loss_; }

    double GetUsedMargin() const {
        double margin = 0.0;
        for (const auto& pos : positions_) {
            margin += pos.lot_size * contract_size_ * pos.entry_price / leverage_;
        }
        return margin;
    }

    double GetMarginLevel() const {
        double margin = GetUsedMargin();
        return (margin > 0) ? (equity_ / margin * 100.0) : 10000.0;
    }

private:
    double balance_;
    double equity_;
    double initial_balance_;
    double contract_size_;
    double leverage_;
    int next_id_;
    double peak_equity_;
    double max_drawdown_;
    double max_drawdown_pct_;
    bool stop_out_occurred_;
    double current_bid_;
    double current_ask_;
    std::vector<Position> positions_;
    int total_trades_;
    int winning_trades_;
    double total_profit_;
    double total_loss_;

    void UpdateEquity() {
        equity_ = balance_;
        for (const auto& pos : positions_) {
            double unrealized = (current_bid_ - pos.entry_price) * pos.lot_size * contract_size_;
            equity_ += unrealized;
        }

        if (equity_ > peak_equity_) {
            peak_equity_ = equity_;
        }

        double dd = peak_equity_ - equity_;
        if (dd > max_drawdown_) {
            max_drawdown_ = dd;
            max_drawdown_pct_ = (peak_equity_ > 0) ? (dd / peak_equity_ * 100.0) : 0.0;
        }
    }

    void CheckStopOut() {
        if (positions_.empty() || stop_out_occurred_) return;

        double margin = GetUsedMargin();
        if (margin <= 0) return;

        double margin_level = (equity_ / margin) * 100.0;
        if (margin_level < 20.0) {
            // Close all at stop-out
            while (!positions_.empty()) {
                ClosePosition(positions_.front().id, "STOP_OUT");
            }
            stop_out_occurred_ = true;
        }
    }

    void CheckTakeProfits() {
        std::vector<int> to_close;
        for (const auto& pos : positions_) {
            if (pos.take_profit > 0 && current_bid_ >= pos.take_profit) {
                to_close.push_back(pos.id);
            }
            if (pos.stop_loss > 0 && current_bid_ <= pos.stop_loss) {
                to_close.push_back(pos.id);
            }
        }
        for (int id : to_close) {
            ClosePosition(id, "TP/SL");
        }
    }
};

//=============================================================================
// STRATEGY WITH KILL SWITCH
//=============================================================================

class StrategyWithKillSwitch {
public:
    StrategyWithKillSwitch(double survive_pct, double base_spacing,
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
          caution_ticks_(0) {}

    void OnTick(SyntheticEngine& engine) {
        tick_count_++;

        // Update kill switch
        KillSwitch::Level level = KillSwitch::NORMAL;
        if (enable_kill_switch_) {
            level = kill_switch_.Update(
                engine.GetBid(),
                engine.GetEquity(),
                engine.GetUsedMargin(),
                tick_count_
            );

            // Track time in each level
            if (level == KillSwitch::PAUSE) pause_ticks_++;
            else if (level == KillSwitch::CAUTION) caution_ticks_++;

            // Emergency close all
            if (level == KillSwitch::EMERGENCY && !engine.GetPositions().empty()) {
                engine.CloseAllPositions("KILL_SWITCH_EMERGENCY");
                emergency_closes_++;
                return;
            }

            // Pause - no new trades
            if (level >= KillSwitch::PAUSE) {
                return;
            }
        }

        // Update position tracking
        UpdatePositionTracking(engine);

        // Open new positions (with size multiplier from kill switch)
        double size_mult = enable_kill_switch_ ? kill_switch_.GetSizeMultiplier() : 1.0;
        OpenNewPositions(engine, size_mult);
    }

    // Statistics
    int GetEmergencyCloses() const { return emergency_closes_; }
    long GetPauseTicks() const { return pause_ticks_; }
    long GetCautionTicks() const { return caution_ticks_; }
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

    void UpdatePositionTracking(SyntheticEngine& engine) {
        lowest_buy_ = 1e30;
        highest_buy_ = -1e30;
        volume_of_open_trades_ = 0.0;

        for (const auto& pos : engine.GetPositions()) {
            volume_of_open_trades_ += pos.lot_size;
            if (pos.entry_price < lowest_buy_) lowest_buy_ = pos.entry_price;
            if (pos.entry_price > highest_buy_) highest_buy_ = pos.entry_price;
        }
    }

    double CalculateLotSize(SyntheticEngine& engine, int positions_total, double size_mult) {
        double current_ask = engine.GetAsk();
        double current_equity = engine.GetEquity();

        double used_margin = engine.GetUsedMargin();
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

    void OpenNewPositions(SyntheticEngine& engine, double size_mult) {
        int positions_total = engine.GetPositions().size();
        double current_ask = engine.GetAsk();
        double spread = current_ask - engine.GetBid();

        if (positions_total == 0) {
            double lots = CalculateLotSize(engine, positions_total, size_mult);
            if (lots >= min_volume_) {
                double tp = current_ask + spread + base_spacing_;
                engine.OpenBuy(lots, tp);
                highest_buy_ = current_ask;
                lowest_buy_ = current_ask;
            }
        } else {
            if (lowest_buy_ >= current_ask + base_spacing_) {
                double lots = CalculateLotSize(engine, positions_total, size_mult);
                if (lots >= min_volume_) {
                    double tp = current_ask + spread + base_spacing_;
                    engine.OpenBuy(lots, tp);
                    lowest_buy_ = current_ask;
                }
            } else if (highest_buy_ <= current_ask - base_spacing_) {
                double lots = CalculateLotSize(engine, positions_total, size_mult);
                if (lots >= min_volume_) {
                    double tp = current_ask + spread + base_spacing_;
                    engine.OpenBuy(lots, tp);
                    highest_buy_ = current_ask;
                }
            }
        }
    }
};

//=============================================================================
// STRESS SCENARIO GENERATOR
//=============================================================================

class StressScenarioGenerator {
public:
    struct Tick {
        double bid;
        double ask;
    };

    StressScenarioGenerator(double start_price = 2600.0, double spread = 0.25)
        : current_price_(start_price),
          spread_(spread),
          rng_(42) {}

    void Clear() {
        ticks_.clear();
        current_price_ = 2600.0;
    }

    void SetStartPrice(double price) {
        current_price_ = price;
    }

    // Scenario 1: Flash Crash
    // -10% in 1 hour, then 50% recovery in 2 hours
    void GenerateFlashCrash() {
        Clear();
        double start_price = 2600.0;
        current_price_ = start_price;

        // 1 hour stable trading
        GenerateSideways(3600, 5.0);

        // Flash crash: -10% in 1 hour
        double crash_target = start_price * 0.90;
        GenerateCrash(3600, current_price_ - crash_target);

        double bottom = current_price_;
        double drop = start_price - bottom;

        // 50% recovery in 2 hours
        GenerateTrend(7200, drop * 0.50, 0.5);
    }

    // Scenario 2: Sustained Crash
    // -30% over 5 days, no recovery
    void GenerateSustainedCrash() {
        Clear();
        double start_price = 2600.0;
        current_price_ = start_price;

        // 5 days = 432000 seconds, use 43200 ticks (1 per 10 sec)
        double target = start_price * 0.70;
        double total_drop = current_price_ - target;

        // 10 segments, each drops ~3% with small bounces
        int ticks_per_segment = 4320;
        double drop_per_segment = total_drop / 10;

        for (int s = 0; s < 10; s++) {
            // Drop
            GenerateTrend(ticks_per_segment * 8 / 10, -drop_per_segment * 1.0, 0.3);
            // Small bounce (5% recovery of segment drop)
            GenerateTrend(ticks_per_segment * 2 / 10, drop_per_segment * 0.05, 0.1);
        }
    }

    // Scenario 3: V-Shaped Recovery
    // -20% in 2 days, +25% from bottom in 3 days
    void GenerateVRecovery() {
        Clear();
        double start_price = 2600.0;
        current_price_ = start_price;

        // Pre-crash stability (4 hours)
        GenerateSideways(1440, 3.0);

        // Down: -20% in 2 days
        double bottom_target = start_price * 0.80;
        GenerateTrend(17280, bottom_target - current_price_, 0.5);

        double bottom = current_price_;

        // Up: +25% from bottom in 3 days
        double recovery_target = bottom * 1.25;
        GenerateTrend(25920, recovery_target - current_price_, 0.5);
    }

    // Scenario 4: Choppy Decline
    // -1% per day oscillating for 30 days
    void GenerateChoppyDecline() {
        Clear();
        double start_price = 2600.0;
        current_price_ = start_price;

        int ticks_per_day = 1440;

        for (int day = 0; day < 30; day++) {
            double day_start = current_price_;
            double day_end_target = day_start * 0.99;
            double daily_drop = day_start - day_end_target;

            // 4 oscillations per day
            int cycle_ticks = ticks_per_day / 4;
            double osc_amplitude = (30 - day) * 0.5 + 5;

            for (int c = 0; c < 4; c++) {
                GenerateTrend(cycle_ticks / 2, osc_amplitude - daily_drop / 4, 0.1);
                GenerateTrend(cycle_ticks / 2, -osc_amplitude - daily_drop / 4, 0.1);
            }
        }
    }

    // Scenario 5: March 2020 style crash
    // -15% in 3 days, then violent recovery
    void GenerateMarch2020Style() {
        Clear();
        double start_price = 2600.0;
        current_price_ = start_price;

        // Day 1: -5%
        GenerateTrend(14400, -start_price * 0.05, 0.8);

        // Day 2: -7% more (accelerating)
        GenerateCrash(14400, current_price_ * 0.07);

        // Day 3: -3% more
        GenerateTrend(14400, -current_price_ * 0.03, 0.5);

        double bottom = current_price_;

        // Days 4-10: Violent +20% recovery with high volatility
        GenerateTrend(100800, bottom * 0.20, 2.0);
    }

    const std::vector<Tick>& GetTicks() const { return ticks_; }
    double GetStartPrice() const { return ticks_.empty() ? 0 : ticks_.front().bid; }
    double GetEndPrice() const { return ticks_.empty() ? 0 : ticks_.back().bid; }

private:
    double current_price_;
    double spread_;
    std::mt19937 rng_;
    std::vector<Tick> ticks_;

    void AddTick() {
        Tick t;
        t.bid = current_price_;
        t.ask = current_price_ + spread_;
        ticks_.push_back(t);
    }

    void GenerateSideways(int num_ticks, double range) {
        double center = current_price_;
        std::uniform_real_distribution<double> dist(-range / 2, range / 2);
        for (int i = 0; i < num_ticks; i++) {
            current_price_ = center + dist(rng_);
            current_price_ = std::max(100.0, current_price_);
            AddTick();
        }
    }

    void GenerateTrend(int num_ticks, double total_move, double noise) {
        if (num_ticks <= 0) return;
        double move_per_tick = total_move / num_ticks;
        std::normal_distribution<double> noise_dist(0.0, noise);
        for (int i = 0; i < num_ticks; i++) {
            current_price_ += move_per_tick + noise_dist(rng_);
            current_price_ = std::max(100.0, current_price_);
            AddTick();
        }
    }

    void GenerateCrash(int num_ticks, double total_drop) {
        // Accelerating crash - starts slow, accelerates
        for (int i = 0; i < num_ticks; i++) {
            double progress = (double)i / num_ticks;
            double acceleration = 1.0 + progress * 2.0;
            double tick_drop = (total_drop / num_ticks) * acceleration * 0.5;
            current_price_ -= tick_drop;
            current_price_ = std::max(100.0, current_price_);
            AddTick();
        }
    }
};

//=============================================================================
// TEST RUNNER
//=============================================================================

struct TestResult {
    std::string scenario;
    double start_price;
    double end_price;
    double price_change_pct;

    // Without kill switch
    double final_equity_no_ks;
    double max_dd_pct_no_ks;
    bool margin_call_no_ks;

    // With kill switch
    double final_equity_ks;
    double max_dd_pct_ks;
    bool margin_call_ks;
    int emergency_closes;
    long pause_ticks;

    // Comparison
    double equity_saved;
    double dd_reduction;
};

TestResult RunComparison(const std::string& scenario_name,
                         const std::vector<StressScenarioGenerator::Tick>& ticks,
                         double initial_balance = 10000.0) {
    TestResult result;
    result.scenario = scenario_name;
    result.start_price = ticks.front().bid;
    result.end_price = ticks.back().bid;
    result.price_change_pct = (result.end_price - result.start_price) / result.start_price * 100.0;

    // Test WITHOUT kill switch
    {
        SyntheticEngine engine(initial_balance, 100.0, 500.0);
        StrategyWithKillSwitch strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0, false);

        for (const auto& tick : ticks) {
            engine.ProcessTick(tick.bid, tick.ask);
            if (engine.IsStopOut()) break;
            strategy.OnTick(engine);
        }

        result.final_equity_no_ks = engine.GetEquity();
        result.max_dd_pct_no_ks = engine.GetMaxDrawdownPct();
        result.margin_call_no_ks = engine.IsStopOut();
    }

    // Test WITH kill switch
    {
        SyntheticEngine engine(initial_balance, 100.0, 500.0);
        KillSwitch::Config ks_config;
        // Use default config
        StrategyWithKillSwitch strategy(13.0, 1.5, 0.01, 10.0, 100.0, 500.0, true, ks_config);

        for (const auto& tick : ticks) {
            engine.ProcessTick(tick.bid, tick.ask);
            if (engine.IsStopOut()) break;
            strategy.OnTick(engine);
        }

        result.final_equity_ks = engine.GetEquity();
        result.max_dd_pct_ks = engine.GetMaxDrawdownPct();
        result.margin_call_ks = engine.IsStopOut();
        result.emergency_closes = strategy.GetEmergencyCloses();
        result.pause_ticks = strategy.GetPauseTicks();
    }

    // Calculate improvements
    result.equity_saved = result.final_equity_ks - result.final_equity_no_ks;
    result.dd_reduction = result.max_dd_pct_no_ks - result.max_dd_pct_ks;

    return result;
}

void PrintResult(const TestResult& r) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "SCENARIO: " << r.scenario << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    std::cout << "Price: $" << r.start_price << " -> $" << r.end_price
              << " (" << (r.price_change_pct >= 0 ? "+" : "") << r.price_change_pct << "%)\n" << std::endl;

    std::cout << std::setw(25) << std::left << "Metric"
              << std::setw(20) << std::right << "WITHOUT Kill Switch"
              << std::setw(20) << "WITH Kill Switch"
              << std::setw(15) << "Difference" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    std::cout << std::setw(25) << std::left << "Final Equity"
              << std::setw(19) << std::right << "$" << r.final_equity_no_ks
              << std::setw(19) << "$" << r.final_equity_ks
              << std::setw(14) << (r.equity_saved >= 0 ? "+$" : "-$") << std::abs(r.equity_saved) << std::endl;

    std::cout << std::setw(25) << std::left << "Max Drawdown"
              << std::setw(20) << std::right << std::to_string((int)r.max_dd_pct_no_ks) + "%"
              << std::setw(20) << std::to_string((int)r.max_dd_pct_ks) + "%"
              << std::setw(15) << (r.dd_reduction >= 0 ? "-" : "+") + std::to_string((int)std::abs(r.dd_reduction)) + "%" << std::endl;

    std::cout << std::setw(25) << std::left << "Margin Call"
              << std::setw(20) << std::right << (r.margin_call_no_ks ? "YES" : "NO")
              << std::setw(20) << (r.margin_call_ks ? "YES" : "NO")
              << std::setw(15) << (r.margin_call_no_ks && !r.margin_call_ks ? "PREVENTED" : "-") << std::endl;

    if (r.emergency_closes > 0 || r.pause_ticks > 0) {
        std::cout << std::string(80, '-') << std::endl;
        std::cout << "Kill Switch Actions: " << r.emergency_closes << " emergency closes, "
                  << r.pause_ticks << " ticks paused" << std::endl;
    }
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "         KILL SWITCH STRESS TEST - Strategy Protection" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "\nComparing FillUp Strategy (survive=13%, spacing=$1.5)" << std::endl;
    std::cout << "Initial Balance: $10,000 | Leverage: 1:500 | Contract: 100\n" << std::endl;

    StressScenarioGenerator generator;
    std::vector<TestResult> results;

    // Run all scenarios
    std::cout << "[1/5] Flash Crash (-10% in 1h, 50% recovery)..." << std::endl;
    generator.GenerateFlashCrash();
    results.push_back(RunComparison("Flash Crash", generator.GetTicks()));
    PrintResult(results.back());

    std::cout << "\n[2/5] Sustained Crash (-30% over 5 days)..." << std::endl;
    generator.GenerateSustainedCrash();
    results.push_back(RunComparison("Sustained Crash", generator.GetTicks()));
    PrintResult(results.back());

    std::cout << "\n[3/5] V-Shaped Recovery (-20% then +25%)..." << std::endl;
    generator.GenerateVRecovery();
    results.push_back(RunComparison("V-Shaped Recovery", generator.GetTicks()));
    PrintResult(results.back());

    std::cout << "\n[4/5] Choppy Decline (-1%/day for 30 days)..." << std::endl;
    generator.GenerateChoppyDecline();
    results.push_back(RunComparison("Choppy Decline", generator.GetTicks()));
    PrintResult(results.back());

    std::cout << "\n[5/5] March 2020 Style (-15% in 3d, violent recovery)..." << std::endl;
    generator.GenerateMarch2020Style();
    results.push_back(RunComparison("March 2020 Style", generator.GetTicks()));
    PrintResult(results.back());

    // Summary table
    std::cout << "\n\n" << std::string(100, '=') << std::endl;
    std::cout << "                                    SUMMARY TABLE" << std::endl;
    std::cout << std::string(100, '=') << std::endl;

    std::cout << std::setw(25) << std::left << "Scenario"
              << std::setw(15) << std::right << "Price Chg"
              << std::setw(15) << "No KS Equity"
              << std::setw(15) << "KS Equity"
              << std::setw(10) << "No KS DD"
              << std::setw(10) << "KS DD"
              << std::setw(12) << "Margin Call" << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    int margin_calls_prevented = 0;
    double total_equity_saved = 0.0;

    for (const auto& r : results) {
        std::string mc_status = r.margin_call_no_ks ? (r.margin_call_ks ? "BOTH" : "PREVENTED") : (r.margin_call_ks ? "NEW!" : "NONE");
        if (r.margin_call_no_ks && !r.margin_call_ks) margin_calls_prevented++;
        total_equity_saved += r.equity_saved;

        std::cout << std::setw(25) << std::left << r.scenario
                  << std::setw(14) << std::right << std::to_string((int)r.price_change_pct) + "%"
                  << std::setw(14) << "$" << (int)r.final_equity_no_ks
                  << std::setw(14) << "$" << (int)r.final_equity_ks
                  << std::setw(9) << std::to_string((int)r.max_dd_pct_no_ks) + "%"
                  << std::setw(9) << std::to_string((int)r.max_dd_pct_ks) + "%"
                  << std::setw(12) << mc_status << std::endl;
    }

    std::cout << std::string(100, '=') << std::endl;

    // Final verdict
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "                    KILL SWITCH VERDICT" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    int no_ks_margin_calls = 0, ks_margin_calls = 0;
    for (const auto& r : results) {
        if (r.margin_call_no_ks) no_ks_margin_calls++;
        if (r.margin_call_ks) ks_margin_calls++;
    }

    std::cout << "  Without Kill Switch: " << no_ks_margin_calls << "/" << results.size() << " scenarios caused margin call" << std::endl;
    std::cout << "  With Kill Switch:    " << ks_margin_calls << "/" << results.size() << " scenarios caused margin call" << std::endl;
    std::cout << "  Margin Calls Prevented: " << margin_calls_prevented << std::endl;
    std::cout << "  Total Equity Difference: " << (total_equity_saved >= 0 ? "+$" : "-$")
              << std::abs((int)total_equity_saved) << std::endl;

    if (ks_margin_calls < no_ks_margin_calls) {
        std::cout << "\n  RESULT: Kill Switch IMPROVED survival rate!" << std::endl;
    } else if (ks_margin_calls == no_ks_margin_calls && total_equity_saved > 0) {
        std::cout << "\n  RESULT: Kill Switch preserved more capital!" << std::endl;
    } else {
        std::cout << "\n  RESULT: Kill Switch needs tuning." << std::endl;
    }

    std::cout << std::string(60, '=') << std::endl;

    return (ks_margin_calls > no_ks_margin_calls) ? 1 : 0;
}
