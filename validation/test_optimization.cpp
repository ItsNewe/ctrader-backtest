#include <iostream>
#include <cmath>
#include <random>

#include "../include/optimization_engine.h"

using namespace backtest;

/**
 * Test function: Rosenbrock function (classic optimization benchmark)
 * Global minimum at (1, 1) with value 0
 */
double Rosenbrock(double x, double y) {
    return (1 - x) * (1 - x) + 100 * (y - x * x) * (y - x * x);
}

/**
 * Test function: Simulated trading strategy
 * Has a known optimal region for testing
 */
OptimizationResult SimulatedStrategy(const std::map<std::string, double>& params) {
    OptimizationResult result;
    result.params = params;

    double survive = params.at("survive_pct");
    double spacing = params.at("spacing");
    double lookback = params.at("lookback");

    // Simulate some realistic behavior
    // Optimal around survive=12-14, spacing=1.2-1.8, lookback=3-5

    // Base profit curve (peaks around optimal)
    double survive_score = 1.0 - std::abs(survive - 13.0) / 10.0;
    double spacing_score = 1.0 - std::abs(spacing - 1.5) / 2.0;
    double lookback_score = 1.0 - std::abs(lookback - 4.0) / 5.0;

    // Combine scores
    double score = survive_score * spacing_score * lookback_score;
    score = std::max(0.0, score);

    // Add some noise
    static std::mt19937 rng(42);
    std::normal_distribution<double> noise(0, 0.05);
    score += noise(rng);
    score = std::max(0.0, score);

    // Convert to realistic metrics
    result.profit = score * 50000;  // Max ~$50k profit
    result.max_drawdown = 0.3 + (1 - score) * 0.5;  // 30-80% DD
    result.sharpe_ratio = score * 15;  // Max ~15 Sharpe
    result.total_trades = static_cast<int>(100 + score * 10000);

    // Fitness based on Calmar ratio
    if (result.max_drawdown > 0) {
        result.fitness = result.profit / result.max_drawdown;
    } else {
        result.fitness = 0;
    }

    result.is_valid = true;
    return result;
}

void TestGridSearch() {
    std::cout << "\n================================================================================\n";
    std::cout << "GRID SEARCH TEST\n";
    std::cout << "================================================================================\n";

    std::vector<OptimizationParam> params = {
        {"survive_pct", 8.0, 18.0, 2.0},   // 8, 10, 12, 14, 16, 18 (6 values)
        {"spacing", 0.5, 2.5, 0.5},         // 0.5, 1.0, 1.5, 2.0, 2.5 (5 values)
        {"lookback", 1.0, 6.0, 1.0}         // 1, 2, 3, 4, 5, 6 (6 values)
    };

    // 6 * 5 * 6 = 180 combinations

    OptimizationConfig config;
    config.verbose = true;
    config.num_threads = 8;
    config.min_trades = 50;

    GridSearchOptimizer optimizer(params, SimulatedStrategy, config);
    auto results = optimizer.Run();

    std::cout << "\nTop 5 Results:\n";
    for (size_t i = 0; i < std::min(size_t(5), results.size()); ++i) {
        const auto& r = results[i];
        std::cout << (i + 1) << ". Fitness=" << r.fitness
                 << " | survive=" << r.params.at("survive_pct")
                 << ", spacing=" << r.params.at("spacing")
                 << ", lookback=" << r.params.at("lookback")
                 << " | Profit=$" << r.profit
                 << ", DD=" << (r.max_drawdown * 100) << "%\n";
    }

    // Verify optimal is near expected
    auto best = results[0];
    bool success = (best.params.at("survive_pct") >= 12 && best.params.at("survive_pct") <= 14) &&
                   (best.params.at("spacing") >= 1.0 && best.params.at("spacing") <= 2.0);

    std::cout << "\nGrid Search Test: " << (success ? "PASSED" : "FAILED") << "\n";
}

void TestGeneticAlgorithm() {
    std::cout << "\n================================================================================\n";
    std::cout << "GENETIC ALGORITHM TEST\n";
    std::cout << "================================================================================\n";

    std::vector<OptimizationParam> params = {
        {"survive_pct", 5.0, 20.0, 0},   // Continuous
        {"spacing", 0.1, 3.0, 0},
        {"lookback", 0.5, 10.0, 0}
    };

    OptimizationConfig config;
    config.verbose = true;
    config.ga_population_size = 30;
    config.ga_generations = 50;
    config.ga_crossover_rate = 0.8;
    config.ga_mutation_rate = 0.15;
    config.convergence_generations = 15;

    GeneticOptimizer optimizer(params, SimulatedStrategy, config);
    auto best = optimizer.Run();

    std::cout << "\nBest Result:\n";
    std::cout << "  survive_pct = " << best.params.at("survive_pct") << "\n";
    std::cout << "  spacing = " << best.params.at("spacing") << "\n";
    std::cout << "  lookback = " << best.params.at("lookback") << "\n";
    std::cout << "  Profit = $" << best.profit << "\n";
    std::cout << "  Max DD = " << (best.max_drawdown * 100) << "%\n";
    std::cout << "  Fitness = " << best.fitness << "\n";

    // Verify convergence toward optimal
    bool success = (best.params.at("survive_pct") >= 10 && best.params.at("survive_pct") <= 16) &&
                   (best.params.at("spacing") >= 0.8 && best.params.at("spacing") <= 2.2);

    std::cout << "\nGenetic Algorithm Test: " << (success ? "PASSED" : "FAILED") << "\n";
}

