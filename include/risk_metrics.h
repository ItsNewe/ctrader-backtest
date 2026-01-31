#ifndef RISK_METRICS_H
#define RISK_METRICS_H

#include <vector>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace backtest {

/**
 * Comprehensive risk metrics for strategy/portfolio analysis
 */
struct RiskReport {
    // Value at Risk
    double var_95;           // 95% VaR (historical)
    double var_99;           // 99% VaR (historical)
    double var_95_parametric; // 95% VaR (parametric/Gaussian)

    // Conditional VaR (Expected Shortfall)
    double cvar_95;          // Average loss beyond 95% VaR
    double cvar_99;          // Average loss beyond 99% VaR

    // Drawdown metrics
    double max_drawdown;
    double avg_drawdown;
    double max_drawdown_duration_days;
    double current_drawdown;
    int drawdown_count;       // Number of drawdown periods

    // Volatility metrics
    double daily_volatility;
    double annualized_volatility;
    double downside_volatility;
    double upside_volatility;

    // Tail risk
    double skewness;          // Negative = left tail risk
    double kurtosis;          // >3 = fat tails
    double tail_ratio;        // 95th percentile / abs(5th percentile)

    // Recovery metrics
    double ulcer_index;       // Measures recovery difficulty
    double pain_index;        // Average drawdown
    double pain_ratio;        // Return / Pain Index

    // Risk-adjusted returns
    double sharpe_ratio;
    double sortino_ratio;
    double calmar_ratio;
    double omega_ratio;       // Probability-weighted gain/loss ratio
    double sterling_ratio;    // Return / (Avg annual DD - 10%)

    // Stress metrics
    double worst_day;
    double worst_week;
    double worst_month;
    double best_day;
    double best_week;
    double best_month;

    // Win/Loss analysis
    double avg_win;
    double avg_loss;
    double win_loss_ratio;
    double expectancy;        // Avg win * win_rate - avg_loss * loss_rate
    double profit_factor;

    void Print() const {
        printf("\n================================================================================\n");
        printf("RISK METRICS REPORT\n");
        printf("================================================================================\n");

        printf("\n--- Value at Risk ---\n");
        printf("VaR 95%% (Historical):   %.2f%%\n", var_95 * 100);
        printf("VaR 99%% (Historical):   %.2f%%\n", var_99 * 100);
        printf("VaR 95%% (Parametric):   %.2f%%\n", var_95_parametric * 100);
        printf("CVaR 95%% (Exp. Short.): %.2f%%\n", cvar_95 * 100);
        printf("CVaR 99%% (Exp. Short.): %.2f%%\n", cvar_99 * 100);

        printf("\n--- Drawdown ---\n");
        printf("Maximum Drawdown:        %.2f%%\n", max_drawdown * 100);
        printf("Average Drawdown:        %.2f%%\n", avg_drawdown * 100);
        printf("Current Drawdown:        %.2f%%\n", current_drawdown * 100);
        printf("Drawdown Count:          %d\n", drawdown_count);
        printf("Max DD Duration (days):  %.1f\n", max_drawdown_duration_days);

        printf("\n--- Volatility ---\n");
        printf("Daily Volatility:        %.2f%%\n", daily_volatility * 100);
        printf("Annualized Volatility:   %.2f%%\n", annualized_volatility * 100);
        printf("Downside Volatility:     %.2f%%\n", downside_volatility * 100);
        printf("Upside Volatility:       %.2f%%\n", upside_volatility * 100);

        printf("\n--- Tail Risk ---\n");
        printf("Skewness:                %.3f %s\n", skewness,
               skewness < -0.5 ? "(LEFT TAIL RISK)" : skewness > 0.5 ? "(Right skewed)" : "");
        printf("Kurtosis:                %.3f %s\n", kurtosis,
               kurtosis > 4 ? "(FAT TAILS)" : kurtosis < 2 ? "(Thin tails)" : "");
        printf("Tail Ratio (95/5):       %.2f\n", tail_ratio);

        printf("\n--- Risk-Adjusted Returns ---\n");
        printf("Sharpe Ratio:            %.2f\n", sharpe_ratio);
        printf("Sortino Ratio:           %.2f\n", sortino_ratio);
        printf("Calmar Ratio:            %.2f\n", calmar_ratio);
        printf("Omega Ratio:             %.2f\n", omega_ratio);
        printf("Sterling Ratio:          %.2f\n", sterling_ratio);
        printf("Ulcer Index:             %.3f\n", ulcer_index);
        printf("Pain Index:              %.3f\n", pain_index);
        printf("Pain Ratio:              %.2f\n", pain_ratio);

        printf("\n--- Extremes ---\n");
        printf("Worst Day:               %.2f%%\n", worst_day * 100);
        printf("Worst Week:              %.2f%%\n", worst_week * 100);
        printf("Worst Month:             %.2f%%\n", worst_month * 100);
        printf("Best Day:                %.2f%%\n", best_day * 100);
        printf("Best Week:               %.2f%%\n", best_week * 100);
        printf("Best Month:              %.2f%%\n", best_month * 100);

        printf("\n--- Trade Statistics ---\n");
        printf("Average Win:             %.2f%%\n", avg_win * 100);
        printf("Average Loss:            %.2f%%\n", avg_loss * 100);
        printf("Win/Loss Ratio:          %.2f\n", win_loss_ratio);
        printf("Expectancy:              %.4f\n", expectancy);
        printf("Profit Factor:           %.2f\n", profit_factor);

        printf("\n================================================================================\n");
    }
};

