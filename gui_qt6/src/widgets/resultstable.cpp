/**
 * ResultsTable Widget Implementation
 */

#include "resultstable.h"

#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QHeaderView>
#include <QFile>
#include <QTextStream>
#include <algorithm>

ResultsTable::ResultsTable(QWidget *parent)
    : QWidget(parent)
    , m_selectedIndex(-1)
{
    setupUI();
}

void ResultsTable::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_tabWidget = new QTabWidget;

    setupSummaryTab();
    setupTradesTab();
    setupOptimizationTab();

    mainLayout->addWidget(m_tabWidget);
}

void ResultsTable::setupSummaryTab()
{
    m_summaryTab = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(m_summaryTab);

    // Performance metrics
    QGroupBox *perfGroup = new QGroupBox(tr("Performance"));
    QFormLayout *perfLayout = new QFormLayout(perfGroup);

    m_lblFinalBalance = new QLabel("-");
    m_lblNetProfit = new QLabel("-");
    m_lblProfitFactor = new QLabel("-");
    m_lblExpectedPayoff = new QLabel("-");

    perfLayout->addRow(tr("Final Balance:"), m_lblFinalBalance);
    perfLayout->addRow(tr("Net Profit:"), m_lblNetProfit);
    perfLayout->addRow(tr("Profit Factor:"), m_lblProfitFactor);
    perfLayout->addRow(tr("Expected Payoff:"), m_lblExpectedPayoff);

    // Risk metrics
    QGroupBox *riskGroup = new QGroupBox(tr("Risk Metrics"));
    QFormLayout *riskLayout = new QFormLayout(riskGroup);

    m_lblMaxDrawdown = new QLabel("-");
    m_lblSharpeRatio = new QLabel("-");

    riskLayout->addRow(tr("Max Drawdown:"), m_lblMaxDrawdown);
    riskLayout->addRow(tr("Sharpe Ratio:"), m_lblSharpeRatio);

    // Trade statistics
    QGroupBox *tradeGroup = new QGroupBox(tr("Trade Statistics"));
    QFormLayout *tradeLayout = new QFormLayout(tradeGroup);

    m_lblTotalTrades = new QLabel("-");
    m_lblWinRate = new QLabel("-");

    tradeLayout->addRow(tr("Total Trades:"), m_lblTotalTrades);
    tradeLayout->addRow(tr("Win Rate:"), m_lblWinRate);

    layout->addWidget(perfGroup);
    layout->addWidget(riskGroup);
    layout->addWidget(tradeGroup);
    layout->addStretch();

    m_tabWidget->addTab(m_summaryTab, tr("Summary"));
}

void ResultsTable::setupTradesTab()
{
    QWidget *tradesTab = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(tradesTab);

    m_tradesTable = new QTableWidget;
    m_tradesTable->setColumnCount(8);
    m_tradesTable->setHorizontalHeaderLabels({
        tr("Time"), tr("Type"), tr("Symbol"), tr("Volume"),
        tr("Price"), tr("Profit"), tr("Balance"), tr("Comment")
    });

    m_tradesTable->horizontalHeader()->setStretchLastSection(true);
    m_tradesTable->setAlternatingRowColors(true);
    m_tradesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tradesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tradesTable->setSortingEnabled(true);

    layout->addWidget(m_tradesTable);

    m_tabWidget->addTab(tradesTab, tr("Trades"));
}