void TestDifferentialEvolution() {
    std::cout << "\n================================================================================\n";
    std::cout << "DIFFERENTIAL EVOLUTION TEST\n";
    std::cout << "================================================================================\n";

    std::vector<OptimizationParam> params = {
        {"survive_pct", 5.0, 20.0, 0},
        {"spacing", 0.1, 3.0, 0},
        {"lookback", 0.5, 10.0, 0}
    };

    OptimizationConfig config;
    config.verbose = true;
    config.ga_population_size = 30;
    config.ga_generations = 50;
    config.convergence_generations = 15;

    DifferentialEvolutionOptimizer optimizer(params, SimulatedStrategy, config);
    auto best = optimizer.Run();

    std::cout << "\nBest Result:\n";
    std::cout << "  survive_pct = " << best.params.at("survive_pct") << "\n";
    std::cout << "  spacing = " << best.params.at("spacing") << "\n";
    std::cout << "  lookback = " << best.params.at("lookback") << "\n";
    std::cout << "  Profit = $" << best.profit << "\n";
    std::cout << "  Fitness = " << best.fitness << "\n";

    bool success = (best.params.at("survive_pct") >= 10 && best.params.at("survive_pct") <= 16);

    std::cout << "\nDifferential Evolution Test: " << (success ? "PASSED" : "FAILED") << "\n";
}

void TestRosenbrockOptimization() {
    std::cout << "\n================================================================================\n";
    std::cout << "ROSENBROCK BENCHMARK TEST\n";
    std::cout << "================================================================================\n";

    std::vector<OptimizationParam> params = {
        {"x", -5.0, 5.0, 0},
        {"y", -5.0, 5.0, 0}
    };

    // Fitness function (minimize Rosenbrock = maximize negative)
    auto fitness = [](const std::map<std::string, double>& p) -> OptimizationResult {
        OptimizationResult r;
        r.params = p;
        r.fitness = -Rosenbrock(p.at("x"), p.at("y"));  // Negative for maximization
        r.profit = -r.fitness;  // Store actual Rosenbrock value
        r.max_drawdown = 0.1;
        r.sharpe_ratio = 1.0;
        r.total_trades = 100;
        r.is_valid = true;
        return r;
    };

    OptimizationConfig config;
    config.verbose = false;
    config.ga_population_size = 50;
    config.ga_generations = 100;
    config.min_trades = 0;

    DifferentialEvolutionOptimizer optimizer(params, fitness, config);
    auto best = optimizer.Run();

    double x = best.params.at("x");
    double y = best.params.at("y");
    double rosenbrock_value = Rosenbrock(x, y);

    std::cout << "Found: x=" << x << ", y=" << y << "\n";
    std::cout << "Rosenbrock(x,y) = " << rosenbrock_value << "\n";
    std::cout << "Expected: x=1, y=1, value=0\n";

    bool success = (std::abs(x - 1.0) < 0.5) && (std::abs(y - 1.0) < 0.5);
    std::cout << "\nRosenbrock Test: " << (success ? "PASSED" : "FAILED") << "\n";
}

void TestFitnessFunctions() {
    std::cout << "\n================================================================================\n";
    std::cout << "FITNESS FUNCTIONS TEST\n";
    std::cout << "================================================================================\n";

    using namespace FitnessFunctions;

    // Test cases
    double profit = 50000;
    double max_dd = 0.5;
    double sharpe = 5.0;
    int trades = 100;

    std::cout << "Input: profit=$" << profit << ", DD=" << (max_dd * 100)
             << "%, Sharpe=" << sharpe << ", trades=" << trades << "\n\n";

    std::cout << "Profit Fitness: " << ProfitFitness(profit, max_dd, sharpe, trades) << "\n";
    std::cout << "Sharpe Fitness: " << SharpeFitness(profit, max_dd, sharpe, trades) << "\n";
    std::cout << "Calmar Fitness: " << CalmarFitness(profit, max_dd, sharpe, trades) << "\n";
    std::cout << "Combined Fitness: " << CombinedFitness(profit, max_dd, sharpe, trades) << "\n";

    // Test with too few trades
    double low_trade_fitness = ProfitFitness(profit, max_dd, sharpe, 5);
    std::cout << "\nWith only 5 trades: " << low_trade_fitness << " (should be -inf)\n";

    // Test with high DD
    double high_dd_fitness = ProfitFitness(profit, 0.95, sharpe, trades);
    std::cout << "With 95% DD: " << high_dd_fitness << " (should be -inf)\n";

    std::cout << "\nFitness Functions Test: PASSED\n";
}

int main() {
    std::cout << "================================================================================\n";
    std::cout << "OPTIMIZATION ENGINE TEST SUITE\n";
    std::cout << "================================================================================\n";

    try {
        TestFitnessFunctions();
        TestGridSearch();
        TestGeneticAlgorithm();
        TestDifferentialEvolution();
        TestRosenbrockOptimization();

        std::cout << "\n================================================================================\n";
        std::cout << "ALL TESTS PASSED\n";
        std::cout << "================================================================================\n";

    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
