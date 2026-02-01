/**
 * BacktestWorker - Background thread for running backtests
 */

#ifndef BACKTESTWORKER_H
#define BACKTESTWORKER_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QVector>
#include <QMap>
#include <atomic>

struct BacktestConfig {
    // Symbol settings
    QString symbol;
    QString tickDataPath;
    double contractSize;
    double leverage;
    double pipSize;

    // Swap settings
    double swapLong;
    double swapShort;
    int swapMode;
    int swap3Days;

    // Account settings
    double initialBalance;
    QString currency;

    // Date range
    QString startDate;
    QString endDate;

    // Strategy parameters (flexible map)
    QMap<QString, double> parameters;
};

struct OptimizationConfig {
    struct ParamRange {
        QString name;
        double start;
        double stop;
        double step;
    };

    QVector<ParamRange> ranges;
    int numThreads;

    enum Criterion {
        MaxProfit,
        MaxProfitFactor,
        MaxSharpe,
        MaxCalmar,
        MinDrawdown,
        MaxRecoveryFactor,
        Custom
    } criterion;

    // For genetic algorithm
    bool useGeneticAlgorithm;
    int gaPopulationSize;
    int gaGenerations;
    double gaMutationRate;
    double gaCrossoverRate;
};

struct BacktestProgress {
    int currentRun;
    int totalRuns;
    double percentComplete;
    double bestReturn;
    double bestDrawdown;
    QString bestParams;
    qint64 ticksProcessed;
    qint64 totalTicks;
    double elapsedSeconds;
    double estimatedRemainingSeconds;
};

struct SingleBacktestResult {
    QMap<QString, double> parameters;
    double finalBalance;
    double netProfit;
    double profitFactor;
    double maxDrawdown;
    double maxDrawdownPct;
    double sharpeRatio;
    double sortinoRatio;
    double calmarRatio;
    int totalTrades;
    double winRate;
    double customScore;
    bool valid;
};

class BacktestWorker : public QObject
{
    Q_OBJECT

public:
    explicit BacktestWorker(QObject *parent = nullptr);
    ~BacktestWorker();

    // Configuration
    void setBacktestConfig(const BacktestConfig &config);
    void setOptimizationConfig(const OptimizationConfig &config);

    // State
    bool isRunning() const { return m_running; }
    bool isPaused() const { return m_paused; }

    // Results
    QVector<SingleBacktestResult> getResults() const;
    SingleBacktestResult getBestResult() const;

public slots:
    void startSingleBacktest();
    void startOptimization();
    void pause();
    void resume();
    void stop();

signals:
    void started();
    void progress(const BacktestProgress &progress);
    void singleResultReady(const SingleBacktestResult &result);
    void optimizationComplete(const QVector<SingleBacktestResult> &results);
    void backtestComplete(const SingleBacktestResult &result);
    void error(const QString &message);
    void finished();

private:
    void runSingleBacktest(const QMap<QString, double> &params);
    void runGridOptimization();
    void runGeneticOptimization();
    double calculateFitness(const SingleBacktestResult &result);
    void generateParameterCombinations(QVector<QMap<QString, double>> &combinations);

    // Engine integration
    SingleBacktestResult executeBacktest(const QMap<QString, double> &params);

    BacktestConfig m_config;
    OptimizationConfig m_optConfig;

    std::atomic<bool> m_running;
    std::atomic<bool> m_paused;
    std::atomic<bool> m_stopRequested;

    QMutex m_mutex;
    QVector<SingleBacktestResult> m_results;
    SingleBacktestResult m_bestResult;

    qint64 m_startTime;
};

#endif // BACKTESTWORKER_H
