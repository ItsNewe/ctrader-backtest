#ifndef REPORT_GENERATOR_H
#define REPORT_GENERATOR_H

#include "tick_based_engine.h"
#include "monte_carlo.h"
#include "walk_forward.h"
#include <string>
#include <vector>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace backtest {

/**
 * Report Generator
 *
 * Creates professional backtest reports in multiple formats.
 */

struct ReportConfig {
    std::string title = "Backtest Report";
    std::string strategy_name = "Unknown Strategy";
    std::string symbol = "XAUUSD";
    std::string period = "";

    bool include_equity_curve = true;
    bool include_trade_list = true;
    bool include_monthly_breakdown = true;
    bool include_monte_carlo = false;
    bool include_walk_forward = false;

    std::string output_path = "report.html";
};

struct EquityPoint {
    std::string timestamp;
    double equity;
    double balance;
    double drawdown_pct;
};

struct MonthlyStats {
    int year;
    int month;
    double profit;
    int trades;
    double max_dd;
    double win_rate;
};

class ReportGenerator {
public:
    explicit ReportGenerator(const ReportConfig& config) : config_(config) {}

    /**
     * Generate HTML report from backtest results
     */
    void GenerateHTML(
        const BacktestResults& results,
        const std::vector<Trade>& trades,
        const std::vector<EquityPoint>& equity_curve,
        const MonteCarloResult* mc_result = nullptr,
        const WalkForwardResult* wf_result = nullptr
    ) {
        std::ofstream out(config_.output_path);
        if (!out) {
            throw std::runtime_error("Cannot create report file: " + config_.output_path);
        }

        out << GenerateHTMLHeader();
        out << GenerateSummarySection(results);
        out << GenerateMetricsSection(results);

        if (config_.include_equity_curve && !equity_curve.empty()) {
            out << GenerateEquityCurveSection(equity_curve);
        }

        if (config_.include_monthly_breakdown && !trades.empty()) {
            out << GenerateMonthlySection(trades);
        }

        if (config_.include_monte_carlo && mc_result) {
            out << GenerateMonteCarloSection(*mc_result);
        }

        if (config_.include_walk_forward && wf_result) {
            out << GenerateWalkForwardSection(*wf_result);
        }

        if (config_.include_trade_list && !trades.empty()) {
            out << GenerateTradeListSection(trades);
        }

        out << GenerateHTMLFooter();
        out.close();
    }

    /**
     * Generate CSV report (raw data)
     */
    void GenerateCSV(
        const BacktestResults& results,
        const std::vector<Trade>& trades,
        const std::string& output_path
    ) {
        std::ofstream out(output_path);
        if (!out) return;

        out << "id,symbol,direction,entry_time,entry_price,exit_time,exit_price,lot_size,profit_loss,exit_reason\n";

        for (const auto& t : trades) {
            out << t.id << ","
                << t.symbol << ","
                << t.direction << ","
                << t.entry_time << ","
                << std::fixed << std::setprecision(5) << t.entry_price << ","
                << t.exit_time << ","
                << t.exit_price << ","
                << std::setprecision(2) << t.lot_size << ","
                << t.profit_loss << ","
                << t.exit_reason << "\n";
        }
    }

    /**
     * Generate JSON report (for programmatic use)
     */
    void GenerateJSON(
        const BacktestResults& results,
        const std::vector<Trade>& trades,
        const std::string& output_path
    ) {
        std::ofstream out(output_path);
        if (!out) return;

        out << "{\n";
        out << "  \"strategy\": \"" << config_.strategy_name << "\",\n";
        out << "  \"symbol\": \"" << config_.symbol << "\",\n";
        out << "  \"results\": {\n";
        out << "    \"initial_balance\": " << std::fixed << std::setprecision(2) << results.initial_balance << ",\n";
        out << "    \"final_balance\": " << results.final_balance << ",\n";
        out << "    \"profit_loss\": " << results.total_profit_loss << ",\n";
        out << "    \"return_multiple\": " << std::setprecision(4) << (results.final_balance / results.initial_balance) << ",\n";
        out << "    \"total_trades\": " << results.total_trades << ",\n";
        out << "    \"winning_trades\": " << results.winning_trades << ",\n";
        out << "    \"win_rate\": " << std::setprecision(2) << results.win_rate << ",\n";
        out << "    \"max_drawdown\": " << results.max_drawdown << ",\n";
        out << "    \"max_drawdown_pct\": " << results.max_drawdown_pct << ",\n";
        out << "    \"average_win\": " << results.average_win << ",\n";
        out << "    \"average_loss\": " << results.average_loss << ",\n";
        out << "    \"total_swap\": " << results.total_swap_charged << "\n";
        out << "  },\n";
        out << "  \"trade_count\": " << trades.size() << "\n";
        out << "}\n";
    }

private:
    ReportConfig config_;

