/**
 * EquityChart Widget
 * Interactive equity curve chart using QtCharts
 */

#ifndef EQUITYCHART_H
#define EQUITYCHART_H

#include <QWidget>
#include <QVector>
#include <QPair>

QT_BEGIN_NAMESPACE
class QChart;
class QChartView;
class QLineSeries;
class QAreaSeries;
class QDateTimeAxis;
class QValueAxis;
QT_END_NAMESPACE

using namespace QtCharts;

class EquityChart : public QWidget
{
    Q_OBJECT

public:
    explicit EquityChart(QWidget *parent = nullptr);

    // Set equity curve data
    void setEquityCurve(const QVector<QPair<qint64, double>> &data);

    // Set drawdown curve
    void setDrawdownCurve(const QVector<QPair<qint64, double>> &data);

    // Add trade marker
    void addTradeMarker(qint64 timestamp, double price, bool isBuy, double profit);

    // Clear all data
    void clear();

    // Chart controls
    void zoomIn();
    void zoomOut();
    void resetZoom();

signals:
    void chartClicked(qint64 timestamp, double value);

private:
    void setupChart();
    void updateAxes();

    QChartView *m_chartView;
    QChart *m_chart;

    // Series
    QLineSeries *m_equitySeries;
    QLineSeries *m_balanceSeries;
    QAreaSeries *m_drawdownArea;
    QLineSeries *m_drawdownSeries;

    // Axes
    QDateTimeAxis *m_axisX;
    QValueAxis *m_axisY;
    QValueAxis *m_axisY2;  // For drawdown

    // Data range
    double m_minEquity;
    double m_maxEquity;
    qint64 m_minTime;
    qint64 m_maxTime;
};

#endif // EQUITYCHART_H