/**
 * Risk metrics calculator
 */
class RiskMetricsCalculator {
public:
    /**
     * Calculate all risk metrics from returns series
     */
    static RiskReport Calculate(const std::vector<double>& returns,
                                double risk_free_rate = 0.0,
                                int periods_per_year = 252) {
        RiskReport report = {};

        if (returns.size() < 2) return report;

        std::vector<double> sorted_returns = returns;
        std::sort(sorted_returns.begin(), sorted_returns.end());

        // Basic statistics
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();

        double variance = 0;
        for (double r : returns) {
            variance += (r - mean) * (r - mean);
        }
        variance /= returns.size();
        double std_dev = sqrt(variance);

        // VaR (Historical)
        size_t idx_5 = static_cast<size_t>(returns.size() * 0.05);
        size_t idx_1 = static_cast<size_t>(returns.size() * 0.01);

        report.var_95 = -sorted_returns[idx_5];
        report.var_99 = -sorted_returns[idx_1];

        // VaR (Parametric) - assumes normal distribution
        report.var_95_parametric = -(mean - 1.645 * std_dev);

        // CVaR (Expected Shortfall)
        double sum_95 = 0, sum_99 = 0;
        for (size_t i = 0; i <= idx_5; ++i) sum_95 += sorted_returns[i];
        for (size_t i = 0; i <= idx_1; ++i) sum_99 += sorted_returns[i];

        report.cvar_95 = idx_5 > 0 ? -sum_95 / (idx_5 + 1) : 0;
        report.cvar_99 = idx_1 > 0 ? -sum_99 / (idx_1 + 1) : 0;

        // Volatility
        report.daily_volatility = std_dev;
        report.annualized_volatility = std_dev * sqrt(periods_per_year);

        // Downside/Upside volatility
        double down_var = 0, up_var = 0;
        int down_count = 0, up_count = 0;

        for (double r : returns) {
            if (r < 0) {
                down_var += r * r;
                down_count++;
            } else {
                up_var += r * r;
                up_count++;
            }
        }

        report.downside_volatility = down_count > 0 ? sqrt(down_var / down_count) : 0;
        report.upside_volatility = up_count > 0 ? sqrt(up_var / up_count) : 0;

        // Skewness and Kurtosis
        double m3 = 0, m4 = 0;
        for (double r : returns) {
            double d = (r - mean) / std_dev;
            m3 += d * d * d;
            m4 += d * d * d * d;
        }
        report.skewness = m3 / returns.size();
        report.kurtosis = m4 / returns.size();

        // Tail ratio
        size_t idx_95 = static_cast<size_t>(returns.size() * 0.95);
        double p95 = sorted_returns[idx_95];
        double p5 = sorted_returns[idx_5];
        report.tail_ratio = (p5 != 0) ? p95 / std::abs(p5) : 0;

        // Drawdown analysis
        CalculateDrawdownMetrics(returns, report);

        // Risk-adjusted returns
        double excess_mean = mean - risk_free_rate / periods_per_year;

        report.sharpe_ratio = std_dev > 0 ? (excess_mean / std_dev) * sqrt(periods_per_year) : 0;
        report.sortino_ratio = report.downside_volatility > 0
            ? (excess_mean / report.downside_volatility) * sqrt(periods_per_year) : 0;
        report.calmar_ratio = report.max_drawdown > 0
            ? (mean * periods_per_year) / report.max_drawdown : 0;

        // Omega ratio
        double gains = 0, losses = 0;
        for (double r : returns) {
            if (r > 0) gains += r;
            else losses -= r;
        }
        report.omega_ratio = losses > 0 ? gains / losses : 0;

        // Sterling ratio (using avg annual drawdown)
        double avg_annual_dd = report.avg_drawdown;  // Simplified
        report.sterling_ratio = (avg_annual_dd - 0.1) > 0
            ? (mean * periods_per_year) / (avg_annual_dd - 0.1) : 0;

        // Ulcer Index (RMS of drawdowns)
        report.pain_ratio = report.pain_index > 0
            ? (mean * periods_per_year) / report.pain_index : 0;

        // Extremes
        CalculateExtremes(returns, periods_per_year, report);

        // Win/Loss statistics
        CalculateWinLossStats(returns, report);

        return report;
    }