    std::string GenerateHTMLHeader() {
        std::ostringstream ss;
        ss << R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)" << config_.title << R"(</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
            background: #f5f5f5;
            color: #333;
            line-height: 1.6;
        }
        .container { max-width: 1200px; margin: 0 auto; padding: 20px; }
        .header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 40px 20px;
            text-align: center;
            margin-bottom: 30px;
            border-radius: 10px;
        }
        .header h1 { font-size: 2.5em; margin-bottom: 10px; }
        .header .subtitle { opacity: 0.9; font-size: 1.1em; }
        .section {
            background: white;
            border-radius: 10px;
            padding: 25px;
            margin-bottom: 20px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.08);
        }
        .section h2 {
            border-bottom: 2px solid #667eea;
            padding-bottom: 10px;
            margin-bottom: 20px;
            color: #333;
        }
        .metrics-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
        }
        .metric-card {
            background: #f8f9fa;
            border-radius: 8px;
            padding: 20px;
            text-align: center;
        }
        .metric-value {
            font-size: 2em;
            font-weight: bold;
            color: #667eea;
        }
        .metric-label {
            color: #666;
            font-size: 0.9em;
            margin-top: 5px;
        }
        .positive { color: #28a745; }
        .negative { color: #dc3545; }
        .chart-container {
            position: relative;
            height: 400px;
            margin: 20px 0;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            font-size: 0.9em;
        }
        th, td {
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid #eee;
        }
        th { background: #f8f9fa; font-weight: 600; }
        tr:hover { background: #f8f9fa; }
        .footer {
            text-align: center;
            padding: 20px;
            color: #666;
            font-size: 0.9em;
        }
        @media (max-width: 768px) {
            .metrics-grid { grid-template-columns: 1fr; }
            .header h1 { font-size: 1.8em; }
        }
    </style>
</head>
<body>
<div class="container">
    <div class="header">
        <h1>)" << config_.title << R"(</h1>
        <div class="subtitle">)" << config_.strategy_name << " | " << config_.symbol;
        if (!config_.period.empty()) {
            ss << " | " << config_.period;
        }
        ss << R"(</div>
    </div>
)";
        return ss.str();
    }

    std::string GenerateSummarySection(const BacktestResults& results) {
        std::ostringstream ss;
        double return_mult = results.final_balance / results.initial_balance;
        std::string profit_class = results.total_profit_loss >= 0 ? "positive" : "negative";

        ss << R"(
    <div class="section">
        <h2>Summary</h2>
        <div class="metrics-grid">
            <div class="metric-card">
                <div class="metric-value )" << profit_class << R"(">$)" << std::fixed << std::setprecision(0) << results.total_profit_loss << R"(</div>
                <div class="metric-label">Net Profit</div>
            </div>
            <div class="metric-card">
                <div class="metric-value">)" << std::setprecision(2) << return_mult << R"(x</div>
                <div class="metric-label">Return Multiple</div>
            </div>
            <div class="metric-card">
                <div class="metric-value negative">)" << std::setprecision(1) << results.max_drawdown_pct << R"(%</div>
                <div class="metric-label">Max Drawdown</div>
            </div>
            <div class="metric-card">
                <div class="metric-value">)" << results.total_trades << R"(</div>
                <div class="metric-label">Total Trades</div>
            </div>
        </div>
    </div>
)";
        return ss.str();
    }

    std::string GenerateMetricsSection(const BacktestResults& results) {
        double profit_factor = (results.average_loss != 0)
            ? std::abs(results.average_win * results.winning_trades) /
              std::abs(results.average_loss * results.losing_trades)
            : 0;

        std::ostringstream ss;
        ss << R"(
    <div class="section">
        <h2>Performance Metrics</h2>
        <div class="metrics-grid">
            <div class="metric-card">
                <div class="metric-value">)" << std::fixed << std::setprecision(1) << results.win_rate << R"(%</div>
                <div class="metric-label">Win Rate</div>
            </div>
            <div class="metric-card">
                <div class="metric-value">)" << std::setprecision(2) << profit_factor << R"(</div>
                <div class="metric-label">Profit Factor</div>
            </div>
            <div class="metric-card">
                <div class="metric-value positive">$)" << std::setprecision(0) << results.average_win << R"(</div>
                <div class="metric-label">Avg Win</div>
            </div>
            <div class="metric-card">
                <div class="metric-value negative">$)" << results.average_loss << R"(</div>
                <div class="metric-label">Avg Loss</div>
            </div>
            <div class="metric-card">
                <div class="metric-value positive">$)" << results.largest_win << R"(</div>
                <div class="metric-label">Largest Win</div>
            </div>
            <div class="metric-card">
                <div class="metric-value negative">$)" << results.largest_loss << R"(</div>
                <div class="metric-label">Largest Loss</div>
            </div>
            <div class="metric-card">
                <div class="metric-value">)" << results.winning_trades << R"(</div>
                <div class="metric-label">Winning Trades</div>
            </div>
            <div class="metric-card">
                <div class="metric-value">)" << results.losing_trades << R"(</div>
                <div class="metric-label">Losing Trades</div>
            </div>
        </div>
    </div>
)";
        return ss.str();
    }

    std::string GenerateEquityCurveSection(const std::vector<EquityPoint>& equity_curve) {
        // Sample data points for chart (max 500 points)
        int step = std::max(1, (int)equity_curve.size() / 500);
        std::ostringstream labels, data;

        for (size_t i = 0; i < equity_curve.size(); i += step) {
            if (i > 0) { labels << ","; data << ","; }
            labels << "\"" << equity_curve[i].timestamp.substr(0, 10) << "\"";
            data << std::fixed << std::setprecision(2) << equity_curve[i].equity;
        }

        std::ostringstream ss;
        ss << R"(
    <div class="section">
        <h2>Equity Curve</h2>
        <div class="chart-container">
            <canvas id="equityChart"></canvas>
        </div>
        <script>
            new Chart(document.getElementById('equityChart').getContext('2d'), {
                type: 'line',
                data: {
                    labels: [)" << labels.str() << R"(],
                    datasets: [{
                        label: 'Equity',
                        data: [)" << data.str() << R"(],
                        borderColor: '#667eea',
                        backgroundColor: 'rgba(102, 126, 234, 0.1)',
                        fill: true,
                        tension: 0.1,
                        pointRadius: 0
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    plugins: { legend: { display: false } },
                    scales: {
                        y: { beginAtZero: false },
                        x: { display: false }
                    }
                }
            });
        </script>
    </div>
)";
        return ss.str();
    }

    std::string GenerateMonthlySection(const std::vector<Trade>& trades) {
        std::map<std::string, MonthlyStats> monthly;

        for (const auto& t : trades) {
            std::string key = t.exit_time.substr(0, 7);  // YYYY.MM
            if (monthly.find(key) == monthly.end()) {
                monthly[key] = {
                    std::stoi(t.exit_time.substr(0, 4)),
                    std::stoi(t.exit_time.substr(5, 2)),
                    0, 0, 0, 0
                };
            }
            auto& m = monthly[key];
            m.profit += t.profit_loss;
            m.trades++;
        }

        std::ostringstream ss;
        ss << R"(
    <div class="section">
        <h2>Monthly Breakdown</h2>
        <table>
            <tr>
                <th>Month</th>
                <th>Profit</th>
                <th>Trades</th>
            </tr>
)";
        for (const auto& [key, m] : monthly) {
            std::string profit_class = m.profit >= 0 ? "positive" : "negative";
            ss << "            <tr>\n";
            ss << "                <td>" << key << "</td>\n";
            ss << "                <td class=\"" << profit_class << "\">$"
               << std::fixed << std::setprecision(0) << m.profit << "</td>\n";
            ss << "                <td>" << m.trades << "</td>\n";
            ss << "            </tr>\n";
        }
        ss << R"(        </table>
    </div>
)";
        return ss.str();
    }

    std::string GenerateMonteCarloSection(const MonteCarloResult& mc) {
        std::ostringstream ss;
        ss << R"(
    <div class="section">
        <h2>Monte Carlo Analysis</h2>
        <div class="metrics-grid">
            <div class="metric-card">
                <div class="metric-value">$)" << std::fixed << std::setprecision(0) << mc.profit_median << R"(</div>
                <div class="metric-label">Median Profit</div>
            </div>
            <div class="metric-card">
                <div class="metric-value">$)" << mc.profit_5th_percentile << R"(</div>
                <div class="metric-label">5th Percentile (Worst)</div>
            </div>
            <div class="metric-card">
                <div class="metric-value">$)" << mc.profit_95th_percentile << R"(</div>
                <div class="metric-label">95th Percentile (Best)</div>
            </div>
            <div class="metric-card">
                <div class="metric-value">)" << std::setprecision(1) << mc.probability_of_loss << R"(%</div>
                <div class="metric-label">Loss Probability</div>
            </div>
        </div>
        <p style="margin-top: 20px; text-align: center;">
            <strong>Confidence:</strong> )" << mc.confidence_level
               << " (" << std::setprecision(0) << mc.confidence_score << R"(/100)
        </p>
    </div>
)";
        return ss.str();
    }

    std::string GenerateWalkForwardSection(const WalkForwardResult& wf) {
        std::ostringstream ss;
        ss << R"(
    <div class="section">
        <h2>Walk-Forward Analysis</h2>
        <div class="metrics-grid">
            <div class="metric-card">
                <div class="metric-value">$)" << std::fixed << std::setprecision(0) << wf.total_oos_profit << R"(</div>
                <div class="metric-label">OOS Profit</div>
            </div>
            <div class="metric-card">
                <div class="metric-value">)" << std::setprecision(1) << (wf.avg_efficiency_ratio * 100) << R"(%</div>
                <div class="metric-label">Avg Efficiency</div>
            </div>
            <div class="metric-card">
                <div class="metric-value">)" << (wf.param_stability * 100) << R"(%</div>
                <div class="metric-label">Param Stability</div>
            </div>
            <div class="metric-card">
                <div class="metric-value">)" << std::setprecision(0) << wf.robustness_score << R"(/100</div>
                <div class="metric-label">Robustness Score</div>
            </div>
        </div>
        <table style="margin-top: 20px;">
            <tr>
                <th>Window</th>
                <th>IS Period</th>
                <th>OOS Period</th>
                <th>IS Profit</th>
                <th>OOS Profit</th>
                <th>Efficiency</th>
            </tr>
)";
        for (size_t i = 0; i < wf.windows.size(); i++) {
            const auto& w = wf.windows[i];
            ss << "            <tr>\n";
            ss << "                <td>" << (i+1) << "</td>\n";
            ss << "                <td>" << w.is_start << " - " << w.is_end << "</td>\n";
            ss << "                <td>" << w.oos_start << " - " << w.oos_end << "</td>\n";
            ss << "                <td>$" << std::setprecision(0) << w.is_profit << "</td>\n";
            ss << "                <td>$" << w.oos_profit << "</td>\n";
            ss << "                <td>" << std::setprecision(1) << (w.efficiency_ratio * 100) << "%</td>\n";
            ss << "            </tr>\n";
        }
        ss << R"(        </table>
    </div>
)";
        return ss.str();
    }

    std::string GenerateTradeListSection(const std::vector<Trade>& trades) {
        std::ostringstream ss;
        ss << R"(
    <div class="section">
        <h2>Trade List (Last 50)</h2>
        <table>
            <tr>
                <th>ID</th>
                <th>Direction</th>
                <th>Entry</th>
                <th>Exit</th>
                <th>Lots</th>
                <th>Profit</th>
                <th>Reason</th>
            </tr>
)";
        int start = std::max(0, (int)trades.size() - 50);
        for (size_t i = start; i < trades.size(); i++) {
            const auto& t = trades[i];
            std::string profit_class = t.profit_loss >= 0 ? "positive" : "negative";
            ss << "            <tr>\n";
            ss << "                <td>" << t.id << "</td>\n";
            ss << "                <td>" << t.direction << "</td>\n";
            ss << "                <td>" << std::fixed << std::setprecision(2) << t.entry_price << "</td>\n";
            ss << "                <td>" << t.exit_price << "</td>\n";
            ss << "                <td>" << t.lot_size << "</td>\n";
            ss << "                <td class=\"" << profit_class << "\">$"
               << std::setprecision(0) << t.profit_loss << "</td>\n";
            ss << "                <td>" << t.exit_reason << "</td>\n";
            ss << "            </tr>\n";
        }
        ss << R"(        </table>
    </div>
)";
        return ss.str();
    }

    std::string GenerateHTMLFooter() {
        return R"(
    <div class="footer">
        Generated by ctrader-backtest engine | <a href="https://github.com/user/ctrader-backtest">GitHub</a>
    </div>
</div>
</body>
</html>
)";
    }
};

} // namespace backtest

#endif // REPORT_GENERATOR_H