void ResultsTable::setupOptimizationTab()
{
    QWidget *optTab = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(optTab);

    // Controls
    QHBoxLayout *controlsLayout = new QHBoxLayout;

    QLabel *sortLabel = new QLabel(tr("Sort by:"));
    m_sortColumn = new QComboBox;
    m_sortColumn->addItems({
        tr("Net Profit"), tr("Profit Factor"), tr("Max Drawdown"),
        tr("Sharpe Ratio"), tr("Win Rate"), tr("Total Trades")
    });

    m_btnExport = new QPushButton(tr("Export..."));
    connect(m_btnExport, &QPushButton::clicked, this, &ResultsTable::exportRequested);

    controlsLayout->addWidget(sortLabel);
    controlsLayout->addWidget(m_sortColumn);
    controlsLayout->addStretch();
    controlsLayout->addWidget(m_btnExport);

    connect(m_sortColumn, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        // Map combo index to column index in table
        int columnMap[] = {2, 3, 4, 5, 10, 9};  // Adjust based on table layout
        if (index >= 0 && index < 6) {
            m_optimizationTable->sortByColumn(columnMap[index], Qt::DescendingOrder);
        }
    });

    // Table
    m_optimizationTable = new QTableWidget;
    m_optimizationTable->setColumnCount(12);
    m_optimizationTable->setHorizontalHeaderLabels({
        tr("Pass"), tr("Result"), tr("Profit"), tr("Profit Factor"),
        tr("Max DD"), tr("Sharpe"), tr("Sortino"), tr("Calmar"),
        tr("Recovery"), tr("Trades"), tr("Win Rate"), tr("Parameters")
    });

    m_optimizationTable->horizontalHeader()->setStretchLastSection(true);
    m_optimizationTable->setAlternatingRowColors(true);
    m_optimizationTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_optimizationTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_optimizationTable->setSortingEnabled(true);

    connect(m_optimizationTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        int row = m_optimizationTable->currentRow();
        if (row >= 0 && row < m_results.size()) {
            m_selectedIndex = row;
            updateSummary(m_results[row]);
            emit resultSelected(row);
        }
    });

    connect(m_optimizationTable, &QTableWidget::cellDoubleClicked,
            this, [this](int row, int) {
        if (row >= 0 && row < m_results.size()) {
            emit resultDoubleClicked(row);
        }
    });

    layout->addLayout(controlsLayout);
    layout->addWidget(m_optimizationTable);

    m_tabWidget->addTab(optTab, tr("Optimization"));
}

void ResultsTable::updateSummary(const BacktestResult &result)
{
    m_lblFinalBalance->setText(formatCurrency(result.finalBalance));
    m_lblNetProfit->setText(formatCurrency(result.netProfit));
    m_lblProfitFactor->setText(formatNumber(result.profitFactor));
    m_lblExpectedPayoff->setText(formatCurrency(result.expectedPayoff));
    m_lblMaxDrawdown->setText(QString("%1 (%2)")
        .arg(formatCurrency(result.maxDrawdown))
        .arg(formatPercent(result.maxDrawdownPct)));
    m_lblSharpeRatio->setText(formatNumber(result.sharpeRatio));
    m_lblTotalTrades->setText(QString::number(result.totalTrades));
    m_lblWinRate->setText(formatPercent(result.winRate));

    // Color coding
    QString profitColor = result.netProfit >= 0 ? "#00ff00" : "#ff0000";
    m_lblNetProfit->setStyleSheet(QString("color: %1;").arg(profitColor));
}

void ResultsTable::clearResults()
{
    m_results.clear();
    m_selectedIndex = -1;
    m_optimizationTable->setRowCount(0);
    m_tradesTable->setRowCount(0);

    // Reset summary labels
    m_lblFinalBalance->setText("-");
    m_lblNetProfit->setText("-");
    m_lblProfitFactor->setText("-");
    m_lblExpectedPayoff->setText("-");
    m_lblMaxDrawdown->setText("-");
    m_lblSharpeRatio->setText("-");
    m_lblTotalTrades->setText("-");
    m_lblWinRate->setText("-");
}

