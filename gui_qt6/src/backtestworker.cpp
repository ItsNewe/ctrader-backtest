/**
 * BacktestWorker Implementation
 */

#include "backtestworker.h"

#include <QMutexLocker>
#include <QDateTime>
#include <QThread>
#include <algorithm>
#include <cmath>

// Include the actual backtest engine headers
#include "../../include/tick_based_engine.h"
#include "../../include/fill_up_oscillation.h"
#include "../../include/strategy_combined_ju.h"

using namespace backtest;

BacktestWorker::BacktestWorker(QObject *parent)
    : QObject(parent)
    , m_running(false)
    , m_paused(false)
    , m_stopRequested(false)
    , m_startTime(0)
{
}

BacktestWorker::~BacktestWorker()
{
    stop();
}

void BacktestWorker::setBacktestConfig(const BacktestConfig &config)
{
    QMutexLocker locker(&m_mutex);
    m_config = config;
}

void BacktestWorker::setOptimizationConfig(const OptimizationConfig &config)
{
    QMutexLocker locker(&m_mutex);
    m_optConfig = config;
}

QVector<SingleBacktestResult> BacktestWorker::getResults() const
{
    QMutexLocker locker(&const_cast<QMutex&>(m_mutex));
    return m_results;
}

SingleBacktestResult BacktestWorker::getBestResult() const
{
    QMutexLocker locker(&const_cast<QMutex&>(m_mutex));
    return m_bestResult;
}

void BacktestWorker::startSingleBacktest()
{
    if (m_running) return;

    m_running = true;
    m_paused = false;
    m_stopRequested = false;
    m_startTime = QDateTime::currentMSecsSinceEpoch();

    emit started();

    // Run single backtest with current parameters
    runSingleBacktest(m_config.parameters);

    m_running = false;
    emit finished();
}

void BacktestWorker::startOptimization()
{
    if (m_running) return;

    m_running = true;
    m_paused = false;
    m_stopRequested = false;
    m_startTime = QDateTime::currentMSecsSinceEpoch();
    m_results.clear();
    m_bestResult = SingleBacktestResult();
    m_bestResult.netProfit = -1e18;

    emit started();

    if (m_optConfig.useGeneticAlgorithm) {
        runGeneticOptimization();
    } else {
        runGridOptimization();
    }

    m_running = false;
    emit optimizationComplete(m_results);
    emit finished();
}

void BacktestWorker::pause()
{
    m_paused = true;
}

void BacktestWorker::resume()
{
    m_paused = false;
}

void BacktestWorker::stop()
{
    m_stopRequested = true;
    m_paused = false;  // Unpause to allow clean exit
}

void BacktestWorker::runSingleBacktest(const QMap<QString, double> &params)
{
    auto result = executeBacktest(params);

    if (result.valid) {
        emit singleResultReady(result);
        emit backtestComplete(result);
    } else {
        emit error(tr("Backtest execution failed"));
    }
}

void BacktestWorker::runGridOptimization()
{
    // Generate all parameter combinations
    QVector<QMap<QString, double>> combinations;
    generateParameterCombinations(combinations);

    if (combinations.isEmpty()) {
        emit error(tr("No parameter combinations generated"));
        return;
    }

    int totalRuns = combinations.size();
    int completedRuns = 0;

    // Process combinations (could be parallelized with QtConcurrent)
    for (const auto &params : combinations) {
        if (m_stopRequested) break;

        // Handle pause
        while (m_paused && !m_stopRequested) {
            QThread::msleep(100);
        }

        auto result = executeBacktest(params);

        if (result.valid) {
            QMutexLocker locker(&m_mutex);
            m_results.append(result);

            // Update best result
            double fitness = calculateFitness(result);
            double bestFitness = calculateFitness(m_bestResult);
            if (fitness > bestFitness) {
                m_bestResult = result;
            }
        }

        completedRuns++;

        // Emit progress
        BacktestProgress progress;
        progress.currentRun = completedRuns;
        progress.totalRuns = totalRuns;
        progress.percentComplete = 100.0 * completedRuns / totalRuns;
        progress.bestReturn = m_bestResult.netProfit;
        progress.bestDrawdown = m_bestResult.maxDrawdownPct;

        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startTime;
        progress.elapsedSeconds = elapsed / 1000.0;
        if (completedRuns > 0) {
            progress.estimatedRemainingSeconds = (progress.elapsedSeconds / completedRuns) *
                                                  (totalRuns - completedRuns);
        }

        // Build best params string
        QStringList paramParts;
        for (auto it = m_bestResult.parameters.begin(); it != m_bestResult.parameters.end(); ++it) {
            paramParts.append(QString("%1=%2").arg(it.key()).arg(it.value(), 0, 'f', 2));
        }
        progress.bestParams = paramParts.join(", ");

        emit this->progress(progress);
        emit singleResultReady(result);
    }
}

