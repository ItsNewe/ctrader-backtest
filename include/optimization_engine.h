#ifndef OPTIMIZATION_ENGINE_H
#define OPTIMIZATION_ENGINE_H

#include <vector>
#include <map>
#include <string>
#include <functional>
#include <algorithm>
#include <random>
#include <thread>
#include <mutex>
#include <future>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <limits>

namespace backtest {

/**
 * Parameter definition for optimization
 */
struct OptimizationParam {
    std::string name;
    double min_value;
    double max_value;
    double step;        // For grid search (0 = continuous)
    bool is_integer;    // Round to integer

    OptimizationParam(const std::string& n, double min_val, double max_val,
                      double s = 0, bool integer = false)
        : name(n), min_value(min_val), max_value(max_val),
          step(s), is_integer(integer) {}

    // Number of discrete values for grid search
    size_t GridSize() const {
        if (step <= 0) return 1;
        return static_cast<size_t>((max_value - min_value) / step) + 1;
    }

    // Get value at grid index
    double GridValue(size_t index) const {
        double val = min_value + index * step;
        if (is_integer) val = std::round(val);
        return std::min(val, max_value);
    }

    // Clamp value to valid range
    double Clamp(double val) const {
        val = std::max(min_value, std::min(max_value, val));
        if (is_integer) val = std::round(val);
        return val;
    }
};

/**
 * Result of a single optimization evaluation
 */
struct OptimizationResult {
    std::map<std::string, double> params;
    double fitness;
    double profit;
    double max_drawdown;
    double sharpe_ratio;
    int total_trades;
    bool is_valid;

    OptimizationResult() : fitness(std::numeric_limits<double>::lowest()),
                           profit(0), max_drawdown(0), sharpe_ratio(0),
                           total_trades(0), is_valid(false) {}

    bool operator<(const OptimizationResult& other) const {
        return fitness < other.fitness;
    }
};

/**
 * Fitness function type
 * Takes parameters, returns OptimizationResult
 */
using FitnessFunction = std::function<OptimizationResult(const std::map<std::string, double>&)>;

/**
 * Optimization configuration
 */
struct OptimizationConfig {
    // General
    int num_threads = std::thread::hardware_concurrency();
    bool verbose = true;

    // Grid search
    // (uses step from OptimizationParam)

    // Genetic algorithm
    int ga_population_size = 50;
    int ga_generations = 100;
    double ga_crossover_rate = 0.8;
    double ga_mutation_rate = 0.1;
    double ga_elite_ratio = 0.1;     // Keep top 10% unchanged
    int ga_tournament_size = 3;

    // Convergence
    int convergence_generations = 20;  // Stop if no improvement
    double convergence_threshold = 0.001;  // Min improvement

    // Constraints
    double min_trades = 10;           // Reject results with fewer trades
    double max_drawdown_limit = 0.9;  // Reject if DD > 90%
};

/**
 * Grid Search Optimizer
 */
class GridSearchOptimizer {
private:
    std::vector<OptimizationParam> params_;
    FitnessFunction fitness_func_;
    OptimizationConfig config_;
    std::mutex mutex_;
    std::vector<OptimizationResult> all_results_;

public:
    GridSearchOptimizer(const std::vector<OptimizationParam>& params,
                        FitnessFunction fitness_func,
                        const OptimizationConfig& config = OptimizationConfig())
        : params_(params), fitness_func_(fitness_func), config_(config) {}

