#ifndef CORRELATION_ANALYZER_H
#define CORRELATION_ANALYZER_H

#include <vector>
#include <map>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace backtest {

/**
 * Correlation analysis between strategies/symbols
 */
struct CorrelationMatrix {
    std::vector<std::string> labels;
    std::vector<std::vector<double>> matrix;

    double Get(const std::string& a, const std::string& b) const {
        auto it_a = std::find(labels.begin(), labels.end(), a);
        auto it_b = std::find(labels.begin(), labels.end(), b);
        if (it_a == labels.end() || it_b == labels.end()) return 0;

        size_t i = it_a - labels.begin();
        size_t j = it_b - labels.begin();
        return matrix[i][j];
    }

    void Print() const {
        // Header
        printf("%12s", "");
        for (const auto& label : labels) {
            printf("%12s", label.substr(0, 10).c_str());
        }
        printf("\n");

        // Matrix
        for (size_t i = 0; i < labels.size(); ++i) {
            printf("%12s", labels[i].substr(0, 10).c_str());
            for (size_t j = 0; j < labels.size(); ++j) {
                printf("%12.4f", matrix[i][j]);
            }
            printf("\n");
        }
    }
};

/**
 * Rolling correlation result
 */
struct RollingCorrelation {
    std::vector<int64_t> timestamps;
    std::vector<double> correlations;
    double mean_correlation;
    double min_correlation;
    double max_correlation;
    double correlation_stability;  // 1 - std_dev / range
};

/**
 * Diversification metrics
 */
struct DiversificationMetrics {
    double diversification_ratio;      // Portfolio vol / weighted avg vol
    double concentration_hhi;          // Herfindahl-Hirschman Index
    double effective_n;                // 1 / HHI (effective number of bets)
    double marginal_contribution_risk; // % contribution to total risk
    std::map<std::string, double> risk_contributions;
};

/**
 * Correlation analyzer for portfolio and strategy analysis
 */
class CorrelationAnalyzer {
public:
    /**
     * Calculate Pearson correlation between two return series
     */
    static double PearsonCorrelation(const std::vector<double>& x,
                                     const std::vector<double>& y) {
        if (x.size() != y.size() || x.size() < 2) return 0;

        size_t n = x.size();

        double mean_x = std::accumulate(x.begin(), x.end(), 0.0) / n;
        double mean_y = std::accumulate(y.begin(), y.end(), 0.0) / n;

        double cov = 0, var_x = 0, var_y = 0;
        for (size_t i = 0; i < n; ++i) {
            double dx = x[i] - mean_x;
            double dy = y[i] - mean_y;
            cov += dx * dy;
            var_x += dx * dx;
            var_y += dy * dy;
        }

        if (var_x == 0 || var_y == 0) return 0;

        return cov / sqrt(var_x * var_y);
    }

    /**
     * Calculate Spearman rank correlation (more robust to outliers)
     */
    static double SpearmanCorrelation(const std::vector<double>& x,
                                      const std::vector<double>& y) {
        if (x.size() != y.size() || x.size() < 2) return 0;

        // Convert to ranks
        auto rank = [](const std::vector<double>& v) {
            std::vector<size_t> indices(v.size());
            std::iota(indices.begin(), indices.end(), 0);
            std::sort(indices.begin(), indices.end(),
                [&v](size_t a, size_t b) { return v[a] < v[b]; });

            std::vector<double> ranks(v.size());
            for (size_t i = 0; i < indices.size(); ++i) {
                ranks[indices[i]] = i + 1;
            }
            return ranks;
        };

        return PearsonCorrelation(rank(x), rank(y));
    }