void ResultsTable::addResult(const BacktestResult &result)
{
    m_results.append(result);

    int row = m_optimizationTable->rowCount();
    m_optimizationTable->insertRow(row);

    // Build parameter string
    QStringList paramParts;
    for (auto it = result.parameters.begin(); it != result.parameters.end(); ++it) {
        paramParts.append(QString("%1=%2").arg(it.key()).arg(it.value(), 0, 'f', 2));
    }

    m_optimizationTable->setItem(row, 0, new QTableWidgetItem(QString::number(result.id)));
    m_optimizationTable->setItem(row, 1, new QTableWidgetItem(formatCurrency(result.finalBalance)));
    m_optimizationTable->setItem(row, 2, new QTableWidgetItem(formatCurrency(result.netProfit)));
    m_optimizationTable->setItem(row, 3, new QTableWidgetItem(formatNumber(result.profitFactor)));
    m_optimizationTable->setItem(row, 4, new QTableWidgetItem(formatPercent(result.maxDrawdownPct)));
    m_optimizationTable->setItem(row, 5, new QTableWidgetItem(formatNumber(result.sharpeRatio)));
    m_optimizationTable->setItem(row, 6, new QTableWidgetItem(formatNumber(result.sortinoRatio)));
    m_optimizationTable->setItem(row, 7, new QTableWidgetItem(formatNumber(result.calmarRatio)));
    m_optimizationTable->setItem(row, 8, new QTableWidgetItem(formatNumber(result.profitFactor > 0 ? result.netProfit / result.maxDrawdown : 0)));
    m_optimizationTable->setItem(row, 9, new QTableWidgetItem(QString::number(result.totalTrades)));
    m_optimizationTable->setItem(row, 10, new QTableWidgetItem(formatPercent(result.winRate)));
    m_optimizationTable->setItem(row, 11, new QTableWidgetItem(paramParts.join(", ")));

    // Color code profit column
    QTableWidgetItem *profitItem = m_optimizationTable->item(row, 2);
    if (result.netProfit >= 0) {
        profitItem->setForeground(QBrush(QColor(0, 255, 0)));
    } else {
        profitItem->setForeground(QBrush(QColor(255, 0, 0)));
    }

    // Update summary if first result
    if (m_results.size() == 1) {
        updateSummary(result);
    }
}

void ResultsTable::setResults(const QVector<BacktestResult> &results)
{
    clearResults();
    for (const auto &result : results) {
        addResult(result);
    }
}

QVector<BacktestResult> ResultsTable::getResults() const
{
    return m_results;
}

BacktestResult ResultsTable::getResult(int index) const
{
    if (index >= 0 && index < m_results.size()) {
        return m_results[index];
    }
    return BacktestResult();
}

int ResultsTable::resultCount() const
{
    return m_results.size();
}

int ResultsTable::selectedResultIndex() const
{
    return m_selectedIndex;
}

BacktestResult ResultsTable::selectedResult() const
{
    return getResult(m_selectedIndex);
}

void ResultsTable::selectResult(int index)
{
    if (index >= 0 && index < m_optimizationTable->rowCount()) {
        m_optimizationTable->selectRow(index);
    }
}

void ResultsTable::sortByColumn(int column, Qt::SortOrder order)
{
    m_optimizationTable->sortByColumn(column, order);
}

void ResultsTable::filterByMinProfit(double minProfit)
{
    for (int i = 0; i < m_optimizationTable->rowCount(); ++i) {
        bool show = m_results[i].netProfit >= minProfit;
        m_optimizationTable->setRowHidden(i, !show);
    }
}

void ResultsTable::filterByMaxDrawdown(double maxDD)
{
    for (int i = 0; i < m_optimizationTable->rowCount(); ++i) {
        bool show = m_results[i].maxDrawdownPct <= maxDD;
        m_optimizationTable->setRowHidden(i, !show);
    }
}

void ResultsTable::clearFilters()
{
    for (int i = 0; i < m_optimizationTable->rowCount(); ++i) {
        m_optimizationTable->setRowHidden(i, false);
    }
}

void ResultsTable::showOptimizationSummary()
{
    m_tabWidget->setCurrentIndex(2);  // Optimization tab
}