    /**
     * Run grid search optimization
     */
    std::vector<OptimizationResult> Run() {
        all_results_.clear();

        // Calculate total combinations
        size_t total_combos = 1;
        for (const auto& p : params_) {
            total_combos *= p.GridSize();
        }

        if (config_.verbose) {
            std::cout << "Grid Search: " << total_combos << " combinations, "
                     << config_.num_threads << " threads\n";
        }

        // Generate all parameter combinations
        std::vector<std::map<std::string, double>> all_params;
        GenerateGridCombinations(all_params, 0, {});

        // Process in parallel
        std::vector<std::future<OptimizationResult>> futures;
        std::atomic<size_t> completed{0};

        auto start_time = std::chrono::steady_clock::now();

        for (size_t i = 0; i < all_params.size(); i += config_.num_threads) {
            futures.clear();

            size_t batch_size = std::min(static_cast<size_t>(config_.num_threads),
                                         all_params.size() - i);

            for (size_t j = 0; j < batch_size; ++j) {
                futures.push_back(std::async(std::launch::async, [this, &all_params, i, j]() {
                    return fitness_func_(all_params[i + j]);
                }));
            }

            for (auto& f : futures) {
                auto result = f.get();
                completed++;

                std::lock_guard<std::mutex> lock(mutex_);
                if (IsValidResult(result)) {
                    all_results_.push_back(result);
                }

                if (config_.verbose && completed % 100 == 0) {
                    auto elapsed = std::chrono::steady_clock::now() - start_time;
                    double secs = std::chrono::duration<double>(elapsed).count();
                    double rate = completed / secs;
                    double remaining = (total_combos - completed) / rate;

                    std::cout << "\r  " << completed << "/" << total_combos
                             << " (" << std::fixed << std::setprecision(1)
                             << (100.0 * completed / total_combos) << "%, "
                             << rate << "/s, ETA: " << remaining << "s)   " << std::flush;
                }
            }
        }

        if (config_.verbose) {
            std::cout << "\nGrid search complete. " << all_results_.size()
                     << " valid results.\n";
        }

        // Sort by fitness
        std::sort(all_results_.rbegin(), all_results_.rend());

        return all_results_;
    }

    const std::vector<OptimizationResult>& GetAllResults() const {
        return all_results_;
    }

private:
    void GenerateGridCombinations(std::vector<std::map<std::string, double>>& result,
                                  size_t param_idx,
                                  std::map<std::string, double> current) {
        if (param_idx >= params_.size()) {
            result.push_back(current);
            return;
        }

        const auto& param = params_[param_idx];
        for (size_t i = 0; i < param.GridSize(); ++i) {
            current[param.name] = param.GridValue(i);
            GenerateGridCombinations(result, param_idx + 1, current);
        }
    }

    bool IsValidResult(const OptimizationResult& result) const {
        if (!result.is_valid) return false;
        if (result.total_trades < config_.min_trades) return false;
        if (result.max_drawdown > config_.max_drawdown_limit) return false;
        return true;
    }
};

/**
 * Genetic Algorithm Optimizer
 */
class GeneticOptimizer {
private:
    std::vector<OptimizationParam> params_;
    FitnessFunction fitness_func_;
    OptimizationConfig config_;
    std::mt19937 rng_;

    struct Individual {
        std::map<std::string, double> genes;
        double fitness;
        bool evaluated;

        Individual() : fitness(std::numeric_limits<double>::lowest()), evaluated(false) {}
    };

    std::vector<Individual> population_;
    std::vector<OptimizationResult> history_;  // Best of each generation
    OptimizationResult best_ever_;

public:
    GeneticOptimizer(const std::vector<OptimizationParam>& params,
                     FitnessFunction fitness_func,
                     const OptimizationConfig& config = OptimizationConfig())
        : params_(params), fitness_func_(fitness_func), config_(config),
          rng_(std::random_device{}()) {}

