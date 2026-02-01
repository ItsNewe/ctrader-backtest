/**
 * MT5 Statistics and Optimization Criteria
 *
 * Implements all statistics and optimization criteria available in
 * MetaTrader 5 Strategy Tester.
 */

#ifndef MT5_STATISTICS_H
#define MT5_STATISTICS_H

#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>

namespace backtest {

/**
 * MT5 Optimization Criteria (matching ENUM_STATISTICS)
 */
enum class OptimizationCriterion {
    // Profit-based
    STAT_BALANCE_MIN = 0,           // Minimum balance value
    STAT_BALANCEMAX = 1,            // Maximum balance value (same as profit)
    STAT_PROFIT = 2,                // Net profit
    STAT_GROSS_PROFIT = 3,          // Gross profit
    STAT_GROSS_LOSS = 4,            // Gross loss
    STAT_MAX_PROFITTRADE = 5,       // Maximum profit in a trade
    STAT_MAX_LOSSTRADE = 6,         // Maximum loss in a trade
    STAT_CONPROFITMAX = 7,          // Maximum consecutive profit
    STAT_CONPROFITMAX_TRADES = 8,   // Trades in max consecutive profit
    STAT_MAX_CONWINS = 9,           // Maximum consecutive wins
    STAT_MAX_CONPROFIT_TRADES = 10, // Trades in max consecutive wins
    STAT_CONLOSSMAX = 11,           // Maximum consecutive loss
    STAT_CONLOSSMAX_TRADES = 12,    // Trades in max consecutive loss
    STAT_MAX_CONLOSSES = 13,        // Maximum consecutive losses
    STAT_MAX_CONLOSS_TRADES = 14,   // Trades in max consecutive losses

    // Trade counts
    STAT_TRADES = 15,               // Total trades
    STAT_PROFIT_TRADES = 16,        // Profitable trades
    STAT_LOSS_TRADES = 17,          // Losing trades
    STAT_SHORT_TRADES = 18,         // Short trades
    STAT_LONG_TRADES = 19,          // Long trades
    STAT_PROFIT_SHORTTRADES = 20,   // Profitable short trades
    STAT_PROFIT_LONGTRADES = 21,    // Profitable long trades

    // Drawdown
    STAT_EQUITY_DD = 22,            // Max equity drawdown (absolute)
    STAT_EQUITYDD_PERCENT = 23,     // Max equity drawdown (percent)
    STAT_EQUITY_DDREL_PERCENT = 24, // Max equity relative drawdown (percent)
    STAT_EQUITY_DD_RELATIVE = 25,   // Max equity relative drawdown (absolute)
    STAT_BALANCE_DD = 26,           // Max balance drawdown (absolute)
    STAT_BALANCEDD_PERCENT = 27,    // Max balance drawdown (percent)
    STAT_BALANCE_DDREL_PERCENT = 28,// Max balance relative drawdown (percent)
    STAT_BALANCE_DD_RELATIVE = 29,  // Max balance relative drawdown (absolute)

    // Ratios and factors
    STAT_EXPECTED_PAYOFF = 30,      // Expected payoff
    STAT_PROFIT_FACTOR = 31,        // Profit factor
    STAT_RECOVERY_FACTOR = 32,      // Recovery factor
    STAT_SHARPE_RATIO = 33,         // Sharpe ratio
    STAT_MIN_MARGINLEVEL = 34,      // Minimum margin level
    STAT_CUSTOM_ONTESTER = 35,      // Custom OnTester result

