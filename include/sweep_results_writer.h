#ifndef SWEEP_RESULTS_WRITER_H
#define SWEEP_RESULTS_WRITER_H

#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace backtest {

/**
 * Incremental Results Writer for Parameter Sweeps
 *
 * Writes results to file as they complete, so:
 * - Progress is preserved if process crashes
 * - Can resume from last checkpoint
 * - Real-time monitoring of sweep progress
 */

struct SweepResultRow {
    std::string config_name;
    std::vector<double> params;
    double final_balance;
    double return_multiple;
    double max_dd_pct;
    int total_trades;
    int winning_trades;
    double win_rate;
    double sharpe_ratio;
    long execution_time_ms;

    // Strategy-specific stats (optional)
    int peak_positions = 0;
    long velocity_blocks = 0;
    long lot_zero_blocks = 0;
};

class SweepResultsWriter {
public:
    explicit SweepResultsWriter(const std::string& output_path, const std::vector<std::string>& param_names = {})
        : output_path_(output_path), param_names_(param_names), rows_written_(0) {

        // Create/overwrite output file with header
        std::ofstream out(output_path_);
        if (!out) {
            throw std::runtime_error("Cannot create output file: " + output_path_);
        }

        // Write CSV header
        out << "config_name";
        for (const auto& name : param_names_) {
            out << "," << name;
        }
        out << ",final_balance,return_multiple,max_dd_pct,total_trades,winning_trades,win_rate,sharpe_ratio,execution_time_ms";
        out << ",peak_positions,velocity_blocks,lot_zero_blocks";
        out << "\n";
        out.close();

        start_time_ = std::chrono::steady_clock::now();
    }

    /**
     * Write a single result row (thread-safe)
     */
    void WriteResult(const SweepResultRow& row) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::ofstream out(output_path_, std::ios::app);
        if (!out) {
            std::cerr << "Warning: Failed to write result to " << output_path_ << std::endl;
            return;
        }

        out << std::fixed << std::setprecision(6);
        out << row.config_name;
        for (double p : row.params) {
            out << "," << p;
        }
        out << "," << std::setprecision(2) << row.final_balance
            << "," << row.return_multiple
            << "," << row.max_dd_pct
            << "," << row.total_trades
            << "," << row.winning_trades
            << "," << row.win_rate
            << "," << std::setprecision(4) << row.sharpe_ratio
            << "," << row.execution_time_ms
            << "," << row.peak_positions
            << "," << row.velocity_blocks
            << "," << row.lot_zero_blocks
            << "\n";

        out.close();
        rows_written_++;
    }

    /**
     * Write multiple results at once (for batching)
     */
    void WriteResults(const std::vector<SweepResultRow>& rows) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::ofstream out(output_path_, std::ios::app);
        if (!out) {
            std::cerr << "Warning: Failed to write results to " << output_path_ << std::endl;
            return;
        }

        out << std::fixed;
        for (const auto& row : rows) {
            out << std::setprecision(6);
            out << row.config_name;
            for (double p : row.params) {
                out << "," << p;
            }
            out << "," << std::setprecision(2) << row.final_balance
                << "," << row.return_multiple
                << "," << row.max_dd_pct
                << "," << row.total_trades
                << "," << row.winning_trades
                << "," << row.win_rate
                << "," << std::setprecision(4) << row.sharpe_ratio
                << "," << row.execution_time_ms
                << "," << row.peak_positions
                << "," << row.velocity_blocks
                << "," << row.lot_zero_blocks
                << "\n";
            rows_written_++;
        }

        out.close();
    }

    size_t GetRowsWritten() const {
        return rows_written_;
    }

    std::string GetOutputPath() const {
        return output_path_;
    }

    double GetElapsedSeconds() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - start_time_).count();
    }

private:
    std::string output_path_;
    std::vector<std::string> param_names_;
    std::mutex mutex_;
    size_t rows_written_;
    std::chrono::steady_clock::time_point start_time_;
};

/**
 * Resume-capable sweep state
 *
 * Tracks which parameter combinations have been completed,
 * allowing sweep to resume from last checkpoint after crash.
 */
class SweepCheckpoint {
public:
    explicit SweepCheckpoint(const std::string& checkpoint_path)
        : checkpoint_path_(checkpoint_path) {
        LoadCheckpoint();
    }

    /**
     * Check if a configuration has been completed
     */
    bool IsCompleted(const std::string& config_name) const {
        return completed_.find(config_name) != completed_.end();
    }

    /**
     * Mark a configuration as completed
     */
    void MarkCompleted(const std::string& config_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        completed_.insert(config_name);

        // Append to checkpoint file
        std::ofstream out(checkpoint_path_, std::ios::app);
        if (out) {
            out << config_name << "\n";
        }
    }

    /**
     * Get count of completed configurations
     */
    size_t GetCompletedCount() const {
        return completed_.size();
    }

    /**
     * Clear checkpoint (start fresh)
     */
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        completed_.clear();
        std::ofstream out(checkpoint_path_, std::ios::trunc);
    }

private:
    std::string checkpoint_path_;
    std::set<std::string> completed_;
    std::mutex mutex_;

    void LoadCheckpoint() {
        std::ifstream in(checkpoint_path_);
        if (!in) return;

        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) {
                completed_.insert(line);
            }
        }
    }
};

/**
 * Helper: Calculate Sharpe ratio from trade results
 */
inline double CalculateSharpeRatio(const std::vector<double>& daily_returns, double risk_free_rate = 0.0) {
    if (daily_returns.size() < 2) return 0.0;

    double sum = 0.0;
    for (double r : daily_returns) {
        sum += r;
    }
    double mean = sum / daily_returns.size();

    double sum_sq = 0.0;
    for (double r : daily_returns) {
        sum_sq += (r - mean) * (r - mean);
    }
    double stddev = std::sqrt(sum_sq / (daily_returns.size() - 1));

    if (stddev < 1e-9) return 0.0;

    // Annualized Sharpe (assuming 252 trading days)
    return (mean - risk_free_rate) / stddev * std::sqrt(252.0);
}

} // namespace backtest

#endif // SWEEP_RESULTS_WRITER_H