    /**
     * Run genetic algorithm optimization
     */
    OptimizationResult Run() {
        if (config_.verbose) {
            std::cout << "Genetic Algorithm: " << config_.ga_population_size
                     << " population, " << config_.ga_generations << " generations\n";
        }

        // Initialize population
        InitializePopulation();

        // Evaluate initial population
        EvaluatePopulation();

        int generations_without_improvement = 0;
        double last_best_fitness = best_ever_.fitness;

        for (int gen = 0; gen < config_.ga_generations; ++gen) {
            // Create next generation
            std::vector<Individual> next_gen;

            // Elitism - keep top performers
            int elite_count = static_cast<int>(config_.ga_elite_ratio * config_.ga_population_size);
            std::sort(population_.begin(), population_.end(),
                [](const Individual& a, const Individual& b) {
                    return a.fitness > b.fitness;
                });

            for (int i = 0; i < elite_count && i < static_cast<int>(population_.size()); ++i) {
                next_gen.push_back(population_[i]);
            }

            // Fill rest with crossover and mutation
            while (static_cast<int>(next_gen.size()) < config_.ga_population_size) {
                // Tournament selection
                Individual parent1 = TournamentSelect();
                Individual parent2 = TournamentSelect();

                // Crossover
                Individual child;
                if (RandomDouble() < config_.ga_crossover_rate) {
                    child = Crossover(parent1, parent2);
                } else {
                    child = RandomDouble() < 0.5 ? parent1 : parent2;
                }

                // Mutation
                Mutate(child);

                next_gen.push_back(child);
            }

            population_ = std::move(next_gen);

            // Evaluate new population
            EvaluatePopulation();

            // Track best of generation
            auto best_this_gen = *std::max_element(population_.begin(), population_.end(),
                [](const Individual& a, const Individual& b) {
                    return a.fitness < b.fitness;
                });

            OptimizationResult gen_result;
            gen_result.params = best_this_gen.genes;
            gen_result.fitness = best_this_gen.fitness;
            history_.push_back(gen_result);

            // Check convergence
            double improvement = best_ever_.fitness - last_best_fitness;
            if (improvement < config_.convergence_threshold) {
                generations_without_improvement++;
            } else {
                generations_without_improvement = 0;
                last_best_fitness = best_ever_.fitness;
            }

            if (config_.verbose && (gen + 1) % 10 == 0) {
                std::cout << "  Gen " << (gen + 1) << ": best=" << best_ever_.fitness
                         << " (profit=$" << best_ever_.profit
                         << ", DD=" << (best_ever_.max_drawdown * 100) << "%)\n";
            }

            // Early termination
            if (generations_without_improvement >= config_.convergence_generations) {
                if (config_.verbose) {
                    std::cout << "Converged after " << (gen + 1) << " generations\n";
                }
                break;
            }
        }

        if (config_.verbose) {
            std::cout << "\nBest solution:\n";
            for (const auto& [name, value] : best_ever_.params) {
                std::cout << "  " << name << " = " << value << "\n";
            }
            std::cout << "  Fitness: " << best_ever_.fitness << "\n";
            std::cout << "  Profit: $" << best_ever_.profit << "\n";
            std::cout << "  Max DD: " << (best_ever_.max_drawdown * 100) << "%\n";
            std::cout << "  Sharpe: " << best_ever_.sharpe_ratio << "\n";
        }

        return best_ever_;
    }

    const std::vector<OptimizationResult>& GetHistory() const {
        return history_;
    }

private:
    void InitializePopulation() {
        population_.clear();
        population_.reserve(config_.ga_population_size);

        for (int i = 0; i < config_.ga_population_size; ++i) {
            Individual ind;
            for (const auto& param : params_) {
                ind.genes[param.name] = RandomInRange(param);
            }
            population_.push_back(ind);
        }
    }

    void EvaluatePopulation() {
        std::vector<std::future<std::pair<size_t, OptimizationResult>>> futures;

        for (size_t i = 0; i < population_.size(); ++i) {
            if (!population_[i].evaluated) {
                futures.push_back(std::async(std::launch::async,
                    [this, i]() -> std::pair<size_t, OptimizationResult> {
                        return {i, fitness_func_(population_[i].genes)};
                    }));
            }
        }

        for (auto& f : futures) {
            auto [idx, result] = f.get();
            population_[idx].fitness = result.fitness;
            population_[idx].evaluated = true;

            if (result.is_valid && result.fitness > best_ever_.fitness) {
                best_ever_ = result;
            }
        }
    }

    Individual TournamentSelect() {
        std::uniform_int_distribution<size_t> dist(0, population_.size() - 1);

        Individual best = population_[dist(rng_)];
        for (int i = 1; i < config_.ga_tournament_size; ++i) {
            Individual candidate = population_[dist(rng_)];
            if (candidate.fitness > best.fitness) {
                best = candidate;
            }
        }
        return best;
    }

    Individual Crossover(const Individual& parent1, const Individual& parent2) {
        Individual child;

        // Uniform crossover
        for (const auto& param : params_) {
            if (RandomDouble() < 0.5) {
                child.genes[param.name] = parent1.genes.at(param.name);
            } else {
                child.genes[param.name] = parent2.genes.at(param.name);
            }
        }

        return child;
    }