    /**
     * Calculate VaR using different methods
     */
    static double HistoricalVaR(const std::vector<double>& returns, double confidence = 0.95) {
        if (returns.empty()) return 0;

        std::vector<double> sorted = returns;
        std::sort(sorted.begin(), sorted.end());

        size_t idx = static_cast<size_t>(returns.size() * (1 - confidence));
        return -sorted[idx];
    }

    static double ParametricVaR(const std::vector<double>& returns, double confidence = 0.95) {
        if (returns.size() < 2) return 0;

        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        double variance = 0;
        for (double r : returns) variance += (r - mean) * (r - mean);
        double std_dev = sqrt(variance / returns.size());

        // Z-score for confidence level
        double z = 0;
        if (confidence == 0.95) z = 1.645;
        else if (confidence == 0.99) z = 2.326;
        else z = 1.645;  // Default

        return -(mean - z * std_dev);
    }

    /**
     * Calculate Expected Shortfall (CVaR)
     */
    static double ExpectedShortfall(const std::vector<double>& returns, double confidence = 0.95) {
        if (returns.empty()) return 0;

        std::vector<double> sorted = returns;
        std::sort(sorted.begin(), sorted.end());

        size_t idx = static_cast<size_t>(returns.size() * (1 - confidence));
        double sum = 0;
        for (size_t i = 0; i <= idx; ++i) {
            sum += sorted[i];
        }

        return idx > 0 ? -sum / (idx + 1) : 0;
    }

    /**
     * Calculate maximum drawdown from equity curve
     */
    static double MaxDrawdown(const std::vector<double>& equity_curve) {
        if (equity_curve.empty()) return 0;

        double peak = equity_curve[0];
        double max_dd = 0;

        for (double equity : equity_curve) {
            if (equity > peak) peak = equity;
            double dd = (peak - equity) / peak;
            if (dd > max_dd) max_dd = dd;
        }

        return max_dd;
    }

