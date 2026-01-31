#include <iostream>
#include <vector>
#include <map>
#include <random>
#include <cmath>

#include "../include/portfolio_backtester.h"
#include "../include/correlation_analyzer.h"
#include "../include/risk_metrics.h"

using namespace backtest;

/**
 * Generate synthetic returns for testing
 */
std::vector<double> GenerateSyntheticReturns(size_t n, double mean, double std_dev, int seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(mean, std_dev);

    std::vector<double> returns;
    for (size_t i = 0; i < n; ++i) {
        returns.push_back(dist(rng));
    }
    return returns;
}

/**
 * Generate correlated returns
 */
std::pair<std::vector<double>, std::vector<double>>
GenerateCorrelatedReturns(size_t n, double correlation, int seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(0, 1);

    std::vector<double> x, y;

    for (size_t i = 0; i < n; ++i) {
        double z1 = dist(rng);
        double z2 = dist(rng);

        // Create correlated pair using Cholesky
        double x_val = z1;
        double y_val = correlation * z1 + sqrt(1 - correlation * correlation) * z2;

        x.push_back(x_val * 0.01);  // Scale to realistic daily returns
        y.push_back(y_val * 0.01);
    }

    return {x, y};
}

void TestCorrelationAnalyzer() {
    std::cout << "\n================================================================================\n";
    std::cout << "CORRELATION ANALYZER TEST\n";
    std::cout << "================================================================================\n";

    // Generate correlated returns
    auto [returns_a, returns_b] = GenerateCorrelatedReturns(1000, 0.7, 42);
    auto returns_c = GenerateSyntheticReturns(1000, 0.0005, 0.015, 123);

    std::cout << "\n--- Pearson Correlation ---\n";
    double corr_ab = CorrelationAnalyzer::PearsonCorrelation(returns_a, returns_b);
    double corr_ac = CorrelationAnalyzer::PearsonCorrelation(returns_a, returns_c);
    double corr_bc = CorrelationAnalyzer::PearsonCorrelation(returns_b, returns_c);

    std::cout << "Correlation A-B (expected ~0.7): " << corr_ab << std::endl;
    std::cout << "Correlation A-C (expected ~0.0): " << corr_ac << std::endl;
    std::cout << "Correlation B-C (expected ~0.0): " << corr_bc << std::endl;

    // Build correlation matrix
    std::cout << "\n--- Correlation Matrix ---\n";
    std::map<std::string, std::vector<double>> all_returns;
    all_returns["XAUUSD"] = returns_a;
    all_returns["XAGUSD"] = returns_b;
    all_returns["EURUSD"] = returns_c;

    auto matrix = CorrelationAnalyzer::BuildMatrix(all_returns);
    matrix.Print();

    // Find highly correlated pairs
    std::cout << "\n--- High Correlation Pairs (>0.5) ---\n";
    auto pairs = CorrelationAnalyzer::FindCorrelatedPairs(matrix, 0.5, true);
    for (const auto& [a, b, corr] : pairs) {
        std::cout << a << " <-> " << b << ": " << corr << std::endl;
    }

    // Diversification metrics
    std::cout << "\n--- Diversification Metrics ---\n";
    std::map<std::string, double> weights = {
        {"XAUUSD", 0.4},
        {"XAGUSD", 0.4},
        {"EURUSD", 0.2}
    };

    auto div_metrics = CorrelationAnalyzer::CalculateDiversification(all_returns, weights);
    std::cout << "Diversification Ratio: " << div_metrics.diversification_ratio << std::endl;
    std::cout << "Concentration (HHI): " << div_metrics.concentration_hhi << std::endl;
    std::cout << "Effective N: " << div_metrics.effective_n << std::endl;

    std::cout << "Risk Contributions:" << std::endl;
    for (const auto& [sym, contrib] : div_metrics.risk_contributions) {
        std::cout << "  " << sym << ": " << (contrib * 100) << "%" << std::endl;
    }

    // Beta and Alpha
    std::cout << "\n--- Beta/Alpha Analysis ---\n";
    double beta = CorrelationAnalyzer::CalculateBeta(returns_a, returns_b);
    double alpha = CorrelationAnalyzer::CalculateAlpha(returns_a, returns_b);
    std::cout << "Beta of A vs B: " << beta << std::endl;
    std::cout << "Alpha of A vs B: " << alpha << std::endl;

    std::cout << "\nCorrelation Analyzer Test: PASSED\n";
}