    void Mutate(Individual& ind) {
        for (const auto& param : params_) {
            if (RandomDouble() < config_.ga_mutation_rate) {
                // Gaussian mutation
                double current = ind.genes[param.name];
                double range = param.max_value - param.min_value;
                std::normal_distribution<double> dist(0, range * 0.1);

                double mutated = current + dist(rng_);
                ind.genes[param.name] = param.Clamp(mutated);
                ind.evaluated = false;
            }
        }
    }

    double RandomInRange(const OptimizationParam& param) {
        std::uniform_real_distribution<double> dist(param.min_value, param.max_value);
        double val = dist(rng_);
        if (param.is_integer) val = std::round(val);
        return val;
    }

    double RandomDouble() {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(rng_);
    }
};

/**
 * Differential Evolution Optimizer (more robust than GA for continuous optimization)
 */
class DifferentialEvolutionOptimizer {
private:
    std::vector<OptimizationParam> params_;
    FitnessFunction fitness_func_;
    OptimizationConfig config_;
    std::mt19937 rng_;

    double F_ = 0.8;   // Differential weight
    double CR_ = 0.9;  // Crossover probability

    std::vector<std::vector<double>> population_;
    std::vector<double> fitness_;
    OptimizationResult best_ever_;

public:
    DifferentialEvolutionOptimizer(const std::vector<OptimizationParam>& params,
                                   FitnessFunction fitness_func,
                                   const OptimizationConfig& config = OptimizationConfig())
        : params_(params), fitness_func_(fitness_func), config_(config),
          rng_(std::random_device{}()) {}

    OptimizationResult Run() {
        if (config_.verbose) {
            std::cout << "Differential Evolution: " << config_.ga_population_size
                     << " population, " << config_.ga_generations << " generations\n";
        }

        // Initialize
        InitializePopulation();
        EvaluateAll();

        int stagnant = 0;
        double last_best = best_ever_.fitness;

        for (int gen = 0; gen < config_.ga_generations; ++gen) {
            std::vector<std::vector<double>> new_pop = population_;
            std::vector<double> new_fitness = fitness_;

            // Process each individual
            for (size_t i = 0; i < population_.size(); ++i) {
                // Select 3 random distinct individuals (not i)
                std::vector<size_t> candidates;
                for (size_t j = 0; j < population_.size(); ++j) {
                    if (j != i) candidates.push_back(j);
                }
                std::shuffle(candidates.begin(), candidates.end(), rng_);

                size_t a = candidates[0], b = candidates[1], c = candidates[2];

                // Mutation: v = a + F * (b - c)
                std::vector<double> mutant(params_.size());
                for (size_t d = 0; d < params_.size(); ++d) {
                    mutant[d] = population_[a][d] + F_ * (population_[b][d] - population_[c][d]);
                    mutant[d] = params_[d].Clamp(mutant[d]);
                }

                // Crossover
                std::vector<double> trial(params_.size());
                std::uniform_int_distribution<size_t> dim_dist(0, params_.size() - 1);
                size_t j_rand = dim_dist(rng_);

                for (size_t d = 0; d < params_.size(); ++d) {
                    if (RandomDouble() < CR_ || d == j_rand) {
                        trial[d] = mutant[d];
                    } else {
                        trial[d] = population_[i][d];
                    }
                }

                // Evaluate trial
                auto result = EvaluateIndividual(trial);

                // Selection
                if (result.fitness > fitness_[i]) {
                    new_pop[i] = trial;
                    new_fitness[i] = result.fitness;

                    if (result.fitness > best_ever_.fitness) {
                        best_ever_ = result;
                    }
                }
            }

            population_ = new_pop;
            fitness_ = new_fitness;

            // Convergence check
            if (best_ever_.fitness - last_best < config_.convergence_threshold) {
                stagnant++;
            } else {
                stagnant = 0;
                last_best = best_ever_.fitness;
            }

            if (config_.verbose && (gen + 1) % 10 == 0) {
                std::cout << "  Gen " << (gen + 1) << ": best=" << best_ever_.fitness
                         << " (profit=$" << best_ever_.profit << ")\n";
            }

            if (stagnant >= config_.convergence_generations) {
                if (config_.verbose) {
                    std::cout << "Converged after " << (gen + 1) << " generations\n";
                }
                break;
            }
        }

        return best_ever_;
    }

private:
    void InitializePopulation() {
        population_.resize(config_.ga_population_size);
        fitness_.resize(config_.ga_population_size);

        for (int i = 0; i < config_.ga_population_size; ++i) {
            population_[i].resize(params_.size());
            for (size_t d = 0; d < params_.size(); ++d) {
                std::uniform_real_distribution<double> dist(
                    params_[d].min_value, params_[d].max_value);
                population_[i][d] = dist(rng_);
                if (params_[d].is_integer) {
                    population_[i][d] = std::round(population_[i][d]);
                }
            }
        }
    }