    /**
     * Calculate drawdown series from equity curve
     */
    static std::vector<double> DrawdownSeries(const std::vector<double>& equity_curve) {
        std::vector<double> drawdowns;
        if (equity_curve.empty()) return drawdowns;

        double peak = equity_curve[0];

        for (double equity : equity_curve) {
            if (equity > peak) peak = equity;
            drawdowns.push_back((peak - equity) / peak);
        }

        return drawdowns;
    }

private:
    static void CalculateDrawdownMetrics(const std::vector<double>& returns, RiskReport& report) {
        // Build equity curve from returns
        std::vector<double> equity;
        equity.push_back(1.0);

        for (double r : returns) {
            equity.push_back(equity.back() * (1 + r));
        }

        double peak = equity[0];
        double max_dd = 0;
        double sum_dd = 0;
        int dd_count = 0;
        bool in_drawdown = false;
        int dd_start = 0;
        int max_dd_duration = 0;

        for (size_t i = 0; i < equity.size(); ++i) {
            if (equity[i] > peak) {
                peak = equity[i];

                if (in_drawdown) {
                    int duration = i - dd_start;
                    if (duration > max_dd_duration) {
                        max_dd_duration = duration;
                    }
                    in_drawdown = false;
                    dd_count++;
                }
            }

            double dd = (peak - equity[i]) / peak;
            sum_dd += dd;

            if (dd > max_dd) max_dd = dd;

            if (dd > 0 && !in_drawdown) {
                in_drawdown = true;
                dd_start = i;
            }
        }

        report.max_drawdown = max_dd;
        report.avg_drawdown = equity.size() > 0 ? sum_dd / equity.size() : 0;
        report.current_drawdown = (peak - equity.back()) / peak;
        report.drawdown_count = dd_count;
        report.max_drawdown_duration_days = max_dd_duration;  // Assumes daily returns

        // Ulcer Index (RMS of drawdowns)
        double ulcer_sum = 0;
        for (size_t i = 0; i < equity.size(); ++i) {
            double dd = (peak - equity[i]) / peak;
            ulcer_sum += dd * dd;
        }
        report.ulcer_index = sqrt(ulcer_sum / equity.size());
        report.pain_index = report.avg_drawdown;
    }

    static void CalculateExtremes(const std::vector<double>& returns, int periods_per_year,
                                  RiskReport& report) {
        report.worst_day = *std::min_element(returns.begin(), returns.end());
        report.best_day = *std::max_element(returns.begin(), returns.end());

        // Weekly returns (5 periods)
        if (returns.size() >= 5) {
            std::vector<double> weekly;
            for (size_t i = 4; i < returns.size(); i += 5) {
                double prod = 1;
                for (size_t j = i - 4; j <= i; ++j) {
                    prod *= (1 + returns[j]);
                }
                weekly.push_back(prod - 1);
            }
            if (!weekly.empty()) {
                report.worst_week = *std::min_element(weekly.begin(), weekly.end());
                report.best_week = *std::max_element(weekly.begin(), weekly.end());
            }
        }

        // Monthly returns (21 periods)
        if (returns.size() >= 21) {
            std::vector<double> monthly;
            for (size_t i = 20; i < returns.size(); i += 21) {
                double prod = 1;
                for (size_t j = i - 20; j <= i; ++j) {
                    prod *= (1 + returns[j]);
                }
                monthly.push_back(prod - 1);
            }
            if (!monthly.empty()) {
                report.worst_month = *std::min_element(monthly.begin(), monthly.end());
                report.best_month = *std::max_element(monthly.begin(), monthly.end());
            }
        }
    }

    static void CalculateWinLossStats(const std::vector<double>& returns, RiskReport& report) {
        double sum_wins = 0, sum_losses = 0;
        int wins = 0, losses = 0;

        for (double r : returns) {
            if (r > 0) {
                sum_wins += r;
                wins++;
            } else if (r < 0) {
                sum_losses -= r;  // Make positive
                losses++;
            }
        }

        report.avg_win = wins > 0 ? sum_wins / wins : 0;
        report.avg_loss = losses > 0 ? sum_losses / losses : 0;
        report.win_loss_ratio = report.avg_loss > 0 ? report.avg_win / report.avg_loss : 0;

        double win_rate = returns.size() > 0 ? static_cast<double>(wins) / returns.size() : 0;
        double loss_rate = returns.size() > 0 ? static_cast<double>(losses) / returns.size() : 0;

        report.expectancy = report.avg_win * win_rate - report.avg_loss * loss_rate;
        report.profit_factor = sum_losses > 0 ? sum_wins / sum_losses : 0;
    }
};

}  // namespace backtest

#endif  // RISK_METRICS_H