void ResultsTable::exportToCSV(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream out(&file);

    // Header
    out << "Pass,Final Balance,Net Profit,Profit Factor,Max DD %,Sharpe,Sortino,Calmar,Trades,Win Rate,Parameters\n";

    // Data
    for (const auto &result : m_results) {
        QStringList paramParts;
        for (auto it = result.parameters.begin(); it != result.parameters.end(); ++it) {
            paramParts.append(QString("%1=%2").arg(it.key()).arg(it.value(), 0, 'f', 4));
        }

        out << result.id << ","
            << result.finalBalance << ","
            << result.netProfit << ","
            << result.profitFactor << ","
            << result.maxDrawdownPct << ","
            << result.sharpeRatio << ","
            << result.sortinoRatio << ","
            << result.calmarRatio << ","
            << result.totalTrades << ","
            << result.winRate << ","
            << "\"" << paramParts.join("; ") << "\"\n";
    }
}

void ResultsTable::exportToHTML(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream out(&file);

    out << "<!DOCTYPE html>\n<html>\n<head>\n";
    out << "<title>Backtest Results</title>\n";
    out << "<style>\n";
    out << "body { font-family: Arial, sans-serif; background: #1a1a1a; color: #fff; padding: 20px; }\n";
    out << "table { border-collapse: collapse; width: 100%; }\n";
    out << "th, td { border: 1px solid #444; padding: 8px; text-align: right; }\n";
    out << "th { background: #333; }\n";
    out << "tr:nth-child(even) { background: #252525; }\n";
    out << "tr:hover { background: #353535; }\n";
    out << ".positive { color: #00ff00; }\n";
    out << ".negative { color: #ff0000; }\n";
    out << "</style>\n</head>\n<body>\n";

    out << "<h1>Optimization Results</h1>\n";
    out << "<p>Total passes: " << m_results.size() << "</p>\n";

    out << "<table>\n<tr>";
    out << "<th>Pass</th><th>Final Balance</th><th>Net Profit</th>";
    out << "<th>Profit Factor</th><th>Max DD</th><th>Sharpe</th>";
    out << "<th>Trades</th><th>Win Rate</th><th>Parameters</th>";
    out << "</tr>\n";

    for (const auto &result : m_results) {
        QString profitClass = result.netProfit >= 0 ? "positive" : "negative";

        QStringList paramParts;
        for (auto it = result.parameters.begin(); it != result.parameters.end(); ++it) {
            paramParts.append(QString("%1=%2").arg(it.key()).arg(it.value(), 0, 'f', 2));
        }

        out << "<tr>";
        out << "<td>" << result.id << "</td>";
        out << "<td>" << formatCurrency(result.finalBalance) << "</td>";
        out << "<td class=\"" << profitClass << "\">" << formatCurrency(result.netProfit) << "</td>";
        out << "<td>" << formatNumber(result.profitFactor) << "</td>";
        out << "<td>" << formatPercent(result.maxDrawdownPct) << "</td>";
        out << "<td>" << formatNumber(result.sharpeRatio) << "</td>";
        out << "<td>" << result.totalTrades << "</td>";
        out << "<td>" << formatPercent(result.winRate) << "</td>";
        out << "<td style=\"text-align:left;\">" << paramParts.join(", ") << "</td>";
        out << "</tr>\n";
    }

    out << "</table>\n</body>\n</html>\n";
}

QString ResultsTable::formatDuration(double seconds) const
{
    if (seconds < 60) {
        return QString("%1s").arg(seconds, 0, 'f', 1);
    } else if (seconds < 3600) {
        return QString("%1m %2s").arg(int(seconds) / 60).arg(int(seconds) % 60);
    } else {
        int hours = int(seconds) / 3600;
        int mins = (int(seconds) % 3600) / 60;
        return QString("%1h %2m").arg(hours).arg(mins);
    }
}

QString ResultsTable::formatNumber(double value, int decimals) const
{
    return QString::number(value, 'f', decimals);
}

QString ResultsTable::formatPercent(double value) const
{
    return QString("%1%").arg(value, 0, 'f', 2);
}

QString ResultsTable::formatCurrency(double value) const
{
    return QString("$%1").arg(value, 0, 'f', 2);
}