void BacktestWorker::runGeneticOptimization()
{
    // Genetic Algorithm implementation
    int populationSize = m_optConfig.gaPopulationSize;
    int generations = m_optConfig.gaGenerations;
    double mutationRate = m_optConfig.gaMutationRate;
    double crossoverRate = m_optConfig.gaCrossoverRate;

    // Initialize population with random parameter values
    QVector<QMap<QString, double>> population;
    for (int i = 0; i < populationSize; ++i) {
        QMap<QString, double> individual;
        for (const auto &range : m_optConfig.ranges) {
            double randVal = range.start + (double(rand()) / RAND_MAX) * (range.stop - range.start);
            // Round to step
            if (range.step > 0) {
                randVal = range.start + std::round((randVal - range.start) / range.step) * range.step;
            }
            individual[range.name] = randVal;
        }
        population.append(individual);
    }

    QVector<double> fitness(populationSize);

    for (int gen = 0; gen < generations && !m_stopRequested; ++gen) {
        // Evaluate fitness
        for (int i = 0; i < populationSize && !m_stopRequested; ++i) {
            while (m_paused && !m_stopRequested) {
                QThread::msleep(100);
            }

            auto result = executeBacktest(population[i]);
            fitness[i] = result.valid ? calculateFitness(result) : -1e18;

            if (result.valid) {
                QMutexLocker locker(&m_mutex);
                m_results.append(result);

                if (fitness[i] > calculateFitness(m_bestResult)) {
                    m_bestResult = result;
                }
            }
        }

        // Selection, crossover, mutation
        QVector<QMap<QString, double>> newPopulation;

        // Elitism - keep best individual
        int bestIdx = std::distance(fitness.begin(),
                                    std::max_element(fitness.begin(), fitness.end()));
        newPopulation.append(population[bestIdx]);

        // Generate rest of population
        while (newPopulation.size() < populationSize) {
            // Tournament selection
            auto selectParent = [&]() -> int {
                int a = rand() % populationSize;
                int b = rand() % populationSize;
                return fitness[a] > fitness[b] ? a : b;
            };

            int parent1 = selectParent();
            int parent2 = selectParent();

            QMap<QString, double> child;

            // Crossover
            if ((double(rand()) / RAND_MAX) < crossoverRate) {
                for (const auto &range : m_optConfig.ranges) {
                    if (rand() % 2 == 0) {
                        child[range.name] = population[parent1][range.name];
                    } else {
                        child[range.name] = population[parent2][range.name];
                    }
                }
            } else {
                child = population[parent1];
            }

            // Mutation
            for (const auto &range : m_optConfig.ranges) {
                if ((double(rand()) / RAND_MAX) < mutationRate) {
                    double randVal = range.start + (double(rand()) / RAND_MAX) * (range.stop - range.start);
                    if (range.step > 0) {
                        randVal = range.start + std::round((randVal - range.start) / range.step) * range.step;
                    }
                    child[range.name] = randVal;
                }
            }

            newPopulation.append(child);
        }

        population = newPopulation;

        // Emit progress
        BacktestProgress progress;
        progress.currentRun = (gen + 1) * populationSize;
        progress.totalRuns = generations * populationSize;
        progress.percentComplete = 100.0 * (gen + 1) / generations;
        progress.bestReturn = m_bestResult.netProfit;
        progress.bestDrawdown = m_bestResult.maxDrawdownPct;

        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startTime;
        progress.elapsedSeconds = elapsed / 1000.0;
        progress.estimatedRemainingSeconds = (progress.elapsedSeconds / (gen + 1)) * (generations - gen - 1);

        emit this->progress(progress);
    }
}

double BacktestWorker::calculateFitness(const SingleBacktestResult &result)
{
    if (!result.valid) return -1e18;

    switch (m_optConfig.criterion) {
    case OptimizationConfig::MaxProfit:
        return result.netProfit;

    case OptimizationConfig::MaxProfitFactor:
        return result.profitFactor;

    case OptimizationConfig::MaxSharpe:
        return result.sharpeRatio;

    case OptimizationConfig::MaxCalmar:
        return result.calmarRatio;

    case OptimizationConfig::MinDrawdown:
        return -result.maxDrawdownPct;

    case OptimizationConfig::MaxRecoveryFactor:
        return result.maxDrawdown > 0 ? result.netProfit / result.maxDrawdown : 0;

    case OptimizationConfig::Custom:
        return result.customScore;

    default:
        return result.netProfit;
    }
}