    /**
     * Build correlation matrix from multiple return series
     */
    static CorrelationMatrix BuildMatrix(
        const std::map<std::string, std::vector<double>>& returns,
        bool use_spearman = false) {

        CorrelationMatrix result;

        for (const auto& [label, _] : returns) {
            result.labels.push_back(label);
        }

        size_t n = result.labels.size();
        result.matrix.resize(n, std::vector<double>(n, 0));

        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                if (i == j) {
                    result.matrix[i][j] = 1.0;
                } else if (j > i) {
                    const auto& x = returns.at(result.labels[i]);
                    const auto& y = returns.at(result.labels[j]);

                    result.matrix[i][j] = use_spearman
                        ? SpearmanCorrelation(x, y)
                        : PearsonCorrelation(x, y);
                    result.matrix[j][i] = result.matrix[i][j];
                }
            }
        }

        return result;
    }

    /**
     * Calculate rolling correlation between two series
     */
    static RollingCorrelation RollingPearson(
        const std::vector<double>& x,
        const std::vector<double>& y,
        const std::vector<int64_t>& timestamps,
        size_t window) {

        RollingCorrelation result;

        if (x.size() != y.size() || x.size() < window) {
            return result;
        }

        for (size_t i = window; i <= x.size(); ++i) {
            std::vector<double> win_x(x.begin() + i - window, x.begin() + i);
            std::vector<double> win_y(y.begin() + i - window, y.begin() + i);

            result.correlations.push_back(PearsonCorrelation(win_x, win_y));
            result.timestamps.push_back(timestamps[i - 1]);
        }

        if (!result.correlations.empty()) {
            result.mean_correlation = std::accumulate(
                result.correlations.begin(), result.correlations.end(), 0.0)
                / result.correlations.size();

            result.min_correlation = *std::min_element(
                result.correlations.begin(), result.correlations.end());
            result.max_correlation = *std::max_element(
                result.correlations.begin(), result.correlations.end());

            // Stability: 1 - normalized std dev
            double variance = 0;
            for (double c : result.correlations) {
                variance += (c - result.mean_correlation) * (c - result.mean_correlation);
            }
            double std_dev = sqrt(variance / result.correlations.size());
            double range = result.max_correlation - result.min_correlation;

            result.correlation_stability = range > 0 ? 1.0 - std_dev / range : 1.0;
        }

        return result;
    }

    /**
     * Calculate diversification metrics for a portfolio
     */
    static DiversificationMetrics CalculateDiversification(
        const std::map<std::string, std::vector<double>>& returns,
        const std::map<std::string, double>& weights) {

        DiversificationMetrics result;

        // Calculate individual volatilities
        std::map<std::string, double> vols;
        double weighted_avg_vol = 0;

        for (const auto& [label, rets] : returns) {
            double mean = std::accumulate(rets.begin(), rets.end(), 0.0) / rets.size();
            double variance = 0;
            for (double r : rets) {
                variance += (r - mean) * (r - mean);
            }
            double vol = sqrt(variance / rets.size());
            vols[label] = vol;

            if (weights.count(label)) {
                weighted_avg_vol += weights.at(label) * vol;
            }
        }

        // Calculate portfolio volatility (accounting for correlations)
        double portfolio_variance = 0;
        auto corr_matrix = BuildMatrix(returns);

        for (const auto& [label_i, weight_i] : weights) {
            for (const auto& [label_j, weight_j] : weights) {
                double corr = corr_matrix.Get(label_i, label_j);
                portfolio_variance += weight_i * weight_j * vols[label_i] * vols[label_j] * corr;
            }
        }

        double portfolio_vol = sqrt(portfolio_variance);

        // Diversification ratio
        result.diversification_ratio = weighted_avg_vol > 0
            ? portfolio_vol / weighted_avg_vol : 1.0;

        // Concentration (HHI)
        result.concentration_hhi = 0;
        for (const auto& [_, weight] : weights) {
            result.concentration_hhi += weight * weight;
        }

        result.effective_n = result.concentration_hhi > 0
            ? 1.0 / result.concentration_hhi : 0;

        // Risk contributions
        for (const auto& [label, weight] : weights) {
            double marginal = 0;
            for (const auto& [other_label, other_weight] : weights) {
                double corr = corr_matrix.Get(label, other_label);
                marginal += other_weight * vols[label] * vols[other_label] * corr;
            }
            marginal *= weight;

            result.risk_contributions[label] = portfolio_variance > 0
                ? marginal / portfolio_variance : 0;
        }

        return result;
    }

    /**
     * Extract returns from equity curves
     */
    static std::vector<double> ExtractReturns(
        const std::vector<std::pair<int64_t, double>>& equity_curve,
        size_t sample_interval = 1) {

        std::vector<double> returns;

        for (size_t i = sample_interval; i < equity_curve.size(); i += sample_interval) {
            double prev = equity_curve[i - sample_interval].second;
            double curr = equity_curve[i].second;

            if (prev > 0) {
                returns.push_back((curr - prev) / prev);
            }
        }

        return returns;
    }

    /**
     * Find pairs with high/low correlation
     */
    static std::vector<std::tuple<std::string, std::string, double>>
    FindCorrelatedPairs(const CorrelationMatrix& matrix, double threshold, bool above = true) {
        std::vector<std::tuple<std::string, std::string, double>> pairs;

        for (size_t i = 0; i < matrix.labels.size(); ++i) {
            for (size_t j = i + 1; j < matrix.labels.size(); ++j) {
                double corr = matrix.matrix[i][j];

                if ((above && corr >= threshold) || (!above && corr <= threshold)) {
                    pairs.push_back({matrix.labels[i], matrix.labels[j], corr});
                }
            }
        }

        // Sort by correlation (descending for above, ascending for below)
        std::sort(pairs.begin(), pairs.end(),
            [above](const auto& a, const auto& b) {
                return above ? std::get<2>(a) > std::get<2>(b)
                            : std::get<2>(a) < std::get<2>(b);
            });

        return pairs;
    }

    /**
     * Calculate beta of an asset against a benchmark
     */
    static double CalculateBeta(const std::vector<double>& asset_returns,
                                const std::vector<double>& benchmark_returns) {
        if (asset_returns.size() != benchmark_returns.size() || asset_returns.size() < 2) {
            return 1.0;
        }

        double mean_asset = std::accumulate(asset_returns.begin(), asset_returns.end(), 0.0)
                           / asset_returns.size();
        double mean_bench = std::accumulate(benchmark_returns.begin(), benchmark_returns.end(), 0.0)
                           / benchmark_returns.size();

        double cov = 0, var_bench = 0;
        for (size_t i = 0; i < asset_returns.size(); ++i) {
            double da = asset_returns[i] - mean_asset;
            double db = benchmark_returns[i] - mean_bench;
            cov += da * db;
            var_bench += db * db;
        }

        return var_bench > 0 ? cov / var_bench : 1.0;
    }

    /**
     * Calculate alpha (excess return over benchmark)
     */
    static double CalculateAlpha(const std::vector<double>& asset_returns,
                                 const std::vector<double>& benchmark_returns,
                                 double risk_free_rate = 0.0) {
        double beta = CalculateBeta(asset_returns, benchmark_returns);

        double mean_asset = std::accumulate(asset_returns.begin(), asset_returns.end(), 0.0)
                           / asset_returns.size();
        double mean_bench = std::accumulate(benchmark_returns.begin(), benchmark_returns.end(), 0.0)
                           / benchmark_returns.size();

        // Alpha = asset return - (risk_free + beta * (benchmark - risk_free))
        return mean_asset - (risk_free_rate + beta * (mean_bench - risk_free_rate));
    }
};

}  // namespace backtest

#endif  // CORRELATION_ANALYZER_H