    // Extended metrics (not in standard MT5)
    STAT_SORTINO_RATIO = 100,       // Sortino ratio
    STAT_CALMAR_RATIO = 101,        // Calmar ratio (annual return / max DD)
    STAT_STERLING_RATIO = 102,      // Sterling ratio
    STAT_MAR_RATIO = 103,           // MAR ratio
    STAT_ULCER_INDEX = 104,         // Ulcer index
    STAT_PAIN_INDEX = 105,          // Pain index
    STAT_PAIN_RATIO = 106,          // Pain ratio
    STAT_BURKE_RATIO = 107,         // Burke ratio
    STAT_MARTIN_RATIO = 108,        // Martin ratio (Ulcer performance)
    STAT_TREYNOR_RATIO = 109,       // Treynor ratio
    STAT_INFORMATION_RATIO = 110,   // Information ratio
    STAT_OMEGA_RATIO = 111,         // Omega ratio
    STAT_TAIL_RATIO = 112,          // Tail ratio
    STAT_COMMON_SENSE_RATIO = 113,  // Common sense ratio
    STAT_CPC_INDEX = 114,           // CPC index (consistency)
    STAT_KESTNER_RATIO = 115,       // K-ratio
    STAT_SQRTN = 116,               // SQN (System Quality Number)
    STAT_VAR_95 = 117,              // Value at Risk 95%
    STAT_VAR_99 = 118,              // Value at Risk 99%
    STAT_CVAR_95 = 119,             // Conditional VaR 95%
    STAT_CVAR_99 = 120,             // Conditional VaR 99%
};

/**
 * Trade record for statistics calculation
 */
struct Trade {
    int64_t open_time;
    int64_t close_time;
    double open_price;
    double close_price;
    double volume;
    bool is_buy;
    double profit;
    double commission;
    double swap;
    double balance_after;
    double equity_after;
};

/**
 * Complete MT5-compatible statistics
 */
struct MT5Statistics {
    // Profit metrics
    double initial_deposit = 0;
    double withdrawal = 0;
    double profit = 0;
    double gross_profit = 0;
    double gross_loss = 0;
    double max_profit_trade = 0;
    double max_loss_trade = 0;
    double avg_profit_trade = 0;
    double avg_loss_trade = 0;

    // Consecutive trades
    double con_profit_max = 0;
    int con_profit_max_trades = 0;
    int max_con_wins = 0;
    int max_con_profit_trades = 0;
    double con_loss_max = 0;
    int con_loss_max_trades = 0;
    int max_con_losses = 0;
    int max_con_loss_trades = 0;

    // Trade counts
    int total_trades = 0;
    int profit_trades = 0;
    int loss_trades = 0;
    int short_trades = 0;
    int long_trades = 0;
    int profit_short_trades = 0;
    int profit_long_trades = 0;

    // Balance/Equity
    double balance_min = 0;
    double balance_max = 0;
    double balance_dd = 0;
    double balance_dd_percent = 0;
    double balance_dd_relative = 0;
    double balance_dd_rel_percent = 0;

    double equity_min = 0;
    double equity_max = 0;
    double equity_dd = 0;
    double equity_dd_percent = 0;
    double equity_dd_relative = 0;
    double equity_dd_rel_percent = 0;

    // Ratios
    double profit_factor = 0;
    double expected_payoff = 0;
    double recovery_factor = 0;
    double sharpe_ratio = 0;
    double sortino_ratio = 0;
    double calmar_ratio = 0;

    // Margin
    double min_margin_level = 0;
    double max_margin_used = 0;

    // Time stats
    double avg_trade_duration = 0;  // seconds
    double avg_winning_duration = 0;
    double avg_losing_duration = 0;

    // Win rates
    double win_rate = 0;
    double win_rate_long = 0;
    double win_rate_short = 0;

    // Custom
    double custom_result = 0;