void BacktestWorker::generateParameterCombinations(QVector<QMap<QString, double>> &combinations)
{
    // Build list of values for each parameter
    QVector<QVector<double>> paramValues;
    QVector<QString> paramNames;

    for (const auto &range : m_optConfig.ranges) {
        QVector<double> values;
        for (double v = range.start; v <= range.stop + 1e-9; v += range.step) {
            values.append(v);
        }
        paramValues.append(values);
        paramNames.append(range.name);
    }

    if (paramValues.isEmpty()) return;

    // Calculate total combinations
    int totalCombinations = 1;
    for (const auto &values : paramValues) {
        totalCombinations *= values.size();
    }

    combinations.reserve(totalCombinations);

    // Generate all combinations using iterative approach
    QVector<int> indices(paramValues.size(), 0);

    while (true) {
        // Build current combination
        QMap<QString, double> combo;
        for (int i = 0; i < paramValues.size(); ++i) {
            combo[paramNames[i]] = paramValues[i][indices[i]];
        }
        combinations.append(combo);

        // Increment indices
        int dim = paramValues.size() - 1;
        while (dim >= 0) {
            indices[dim]++;
            if (indices[dim] < paramValues[dim].size()) {
                break;
            }
            indices[dim] = 0;
            dim--;
        }

        if (dim < 0) break;
    }
}

SingleBacktestResult BacktestWorker::executeBacktest(const QMap<QString, double> &params)
{
    SingleBacktestResult result;
    result.parameters = params;
    result.valid = false;

    try {
        // Create tick data config
        TickDataConfig tick_config;
        tick_config.file_path = m_config.tickDataPath.toStdString();
        tick_config.format = TickDataFormat::MT5_CSV;

        // Create backtest config
        TickBacktestConfig config;
        config.symbol = m_config.symbol.toStdString();
        config.initial_balance = m_config.initialBalance;
        config.contract_size = m_config.contractSize;
        config.leverage = m_config.leverage;
        config.pip_size = m_config.pipSize;
        config.swap_long = m_config.swapLong;
        config.swap_short = m_config.swapShort;
        config.swap_mode = m_config.swapMode;
        config.swap_3days = m_config.swap3Days;
        config.start_date = m_config.startDate.toStdString();
        config.end_date = m_config.endDate.toStdString();
        config.tick_data_config = tick_config;
        config.verbose = false;  // Quiet mode for optimization runs

        // Enable equity curve tracking for metrics calculation
        config.track_equity_curve = true;
        config.equity_sample_interval = 1000;

        // Create engine
        TickBasedEngine engine(config);

        // Create strategy with parameters
        double survive = params.value("survive_pct", 13.0);
        double spacing = params.value("base_spacing", 1.5);
        double lookback = params.value("lookback_hours", 4.0);

        // Check for strategy type parameter
        QString strategyType = "fillup";  // default
        if (m_config.parameters.contains("strategy_type")) {
            // Strategy type could be encoded as: 0=fillup, 1=combined
            int stratType = static_cast<int>(m_config.parameters.value("strategy_type", 0));
            strategyType = (stratType == 1) ? "combined" : "fillup";
        }

        if (strategyType == "combined") {
            // Use CombinedJu strategy
            StrategyCombinedJu::Config ju_config;
            ju_config.survive_pct = survive;
            ju_config.base_spacing = spacing;
            ju_config.min_volume = 0.01;
            ju_config.max_volume = 10.0;
            ju_config.contract_size = m_config.contractSize;
            ju_config.leverage = m_config.leverage;

            StrategyCombinedJu strategy(ju_config);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
        } else {
            // Use FillUpOscillation strategy (default)
            FillUpOscillation strategy(survive, spacing, 0.01, 10.0,
                                       m_config.contractSize, m_config.leverage,
                                       FillUpOscillation::ADAPTIVE_SPACING,
                                       0.1, 30.0, lookback);

            engine.Run([&strategy](const Tick& tick, TickBasedEngine& eng) {
                strategy.OnTick(tick, eng);
            });
        }

        // Get results
        auto engineResults = engine.GetResults();

        result.finalBalance = engineResults.final_balance;
        result.netProfit = engineResults.final_balance - m_config.initialBalance;
        result.profitFactor = engineResults.profit_factor;
        result.maxDrawdown = engineResults.max_drawdown;
        result.maxDrawdownPct = engineResults.max_drawdown_pct;
        result.sharpeRatio = engineResults.sharpe_ratio;
        result.sortinoRatio = engineResults.sortino_ratio;
        result.calmarRatio = result.maxDrawdownPct > 0 ?
                            (result.netProfit / m_config.initialBalance * 100.0) / result.maxDrawdownPct : 0;
        result.totalTrades = static_cast<int>(engineResults.total_trades);
        result.winRate = engineResults.win_rate;
        result.valid = true;

    } catch (const std::exception& e) {
        emit error(QString("Backtest error: %1").arg(e.what()));
        result.valid = false;
    }

    return result;
}
