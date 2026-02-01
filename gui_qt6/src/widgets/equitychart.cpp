/**
 * EquityChart Widget Implementation
 */

#include "equitychart.h"

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QScatterSeries>
#include <QVBoxLayout>
#include <QDateTime>
#include <QToolTip>

EquityChart::EquityChart(QWidget *parent)
    : QWidget(parent)
    , m_minEquity(0)
    , m_maxEquity(10000)
    , m_minTime(0)
    , m_maxTime(0)
{
    setupChart();
}

void EquityChart::setupChart()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Create chart
    m_chart = new QChart();
    m_chart->setTitle("Equity Curve");
    m_chart->setAnimationOptions(QChart::NoAnimation);
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);

    // Dark theme
    m_chart->setBackgroundBrush(QBrush(QColor(35, 35, 35)));
    m_chart->setTitleBrush(QBrush(Qt::white));
    m_chart->legend()->setLabelColor(Qt::white);

    // Create equity series
    m_equitySeries = new QLineSeries();
    m_equitySeries->setName("Equity");
    m_equitySeries->setColor(QColor(42, 130, 218));  // Blue
    m_equitySeries->setPen(QPen(QColor(42, 130, 218), 2));

    // Create balance series
    m_balanceSeries = new QLineSeries();
    m_balanceSeries->setName("Balance");
    m_balanceSeries->setColor(QColor(255, 165, 0));  // Orange
    m_balanceSeries->setPen(QPen(QColor(255, 165, 0), 1));

    // Create drawdown series (for area)
    m_drawdownSeries = new QLineSeries();
    m_drawdownSeries->setName("Drawdown");
    QLineSeries *zeroLine = new QLineSeries();

    m_drawdownArea = new QAreaSeries(m_drawdownSeries, zeroLine);
    m_drawdownArea->setName("Drawdown");
    m_drawdownArea->setColor(QColor(200, 50, 50, 100));  // Semi-transparent red
    m_drawdownArea->setBorderColor(QColor(200, 50, 50));

    // Add series to chart
    m_chart->addSeries(m_drawdownArea);
    m_chart->addSeries(m_equitySeries);
    m_chart->addSeries(m_balanceSeries);

    // Create axes
    m_axisX = new QDateTimeAxis();
    m_axisX->setFormat("yyyy-MM-dd");
    m_axisX->setTitleText("Date");
    m_axisX->setLabelsColor(Qt::white);
    m_axisX->setTitleBrush(QBrush(Qt::white));
    m_axisX->setGridLineColor(QColor(80, 80, 80));

    m_axisY = new QValueAxis();
    m_axisY->setTitleText("Equity ($)");
    m_axisY->setLabelsColor(Qt::white);
    m_axisY->setTitleBrush(QBrush(Qt::white));
    m_axisY->setGridLineColor(QColor(80, 80, 80));
    m_axisY->setLabelFormat("$%.0f");

    m_axisY2 = new QValueAxis();
    m_axisY2->setTitleText("Drawdown (%)");
    m_axisY2->setLabelsColor(Qt::white);
    m_axisY2->setTitleBrush(QBrush(Qt::white));
    m_axisY2->setRange(0, 100);
    m_axisY2->setLabelFormat("%.1f%%");

    // Attach axes
    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);
    m_chart->addAxis(m_axisY2, Qt::AlignRight);

    m_equitySeries->attachAxis(m_axisX);
    m_equitySeries->attachAxis(m_axisY);
    m_balanceSeries->attachAxis(m_axisX);
    m_balanceSeries->attachAxis(m_axisY);
    m_drawdownArea->attachAxis(m_axisX);
    m_drawdownArea->attachAxis(m_axisY2);

    // Create chart view
    m_chartView = new QChartView(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);

    layout->addWidget(m_chartView);

    // Connect hover signals for tooltips
    connect(m_equitySeries, &QLineSeries::hovered, this, [this](const QPointF &point, bool state) {
        if (state) {
            QDateTime dt = QDateTime::fromMSecsSinceEpoch(point.x());
            QToolTip::showText(QCursor::pos(),
                QString("Date: %1\nEquity: $%2")
                    .arg(dt.toString("yyyy-MM-dd HH:mm"))
                    .arg(point.y(), 0, 'f', 2));
        }
    });
}