    // Extended ratios
    double sterling_ratio = 0;
    double mar_ratio = 0;
    double ulcer_index = 0;
    double pain_index = 0;
    double pain_ratio = 0;
    double burke_ratio = 0;
    double martin_ratio = 0;
    double omega_ratio = 0;
    double tail_ratio = 0;
    double common_sense_ratio = 0;
    double cpc_index = 0;
    double kestner_ratio = 0;
    double sqn = 0;
    double var_95 = 0;
    double var_99 = 0;
    double cvar_95 = 0;
    double cvar_99 = 0;
};

/**
 * MT5 Statistics Calculator
 */
class MT5StatisticsCalculator {
public:
    /**
     * Calculate all statistics from trade history
     */
    static MT5Statistics Calculate(
        const std::vector<Trade>& trades,
        double initial_balance,
        double risk_free_rate = 0.02,
        int trading_days_per_year = 252)
    {
        MT5Statistics stats;
        stats.initial_deposit = initial_balance;

        if (trades.empty()) {
            return stats;
        }

        // Build equity and balance curves
        std::vector<double> equity_curve;
        std::vector<double> balance_curve;
        std::vector<double> returns;

        double balance = initial_balance;
        double equity = initial_balance;
        double max_balance = initial_balance;
        double max_equity = initial_balance;

        balance_curve.push_back(initial_balance);
        equity_curve.push_back(initial_balance);

        // Consecutive tracking
        int current_wins = 0;
        int current_losses = 0;
        double current_con_profit = 0;
        double current_con_loss = 0;

        for (const auto& trade : trades) {
            double net_profit = trade.profit + trade.commission + trade.swap;

            // Update counts
            stats.total_trades++;
            if (trade.is_buy) stats.long_trades++;
            else stats.short_trades++;

            if (net_profit > 0) {
                stats.profit_trades++;
                stats.gross_profit += net_profit;
                if (trade.is_buy) stats.profit_long_trades++;
                else stats.profit_short_trades++;

                if (net_profit > stats.max_profit_trade) {
                    stats.max_profit_trade = net_profit;
                }

                // Consecutive wins
                current_wins++;
                current_con_profit += net_profit;
                current_losses = 0;

                if (current_wins > stats.max_con_wins) {
                    stats.max_con_wins = current_wins;
                    stats.max_con_profit_trades = current_wins;
                }
                if (current_con_profit > stats.con_profit_max) {
                    stats.con_profit_max = current_con_profit;
                    stats.con_profit_max_trades = current_wins;
                }
            } else if (net_profit < 0) {
                stats.loss_trades++;
                stats.gross_loss += std::abs(net_profit);

                if (net_profit < stats.max_loss_trade) {
                    stats.max_loss_trade = net_profit;
                }

                // Consecutive losses
                current_losses++;
                current_con_loss += std::abs(net_profit);
                current_wins = 0;
                current_con_profit = 0;

                if (current_losses > stats.max_con_losses) {
                    stats.max_con_losses = current_losses;
                    stats.max_con_loss_trades = current_losses;
                }
                if (current_con_loss > stats.con_loss_max) {
                    stats.con_loss_max = current_con_loss;
                    stats.con_loss_max_trades = current_losses;
                }
            }

            // Update balance
            double prev_balance = balance;
            balance += net_profit;
            balance_curve.push_back(balance);

            if (balance > max_balance) max_balance = balance;
            if (balance < stats.balance_min || stats.balance_min == 0) {
                stats.balance_min = balance;
            }

            // Balance drawdown
            double bal_dd = max_balance - balance;
            if (bal_dd > stats.balance_dd) {
                stats.balance_dd = bal_dd;
                stats.balance_dd_percent = (max_balance > 0) ?
                    bal_dd / max_balance * 100 : 0;
            }

            // Track relative drawdown (DD at time it occurred)
            if (max_balance > 0) {
                double rel_dd_pct = bal_dd / max_balance * 100;
                if (rel_dd_pct > stats.balance_dd_rel_percent) {
                    stats.balance_dd_rel_percent = rel_dd_pct;
                    stats.balance_dd_relative = bal_dd;
                }
            }

            // Update equity (using balance as proxy if no floating P/L)
            equity = trade.equity_after > 0 ? trade.equity_after : balance;
            equity_curve.push_back(equity);

            if (equity > max_equity) max_equity = equity;
            if (equity < stats.equity_min || stats.equity_min == 0) {
                stats.equity_min = equity;
            }

            double eq_dd = max_equity - equity;
            if (eq_dd > stats.equity_dd) {
                stats.equity_dd = eq_dd;
                stats.equity_dd_percent = (max_equity > 0) ?
                    eq_dd / max_equity * 100 : 0;
            }

            // Calculate return
            if (prev_balance > 0) {
                returns.push_back(net_profit / prev_balance);
            }

            // Trade duration
            double duration = static_cast<double>(trade.close_time - trade.open_time);
            stats.avg_trade_duration += duration;
            if (net_profit > 0) {
                stats.avg_winning_duration += duration;
            } else {
                stats.avg_losing_duration += duration;
            }
        }

        // Final values
        stats.balance_max = max_balance;
        stats.equity_max = max_equity;
        stats.profit = balance - initial_balance;

        // Averages
        if (stats.profit_trades > 0) {
            stats.avg_profit_trade = stats.gross_profit / stats.profit_trades;
            stats.avg_winning_duration /= stats.profit_trades;
        }
        if (stats.loss_trades > 0) {
            stats.avg_loss_trade = stats.gross_loss / stats.loss_trades;
            stats.avg_losing_duration /= stats.loss_trades;
        }
        if (stats.total_trades > 0) {
            stats.avg_trade_duration /= stats.total_trades;
        }

        // Win rates
        stats.win_rate = stats.total_trades > 0 ?
            static_cast<double>(stats.profit_trades) / stats.total_trades * 100 : 0;
        stats.win_rate_long = stats.long_trades > 0 ?
            static_cast<double>(stats.profit_long_trades) / stats.long_trades * 100 : 0;
        stats.win_rate_short = stats.short_trades > 0 ?
            static_cast<double>(stats.profit_short_trades) / stats.short_trades * 100 : 0;

        // Profit factor
        stats.profit_factor = stats.gross_loss > 0 ?
            stats.gross_profit / stats.gross_loss : 0;

        // Expected payoff
        stats.expected_payoff = stats.total_trades > 0 ?
            stats.profit / stats.total_trades : 0;

        // Recovery factor
        stats.recovery_factor = stats.balance_dd > 0 ?
            stats.profit / stats.balance_dd : 0;

        // Calculate risk-adjusted metrics
        CalculateRiskMetrics(stats, returns, trading_days_per_year, risk_free_rate);

        return stats;
    }

