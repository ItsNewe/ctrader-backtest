/**
 * ResultsTable Widget - Display backtest and optimization results
 */

#ifndef RESULTSTABLE_H
#define RESULTSTABLE_H

#include <QWidget>
#include <QVector>
#include <QMap>

class QTableWidget;
class QTabWidget;
class QLabel;
class QPushButton;
class QComboBox;

struct BacktestResult {
    int id;
    QMap<QString, double> parameters;

    // Performance metrics
    double finalBalance;
    double netProfit;
    double grossProfit;
    double grossLoss;
    double profitFactor;
    double expectedPayoff;

    // Risk metrics
    double maxDrawdown;
    double maxDrawdownPct;
    double relativeDrawdown;
    double sharpeRatio;
    double sortinoRatio;
    double calmarRatio;

    // Trade statistics
    int totalTrades;
    int profitTrades;
    int lossTrades;
    double winRate;
    double avgWin;
    double avgLoss;
    double maxConsecutiveWins;
    double maxConsecutiveLosses;

    // Time statistics
    double avgTradeDuration;
    double longestTradeDuration;

    // Custom score for optimization
    double customScore;
};

class ResultsTable : public QWidget
{
    Q_OBJECT

public:
    explicit ResultsTable(QWidget *parent = nullptr);

    // Data management
    void clearResults();
    void addResult(const BacktestResult &result);
    void setResults(const QVector<BacktestResult> &results);
    QVector<BacktestResult> getResults() const;
    BacktestResult getResult(int index) const;
    int resultCount() const;

    // Selection
    int selectedResultIndex() const;
    BacktestResult selectedResult() const;

    // Sorting/Filtering
    void sortByColumn(int column, Qt::SortOrder order = Qt::DescendingOrder);
    void filterByMinProfit(double minProfit);
    void filterByMaxDrawdown(double maxDD);
    void clearFilters();

    // Export
    void exportToCSV(const QString &filePath);
    void exportToHTML(const QString &filePath);

signals:
    void resultSelected(int index);
    void resultDoubleClicked(int index);
    void exportRequested();

public slots:
    void selectResult(int index);
    void showOptimizationSummary();

private:
    void setupUI();
    void setupSummaryTab();
    void setupTradesTab();
    void setupOptimizationTab();
    void updateSummary(const BacktestResult &result);
    void populateOptimizationTable();
    QString formatDuration(double seconds) const;
    QString formatNumber(double value, int decimals = 2) const;
    QString formatPercent(double value) const;
    QString formatCurrency(double value) const;

    QTabWidget *m_tabWidget;

    // Summary tab
    QWidget *m_summaryTab;
    QLabel *m_lblFinalBalance;
    QLabel *m_lblNetProfit;
    QLabel *m_lblProfitFactor;
    QLabel *m_lblMaxDrawdown;
    QLabel *m_lblSharpeRatio;
    QLabel *m_lblTotalTrades;
    QLabel *m_lblWinRate;
    QLabel *m_lblExpectedPayoff;

    // Trades tab
    QTableWidget *m_tradesTable;

    // Optimization tab
    QTableWidget *m_optimizationTable;
    QComboBox *m_sortColumn;
    QPushButton *m_btnExport;

    QVector<BacktestResult> m_results;
    int m_selectedIndex;
};

#endif // RESULTSTABLE_H
