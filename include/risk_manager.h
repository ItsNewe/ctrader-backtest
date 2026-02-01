/**
 * Risk Manager
 *
 * Comprehensive risk management for backtesting and live trading.
 * Provides position sizing, drawdown limits, and correlation checks.
 */

#ifndef RISK_MANAGER_H
#define RISK_MANAGER_H

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace backtest {

/**
 * Risk management configuration
 */
struct RiskConfig {
    // Position sizing
    double risk_per_trade_pct = 1.0;     // Max risk per trade as % of balance
    double max_position_size = 10.0;      // Maximum position size in lots
    bool use_kelly_criterion = false;     // Use Kelly criterion for sizing

    // Drawdown limits
    double max_daily_loss_pct = 5.0;      // Max daily loss as % of starting balance
    double max_total_drawdown_pct = 20.0; // Max drawdown before stopping
    double drawdown_reduction_start = 10.0; // DD% to start reducing position size
    double drawdown_reduction_factor = 0.5; // Reduce by this factor at max DD

    // Position limits
    int max_open_positions = 100;         // Maximum simultaneous positions
    int max_positions_per_symbol = 50;    // Max positions per symbol
    double max_exposure_pct = 50.0;       // Max margin usage as % of equity

    // Correlation limits
    bool check_correlation = false;       // Check position correlation
    double max_correlation = 0.7;         // Max correlation between positions
};

/**
 * Risk Manager class
 */
class RiskManager {
public:
    RiskManager(const RiskConfig& config = RiskConfig())
        : config_(config),
          daily_start_balance_(0),
          current_balance_(0),
          current_equity_(0),
          daily_pnl_(0),
          max_equity_(0),
          is_max_daily_loss_hit_(false),
          is_max_drawdown_hit_(false) {}

    // ============ Initialization ============

    /**
     * Initialize at start of day/session
     */
    void StartDay(double balance) {
        daily_start_balance_ = balance;
        daily_pnl_ = 0;
        is_max_daily_loss_hit_ = false;
    }

    /**
     * Update with current balance and equity
     */
    void Update(double balance, double equity) {
        current_balance_ = balance;
        current_equity_ = equity;
        daily_pnl_ = balance - daily_start_balance_;

        // Track max equity for drawdown
        if (equity > max_equity_) {
            max_equity_ = equity;
        }

        // Check limits
        CheckLimits();
    }

    // ============ Position Sizing ============

    /**
     * Calculate position size for given risk
     *
     * @param entry_price Expected entry price
     * @param stop_loss Stop loss price
     * @param pip_value Value of one pip in account currency
     * @param contract_size Contract size
     * @return Recommended lot size
     */
    double GetPositionSizeForRisk(double entry_price, double stop_loss,
                                   double pip_value, double contract_size) const {
        if (stop_loss <= 0 || entry_price <= 0) return 0.0;

        // Calculate risk amount
        double risk_amount = current_equity_ * (config_.risk_per_trade_pct / 100.0);

        // Apply drawdown reduction if in drawdown
        risk_amount *= GetDrawdownReductionFactor();

        // Calculate stop distance
        double stop_distance = std::abs(entry_price - stop_loss);
        if (stop_distance <= 0) return 0.0;

        // Position size = Risk Amount / (Stop Distance * Contract Size)
        double lot_size = risk_amount / (stop_distance * contract_size);

        // Apply maximum limit
        lot_size = std::min(lot_size, config_.max_position_size);

        return lot_size;
    }

    /**
     * Get drawdown-based position reduction factor
     * Returns 1.0 at no drawdown, reduces as drawdown increases
     */
    double GetDrawdownReductionFactor() const {
        double current_dd = GetCurrentDrawdownPct();
        if (current_dd <= config_.drawdown_reduction_start) {
            return 1.0;
        }

        // Linear reduction from 1.0 to reduction_factor
        double dd_range = config_.max_total_drawdown_pct - config_.drawdown_reduction_start;
        if (dd_range <= 0) return config_.drawdown_reduction_factor;

        double dd_progress = (current_dd - config_.drawdown_reduction_start) / dd_range;
        dd_progress = std::min(dd_progress, 1.0);

        return 1.0 - dd_progress * (1.0 - config_.drawdown_reduction_factor);
    }

    /**
     * Calculate Kelly Criterion optimal bet size
     *
     * @param win_rate Historical win rate (0-1)
     * @param avg_win Average winning trade
     * @param avg_loss Average losing trade (positive number)
     * @return Kelly fraction (0-1)
     */
    static double KellyCriterion(double win_rate, double avg_win, double avg_loss) {
        if (avg_loss <= 0 || win_rate <= 0 || win_rate >= 1) return 0.0;

        double win_loss_ratio = avg_win / avg_loss;
        double kelly = win_rate - ((1.0 - win_rate) / win_loss_ratio);

        // Half-Kelly is more conservative
        return std::max(0.0, kelly * 0.5);
    }

    // ============ Risk Checks ============

    /**
     * Check if a new trade is allowed
     *
     * @param symbol Symbol to trade
     * @param lot_size Proposed lot size
     * @param margin_required Margin required for position
     * @param current_positions Current number of open positions
     * @param positions_for_symbol Positions already open for this symbol
     * @param error_msg Output: error message if not allowed
     * @return true if trade is allowed
     */
    bool CanOpenTrade(const std::string& symbol, double lot_size,
                      double margin_required, int current_positions,
                      int positions_for_symbol, std::string* error_msg = nullptr) const {
        // Check max daily loss
        if (is_max_daily_loss_hit_) {
            if (error_msg) *error_msg = "Max daily loss limit reached";
            return false;
        }

        // Check max drawdown
        if (is_max_drawdown_hit_) {
            if (error_msg) *error_msg = "Max drawdown limit reached";
            return false;
        }

        // Check position count
        if (current_positions >= config_.max_open_positions) {
            if (error_msg) *error_msg = "Max open positions reached";
            return false;
        }

        // Check positions per symbol
        if (positions_for_symbol >= config_.max_positions_per_symbol) {
            if (error_msg) *error_msg = "Max positions for symbol reached";
            return false;
        }

        // Check exposure
        double exposure_pct = (margin_required / current_equity_) * 100.0;
        if (exposure_pct > config_.max_exposure_pct) {
            if (error_msg) *error_msg = "Max exposure limit exceeded";
            return false;
        }

        return true;
    }

    /**
     * Check if max daily loss has been hit
     */
    bool IsMaxDailyLossHit() const { return is_max_daily_loss_hit_; }

    /**
     * Check if max drawdown has been hit
     */
    bool IsMaxDrawdownHit() const { return is_max_drawdown_hit_; }

    // ============ Metrics ============

    /**
     * Get current drawdown percentage
     */
    double GetCurrentDrawdownPct() const {
        if (max_equity_ <= 0) return 0.0;
        return ((max_equity_ - current_equity_) / max_equity_) * 100.0;
    }

    /**
     * Get current daily P/L percentage
     */
    double GetDailyPnlPct() const {
        if (daily_start_balance_ <= 0) return 0.0;
        return (daily_pnl_ / daily_start_balance_) * 100.0;
    }

    /**
     * Get remaining daily loss allowance
     */
    double GetRemainingDailyLoss() const {
        double max_loss = daily_start_balance_ * (config_.max_daily_loss_pct / 100.0);
        return max_loss + daily_pnl_;  // daily_pnl_ is negative when losing
    }

    // ============ Configuration ============

    const RiskConfig& GetConfig() const { return config_; }
    void SetConfig(const RiskConfig& config) { config_ = config; }

    void SetMaxDailyLossPct(double pct) { config_.max_daily_loss_pct = pct; }
    void SetMaxDrawdownPct(double pct) { config_.max_total_drawdown_pct = pct; }
    void SetRiskPerTradePct(double pct) { config_.risk_per_trade_pct = pct; }
    void SetMaxPositions(int max) { config_.max_open_positions = max; }

private:
    void CheckLimits() {
        // Check daily loss
        double daily_loss_pct = -GetDailyPnlPct();  // Convert to positive loss
        if (daily_loss_pct >= config_.max_daily_loss_pct) {
            is_max_daily_loss_hit_ = true;
        }

        // Check total drawdown
        if (GetCurrentDrawdownPct() >= config_.max_total_drawdown_pct) {
            is_max_drawdown_hit_ = true;
        }
    }

    RiskConfig config_;
    double daily_start_balance_;
    double current_balance_;
    double current_equity_;
    double daily_pnl_;
    double max_equity_;
    bool is_max_daily_loss_hit_;
    bool is_max_drawdown_hit_;
};

} // namespace backtest

#endif // RISK_MANAGER_H