void EquityChart::setEquityCurve(const QVector<QPair<qint64, double>> &data)
{
    m_equitySeries->clear();

    if (data.isEmpty()) return;

    m_minEquity = data.first().second;
    m_maxEquity = data.first().second;
    m_minTime = data.first().first;
    m_maxTime = data.last().first;

    QList<QPointF> points;
    for (const auto &p : data) {
        points.append(QPointF(p.first, p.second));
        if (p.second < m_minEquity) m_minEquity = p.second;
        if (p.second > m_maxEquity) m_maxEquity = p.second;
    }

    m_equitySeries->replace(points);
    updateAxes();
}

void EquityChart::setDrawdownCurve(const QVector<QPair<qint64, double>> &data)
{
    m_drawdownSeries->clear();

    if (data.isEmpty()) return;

    QList<QPointF> points;
    for (const auto &p : data) {
        points.append(QPointF(p.first, p.second));
    }

    m_drawdownSeries->replace(points);
}

void EquityChart::addTradeMarker(qint64 timestamp, double price, bool isBuy, double profit)
{
    // Create scatter series for trade markers if not exists
    static QScatterSeries *buyMarkers = nullptr;
    static QScatterSeries *sellMarkers = nullptr;

    if (!buyMarkers) {
        buyMarkers = new QScatterSeries();
        buyMarkers->setName("Buy");
        buyMarkers->setMarkerShape(QScatterSeries::MarkerShapeTriangle);
        buyMarkers->setMarkerSize(10);
        buyMarkers->setColor(Qt::green);
        m_chart->addSeries(buyMarkers);
        buyMarkers->attachAxis(m_axisX);
        buyMarkers->attachAxis(m_axisY);
    }

    if (!sellMarkers) {
        sellMarkers = new QScatterSeries();
        sellMarkers->setName("Sell");
        sellMarkers->setMarkerShape(QScatterSeries::MarkerShapeTriangle);
        sellMarkers->setMarkerSize(10);
        sellMarkers->setColor(Qt::red);
        m_chart->addSeries(sellMarkers);
        sellMarkers->attachAxis(m_axisX);
        sellMarkers->attachAxis(m_axisY);
    }

    if (isBuy) {
        buyMarkers->append(timestamp, price);
    } else {
        sellMarkers->append(timestamp, price);
    }
}

void EquityChart::clear()
{
    m_equitySeries->clear();
    m_balanceSeries->clear();
    m_drawdownSeries->clear();

    // Remove any scatter series (trade markers)
    QList<QAbstractSeries*> seriesList = m_chart->series();
    for (auto *series : seriesList) {
        if (auto *scatter = qobject_cast<QScatterSeries*>(series)) {
            m_chart->removeSeries(scatter);
            delete scatter;
        }
    }

    m_minEquity = 0;
    m_maxEquity = 10000;
}

void EquityChart::zoomIn()
{
    m_chart->zoomIn();
}

void EquityChart::zoomOut()
{
    m_chart->zoomOut();
}

void EquityChart::resetZoom()
{
    m_chart->zoomReset();
    updateAxes();
}

void EquityChart::updateAxes()
{
    // Add some padding
    double range = m_maxEquity - m_minEquity;
    double padding = range * 0.1;

    m_axisY->setRange(m_minEquity - padding, m_maxEquity + padding);

    if (m_minTime > 0 && m_maxTime > m_minTime) {
        m_axisX->setRange(
            QDateTime::fromMSecsSinceEpoch(m_minTime),
            QDateTime::fromMSecsSinceEpoch(m_maxTime)
        );
    }
}
