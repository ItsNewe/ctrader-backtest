#pragma once
#include <cstddef>

namespace simd {

// Initialization (no-op — Highway auto-detects at first use)
void init();

// Reductions
double sum(const double* data, size_t n);
double max_value(const double* data, size_t n);
double min_value(const double* data, size_t n);
double max_drawdown(const double* equity, size_t n);

// P&L batch calculation
void calculate_pnl_batch(const double* entry_prices, const double* lot_sizes,
    double current_price, double contract_size,
    double* pnl_output, size_t n, bool is_buy);

// SL/TP batch checking — returns number of hits
int check_sl_tp_buy(const double* stop_losses, const double* take_profits,
    double bid_price, size_t n, int* hit_indices, int* hit_types);
int check_sl_tp_sell(const double* stop_losses, const double* take_profits,
    double ask_price, size_t n, int* hit_indices, int* hit_types);

// Margin calculations — unified (Highway dispatches internally)
double total_margin_batch(const double* lot_sizes, const double* prices,
    size_t n, double contract_size, double leverage);
double total_margin_cfd_leverage(const double* lot_sizes, const double* prices,
    size_t n, double contract_size, double leverage, double margin_rate);
double total_margin_cfd(const double* lot_sizes, const double* prices,
    size_t n, double contract_size, double margin_rate);
double total_margin_forex(const double* lot_sizes, size_t n,
    double contract_size, double leverage, double margin_rate);
double total_margin_futures(const double* lot_sizes, size_t n,
    double margin_initial);
double total_margin_forex_nolev(const double* lot_sizes, size_t n,
    double contract_size, double margin_rate);

// Diagnostics
void print_cpu_features();

}  // namespace simd