    void EvaluateAll() {
        std::vector<std::future<std::pair<size_t, OptimizationResult>>> futures;

        for (size_t i = 0; i < population_.size(); ++i) {
            futures.push_back(std::async(std::launch::async,
                [this, i]() -> std::pair<size_t, OptimizationResult> {
                    return {i, EvaluateIndividual(population_[i])};
                }));
        }

        for (auto& f : futures) {
            auto [idx, result] = f.get();
            fitness_[idx] = result.fitness;

            if (result.fitness > best_ever_.fitness) {
                best_ever_ = result;
            }
        }
    }

    OptimizationResult EvaluateIndividual(const std::vector<double>& ind) {
        std::map<std::string, double> params;
        for (size_t i = 0; i < params_.size(); ++i) {
            params[params_[i].name] = ind[i];
        }
        return fitness_func_(params);
    }

    double RandomDouble() {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(rng_);
    }
};

/**
 * Helper: Common fitness functions
 */
namespace FitnessFunctions {

/**
 * Profit-based fitness
 */
inline double ProfitFitness(double profit, double max_dd, double sharpe, int trades,
                           double min_trades = 10, double max_dd_limit = 0.9) {
    if (trades < min_trades) return std::numeric_limits<double>::lowest();
    if (max_dd > max_dd_limit) return std::numeric_limits<double>::lowest();
    return profit;
}

/**
 * Sharpe-based fitness (risk-adjusted)
 */
inline double SharpeFitness(double profit, double max_dd, double sharpe, int trades,
                           double min_trades = 10, double max_dd_limit = 0.9) {
    if (trades < min_trades) return std::numeric_limits<double>::lowest();
    if (max_dd > max_dd_limit) return std::numeric_limits<double>::lowest();
    return sharpe;
}

/**
 * Calmar-based fitness (return/drawdown)
 */
inline double CalmarFitness(double profit, double max_dd, double sharpe, int trades,
                           double min_trades = 10, double max_dd_limit = 0.9) {
    if (trades < min_trades) return std::numeric_limits<double>::lowest();
    if (max_dd > max_dd_limit) return std::numeric_limits<double>::lowest();
    if (max_dd <= 0) return 0;
    return profit / max_dd;
}

/**
 * Combined fitness with penalties
 */
inline double CombinedFitness(double profit, double max_dd, double sharpe, int trades,
                             double profit_weight = 0.4,
                             double sharpe_weight = 0.3,
                             double dd_weight = 0.3,
                             double min_trades = 10,
                             double max_dd_limit = 0.9) {
    if (trades < min_trades) return std::numeric_limits<double>::lowest();
    if (max_dd > max_dd_limit) return std::numeric_limits<double>::lowest();

    // Normalize components
    double profit_score = profit / 10000.0;  // Normalize to ~1 for $10k profit
    double sharpe_score = sharpe / 2.0;      // Sharpe of 2 = score of 1
    double dd_score = 1.0 - max_dd;          // Lower DD = higher score

    return profit_weight * profit_score +
           sharpe_weight * sharpe_score +
           dd_weight * dd_score;
}

}  // namespace FitnessFunctions

}  // namespace backtest

#endif  // OPTIMIZATION_ENGINE_H