void TestRiskMetrics() {
    std::cout << "\n================================================================================\n";
    std::cout << "RISK METRICS TEST\n";
    std::cout << "================================================================================\n";

    // Generate realistic trading returns
    auto returns = GenerateSyntheticReturns(252, 0.0008, 0.015, 42);  // 252 days

    // Add some extreme events
    returns[50] = -0.08;   // Bad day
    returns[100] = -0.05;  // Another bad day
    returns[150] = 0.06;   // Good day
    returns[200] = -0.03;  // Pullback

    // Calculate full risk report
    auto report = RiskMetricsCalculator::Calculate(returns, 0.02);  // 2% risk-free rate
    report.Print();

    // Test individual VaR methods
    std::cout << "\n--- VaR Method Comparison ---\n";
    double hist_var = RiskMetricsCalculator::HistoricalVaR(returns, 0.95);
    double param_var = RiskMetricsCalculator::ParametricVaR(returns, 0.95);
    double es = RiskMetricsCalculator::ExpectedShortfall(returns, 0.95);

    std::cout << "Historical VaR 95%: " << (hist_var * 100) << "%" << std::endl;
    std::cout << "Parametric VaR 95%: " << (param_var * 100) << "%" << std::endl;
    std::cout << "Expected Shortfall: " << (es * 100) << "%" << std::endl;

    // Build equity curve and calculate drawdown
    std::vector<double> equity;
    equity.push_back(10000);
    for (double r : returns) {
        equity.push_back(equity.back() * (1 + r));
    }

    double max_dd = RiskMetricsCalculator::MaxDrawdown(equity);
    std::cout << "\nMax Drawdown from Equity Curve: " << (max_dd * 100) << "%" << std::endl;

    auto dd_series = RiskMetricsCalculator::DrawdownSeries(equity);
    std::cout << "Final Drawdown: " << (dd_series.back() * 100) << "%" << std::endl;

    std::cout << "\nRisk Metrics Test: PASSED\n";
}

void TestIntegration() {
    std::cout << "\n================================================================================\n";
    std::cout << "INTEGRATION TEST: Portfolio + Correlation + Risk\n";
    std::cout << "================================================================================\n";

    // Simulate multiple strategy returns
    std::map<std::string, std::vector<double>> strategy_returns;
    strategy_returns["Strategy_A"] = GenerateSyntheticReturns(252, 0.001, 0.02, 1);
    strategy_returns["Strategy_B"] = GenerateSyntheticReturns(252, 0.0008, 0.015, 2);
    strategy_returns["Strategy_C"] = GenerateSyntheticReturns(252, 0.0005, 0.01, 3);

    // Correlation analysis
    std::cout << "\n--- Strategy Correlations ---\n";
    auto corr_matrix = CorrelationAnalyzer::BuildMatrix(strategy_returns);
    corr_matrix.Print();

    // Risk metrics for each strategy
    std::cout << "\n--- Individual Strategy Risk ---\n";
    for (const auto& [name, returns] : strategy_returns) {
        auto report = RiskMetricsCalculator::Calculate(returns);
        std::cout << name << ":\n";
        std::cout << "  Sharpe: " << report.sharpe_ratio;
        std::cout << "  Sortino: " << report.sortino_ratio;
        std::cout << "  MaxDD: " << (report.max_drawdown * 100) << "%";
        std::cout << "  VaR95: " << (report.var_95 * 100) << "%\n";
    }

    // Portfolio allocation
    std::map<std::string, double> weights = {
        {"Strategy_A", 0.33},
        {"Strategy_B", 0.33},
        {"Strategy_C", 0.34}
    };

    // Combined portfolio returns
    std::vector<double> portfolio_returns(252, 0);
    for (const auto& [name, returns] : strategy_returns) {
        double w = weights[name];
        for (size_t i = 0; i < returns.size(); ++i) {
            portfolio_returns[i] += w * returns[i];
        }
    }

    std::cout << "\n--- Combined Portfolio Risk ---\n";
    auto portfolio_report = RiskMetricsCalculator::Calculate(portfolio_returns);
    std::cout << "Portfolio Sharpe: " << portfolio_report.sharpe_ratio << std::endl;
    std::cout << "Portfolio MaxDD: " << (portfolio_report.max_drawdown * 100) << "%" << std::endl;
    std::cout << "Portfolio VaR95: " << (portfolio_report.var_95 * 100) << "%" << std::endl;

    // Diversification benefit
    auto div_metrics = CorrelationAnalyzer::CalculateDiversification(strategy_returns, weights);
    std::cout << "\nDiversification Ratio: " << div_metrics.diversification_ratio << std::endl;
    std::cout << "Effective N: " << div_metrics.effective_n << std::endl;

    std::cout << "\nIntegration Test: PASSED\n";
}

int main() {
    std::cout << "================================================================================\n";
    std::cout << "PORTFOLIO & RISK ANALYSIS TEST SUITE\n";
    std::cout << "================================================================================\n";

    try {
        TestCorrelationAnalyzer();
        TestRiskMetrics();
        TestIntegration();

        std::cout << "\n================================================================================\n";
        std::cout << "ALL TESTS PASSED\n";
        std::cout << "================================================================================\n";

    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