    /**
     * Get value for specific optimization criterion
     */
    static double GetCriterionValue(const MT5Statistics& stats,
                                    OptimizationCriterion criterion)
    {
        switch (criterion) {
            case OptimizationCriterion::STAT_BALANCE_MIN:
                return stats.balance_min;
            case OptimizationCriterion::STAT_BALANCEMAX:
                return stats.balance_max;
            case OptimizationCriterion::STAT_PROFIT:
                return stats.profit;
            case OptimizationCriterion::STAT_GROSS_PROFIT:
                return stats.gross_profit;
            case OptimizationCriterion::STAT_GROSS_LOSS:
                return -stats.gross_loss;  // Negative for minimize
            case OptimizationCriterion::STAT_MAX_PROFITTRADE:
                return stats.max_profit_trade;
            case OptimizationCriterion::STAT_MAX_LOSSTRADE:
                return stats.max_loss_trade;
            case OptimizationCriterion::STAT_CONPROFITMAX:
                return stats.con_profit_max;
            case OptimizationCriterion::STAT_CONPROFITMAX_TRADES:
                return stats.con_profit_max_trades;
            case OptimizationCriterion::STAT_MAX_CONWINS:
                return stats.max_con_wins;
            case OptimizationCriterion::STAT_MAX_CONPROFIT_TRADES:
                return stats.max_con_profit_trades;
            case OptimizationCriterion::STAT_CONLOSSMAX:
                return -stats.con_loss_max;  // Negative for minimize
            case OptimizationCriterion::STAT_CONLOSSMAX_TRADES:
                return -stats.con_loss_max_trades;
            case OptimizationCriterion::STAT_MAX_CONLOSSES:
                return -stats.max_con_losses;
            case OptimizationCriterion::STAT_MAX_CONLOSS_TRADES:
                return -stats.max_con_loss_trades;
            case OptimizationCriterion::STAT_TRADES:
                return stats.total_trades;
            case OptimizationCriterion::STAT_PROFIT_TRADES:
                return stats.profit_trades;
            case OptimizationCriterion::STAT_LOSS_TRADES:
                return -stats.loss_trades;
            case OptimizationCriterion::STAT_SHORT_TRADES:
                return stats.short_trades;
            case OptimizationCriterion::STAT_LONG_TRADES:
                return stats.long_trades;
            case OptimizationCriterion::STAT_PROFIT_SHORTTRADES:
                return stats.profit_short_trades;
            case OptimizationCriterion::STAT_PROFIT_LONGTRADES:
                return stats.profit_long_trades;
            case OptimizationCriterion::STAT_EQUITY_DD:
                return -stats.equity_dd;
            case OptimizationCriterion::STAT_EQUITYDD_PERCENT:
                return -stats.equity_dd_percent;
            case OptimizationCriterion::STAT_EQUITY_DDREL_PERCENT:
                return -stats.equity_dd_rel_percent;
            case OptimizationCriterion::STAT_EQUITY_DD_RELATIVE:
                return -stats.equity_dd_relative;
            case OptimizationCriterion::STAT_BALANCE_DD:
                return -stats.balance_dd;
            case OptimizationCriterion::STAT_BALANCEDD_PERCENT:
                return -stats.balance_dd_percent;
            case OptimizationCriterion::STAT_BALANCE_DDREL_PERCENT:
                return -stats.balance_dd_rel_percent;
            case OptimizationCriterion::STAT_BALANCE_DD_RELATIVE:
                return -stats.balance_dd_relative;
            case OptimizationCriterion::STAT_EXPECTED_PAYOFF:
                return stats.expected_payoff;
            case OptimizationCriterion::STAT_PROFIT_FACTOR:
                return stats.profit_factor;
            case OptimizationCriterion::STAT_RECOVERY_FACTOR:
                return stats.recovery_factor;
            case OptimizationCriterion::STAT_SHARPE_RATIO:
                return stats.sharpe_ratio;
            case OptimizationCriterion::STAT_MIN_MARGINLEVEL:
                return stats.min_margin_level;
            case OptimizationCriterion::STAT_CUSTOM_ONTESTER:
                return stats.custom_result;
            case OptimizationCriterion::STAT_SORTINO_RATIO:
                return stats.sortino_ratio;
            case OptimizationCriterion::STAT_CALMAR_RATIO:
                return stats.calmar_ratio;
            case OptimizationCriterion::STAT_STERLING_RATIO:
                return stats.sterling_ratio;
            case OptimizationCriterion::STAT_MAR_RATIO:
                return stats.mar_ratio;
            case OptimizationCriterion::STAT_ULCER_INDEX:
                return -stats.ulcer_index;  // Lower is better
            case OptimizationCriterion::STAT_PAIN_INDEX:
                return -stats.pain_index;
            case OptimizationCriterion::STAT_PAIN_RATIO:
                return stats.pain_ratio;
            case OptimizationCriterion::STAT_BURKE_RATIO:
                return stats.burke_ratio;
            case OptimizationCriterion::STAT_MARTIN_RATIO:
                return stats.martin_ratio;
            case OptimizationCriterion::STAT_OMEGA_RATIO:
                return stats.omega_ratio;
            case OptimizationCriterion::STAT_TAIL_RATIO:
                return stats.tail_ratio;
            case OptimizationCriterion::STAT_COMMON_SENSE_RATIO:
                return stats.common_sense_ratio;
            case OptimizationCriterion::STAT_CPC_INDEX:
                return stats.cpc_index;
            case OptimizationCriterion::STAT_KESTNER_RATIO:
                return stats.kestner_ratio;
            case OptimizationCriterion::STAT_SQRTN:
                return stats.sqn;
            case OptimizationCriterion::STAT_VAR_95:
                return -stats.var_95;  // Lower (less negative) is better
            case OptimizationCriterion::STAT_VAR_99:
                return -stats.var_99;
            case OptimizationCriterion::STAT_CVAR_95:
                return -stats.cvar_95;
            case OptimizationCriterion::STAT_CVAR_99:
                return -stats.cvar_99;
            default:
                return 0;
        }
    }

private:
    static void CalculateRiskMetrics(
        MT5Statistics& stats,
        const std::vector<double>& returns,
        int trading_days_per_year,
        double risk_free_rate)
    {
        if (returns.empty()) return;

        int n = static_cast<int>(returns.size());

        // Mean return
        double mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / n;

        // Standard deviation
        double sum_sq = 0;
        for (double r : returns) {
            sum_sq += (r - mean_return) * (r - mean_return);
        }
        double std_dev = std::sqrt(sum_sq / n);

        // Downside deviation (for Sortino)
        double sum_sq_down = 0;
        int down_count = 0;
        for (double r : returns) {
            if (r < 0) {
                sum_sq_down += r * r;
                down_count++;
            }
        }
        double downside_dev = down_count > 0 ?
            std::sqrt(sum_sq_down / down_count) : 0;

        // Annualization factor
        double annual_factor = std::sqrt(static_cast<double>(trading_days_per_year));
        double daily_rf = risk_free_rate / trading_days_per_year;

        // Sharpe Ratio
        if (std_dev > 0) {
            stats.sharpe_ratio = (mean_return - daily_rf) / std_dev * annual_factor;
        }

        // Sortino Ratio
        if (downside_dev > 0) {
            stats.sortino_ratio = (mean_return - daily_rf) / downside_dev * annual_factor;
        }

        // Calmar Ratio (annual return / max DD)
        double annual_return = mean_return * trading_days_per_year;
        if (stats.balance_dd_percent > 0) {
            stats.calmar_ratio = (annual_return * 100) / stats.balance_dd_percent;
        }

        // Sterling Ratio (annual return / (avg DD + 10%))
        double avg_dd = stats.balance_dd_percent / 2;  // Approximation
        if (avg_dd + 10 > 0) {
            stats.sterling_ratio = (annual_return * 100) / (avg_dd + 10);
        }

        // MAR Ratio (same as Calmar but with different calculation)
        stats.mar_ratio = stats.calmar_ratio;

        // Sort returns for VaR calculations
        std::vector<double> sorted_returns = returns;
        std::sort(sorted_returns.begin(), sorted_returns.end());

        // VaR 95% and 99%
        int var95_idx = static_cast<int>(n * 0.05);
        int var99_idx = static_cast<int>(n * 0.01);
        stats.var_95 = sorted_returns[std::max(0, var95_idx)] * stats.initial_deposit;
        stats.var_99 = sorted_returns[std::max(0, var99_idx)] * stats.initial_deposit;

        // CVaR (Expected Shortfall)
        double sum_95 = 0, sum_99 = 0;
        for (int i = 0; i <= var95_idx; ++i) {
            sum_95 += sorted_returns[i];
        }
        for (int i = 0; i <= var99_idx; ++i) {
            sum_99 += sorted_returns[i];
        }
        stats.cvar_95 = var95_idx > 0 ? (sum_95 / (var95_idx + 1)) * stats.initial_deposit : 0;
        stats.cvar_99 = var99_idx > 0 ? (sum_99 / (var99_idx + 1)) * stats.initial_deposit : 0;

        // Omega Ratio
        double threshold = 0;  // Usually 0 or risk-free rate
        double sum_above = 0, sum_below = 0;
        for (double r : returns) {
            if (r > threshold) sum_above += (r - threshold);
            else sum_below += (threshold - r);
        }
        stats.omega_ratio = sum_below > 0 ? sum_above / sum_below : 0;

        // Tail Ratio (95th percentile / 5th percentile absolute)
        double p95 = sorted_returns[static_cast<int>(n * 0.95)];
        double p5 = sorted_returns[var95_idx];
        if (std::abs(p5) > 0) {
            stats.tail_ratio = std::abs(p95 / p5);
        }

        // Common Sense Ratio (Tail ratio * Profit factor)
        stats.common_sense_ratio = stats.tail_ratio * stats.profit_factor;

        // SQN (System Quality Number)
        // SQN = sqrt(N) * (average trade / std dev of trades)
        if (std_dev > 0 && n > 0) {
            stats.sqn = std::sqrt(static_cast<double>(n)) * (mean_return / std_dev);
        }

        // Ulcer Index (measures depth and duration of drawdowns)
        // UI = sqrt(sum of squared DD percentages / N)
        double sum_dd_sq = 0;
        double running_max = stats.initial_deposit;
        for (const double& r : returns) {
            running_max = std::max(running_max, running_max * (1 + r));
            double dd_pct = (running_max - running_max * (1 + r)) / running_max * 100;
            sum_dd_sq += dd_pct * dd_pct;
        }
        stats.ulcer_index = std::sqrt(sum_dd_sq / n);

        // Martin Ratio (Ulcer Performance Index)
        if (stats.ulcer_index > 0) {
            stats.martin_ratio = (annual_return * 100) / stats.ulcer_index;
        }

        // Pain Index (average DD percentage)
        double sum_dd = 0;
        running_max = stats.initial_deposit;
        for (const double& r : returns) {
            running_max = std::max(running_max, running_max * (1 + r));
            double dd_pct = (running_max - running_max * (1 + r)) / running_max * 100;
            sum_dd += dd_pct;
        }
        stats.pain_index = sum_dd / n;

        // Pain Ratio
        if (stats.pain_index > 0) {
            stats.pain_ratio = (annual_return * 100) / stats.pain_index;
        }

        // Burke Ratio (using squared DDs)
        if (sum_dd_sq > 0) {
            stats.burke_ratio = (annual_return * 100) / std::sqrt(sum_dd_sq);
        }

        // K-Ratio (Kestner Ratio) - slope of equity curve / std error
        // Linear regression on cumulative returns
        double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
        double cum_return = 0;
        for (int i = 0; i < n; ++i) {
            cum_return += returns[i];
            sum_x += i;
            sum_y += cum_return;
            sum_xy += i * cum_return;
            sum_x2 += i * i;
        }
        double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);

        // Standard error of slope
        double y_pred_sq_err = 0;
        for (int i = 0; i < n; ++i) {
            double y_pred = slope * i;
            double err = (std::accumulate(returns.begin(), returns.begin() + i + 1, 0.0)) - y_pred;
            y_pred_sq_err += err * err;
        }
        double std_err = std::sqrt(y_pred_sq_err / (n - 2)) /
                         std::sqrt(sum_x2 - (sum_x * sum_x) / n);

        if (std_err > 0) {
            stats.kestner_ratio = slope / std_err;
        }

        // CPC Index (Consistency/Profit/Contribution)
        // CPC = (win_rate / 100) * profit_factor * (1 - DD%/100)
        stats.cpc_index = (stats.win_rate / 100.0) * stats.profit_factor *
                          (1.0 - stats.balance_dd_percent / 100.0);
    }
};

} // namespace backtest

#endif // MT5_STATISTICS_H
