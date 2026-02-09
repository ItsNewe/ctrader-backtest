#include <iostream>
#include <iomanip>
#include <cmath>

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << " FUNDS REQUIRED: 1 microlot per $1 spacing" << std::endl;
    std::cout << " Stop-out at 20% margin level when price hits X% drop" << std::endl;
    std::cout << "========================================================" << std::endl;

    // Gold parameters
    double gold_price = 5000.0;
    double gold_contract = 100.0;   // 100 oz per lot
    double gold_leverage = 500.0;
    double gold_lot = 0.01;         // 1 microlot

    // Silver parameters
    double silver_price = 100.0;
    double silver_contract = 5000.0; // 5000 oz per lot
    double silver_leverage = 500.0;
    double silver_lot = 0.01;        // 1 microlot

    double spacing = 1.0;  // $1
    double stop_out_level = 0.20;  // 20% margin level

    // Per-dollar P&L for 1 microlot
    double gold_per_dollar = gold_lot * gold_contract;   // $1.00 per $1 move
    double silver_per_dollar = silver_lot * silver_contract;  // $50.00 per $1 move

    std::cout << "\n--- INSTRUMENT SPECS ---" << std::endl;
    std::cout << "Gold:   Price=$" << gold_price << ", Contract=" << gold_contract
              << "oz, PnL per $1 move per microlot = $"
              << std::fixed << std::setprecision(2) << gold_per_dollar << std::endl;
    std::cout << "Silver: Price=$" << silver_price << ", Contract=" << silver_contract
              << "oz, PnL per $1 move per microlot = $"
              << silver_per_dollar << std::endl;

    std::cout << "\n--- GOLD (XAUUSD at $" << std::setprecision(0) << gold_price << ") ---" << std::endl;
    std::cout << std::setw(4) << "X%"
              << std::setw(8) << "Drop$"
              << std::setw(7) << "Trades"
              << std::setw(12) << "FloatLoss"
              << std::setw(10) << "Margin"
              << std::setw(12) << "Required"
              << std::setw(12) << "Req/trade" << std::endl;
    std::cout << std::string(67, '-') << std::endl;

    for (int X = 1; X <= 20; X++) {
        double drop = gold_price * X / 100.0;
        int N = (int)std::floor(drop / spacing);

        // Floating loss: sum of (drop - i*spacing) for i=0..N-1, times per_dollar
        // = per_dollar * (N*drop - spacing*N*(N-1)/2)
        // Since drop = N*spacing: = per_dollar * spacing * (N*N - N*(N-1)/2) = per_dollar * spacing * N*(N+1)/2
        double floating_loss = gold_per_dollar * spacing * (double)N * (N + 1) / 2.0;

        // Margin (CFD_LEVERAGE): lot * contract * price_i / leverage for each position
        // Sum of prices = N*gold_price - spacing*N*(N-1)/2
        double sum_prices = (double)N * gold_price - spacing * (double)N * (N - 1) / 2.0;
        double total_margin = gold_lot * gold_contract / gold_leverage * sum_prices;

        double required = floating_loss + stop_out_level * total_margin;

        std::cout << std::setw(3) << X << "%"
                  << std::setw(7) << std::setprecision(0) << drop
                  << std::setw(7) << N
                  << std::setw(10) << std::setprecision(0) << floating_loss << "$"
                  << std::setw(8) << std::setprecision(0) << total_margin << "$"
                  << std::setw(10) << std::setprecision(0) << required << "$"
                  << std::setw(10) << std::setprecision(0) << (N > 0 ? required / N : 0) << "$"
                  << std::endl;
    }

    std::cout << "\n--- SILVER (XAGUSD at $" << std::setprecision(0) << silver_price << ") ---" << std::endl;
    std::cout << std::setw(4) << "X%"
              << std::setw(8) << "Drop$"
              << std::setw(7) << "Trades"
              << std::setw(12) << "FloatLoss"
              << std::setw(10) << "Margin"
              << std::setw(12) << "Required"
              << std::setw(12) << "Req/trade" << std::endl;
    std::cout << std::string(67, '-') << std::endl;

    for (int X = 1; X <= 20; X++) {
        double drop = silver_price * X / 100.0;
        int N = (int)std::floor(drop / spacing);

        double floating_loss = silver_per_dollar * spacing * (double)N * (N + 1) / 2.0;

        double sum_prices = (double)N * silver_price - spacing * (double)N * (N - 1) / 2.0;
        double total_margin = silver_lot * silver_contract / silver_leverage * sum_prices;

        double required = floating_loss + stop_out_level * total_margin;

        std::cout << std::setw(3) << X << "%"
                  << std::setw(7) << std::setprecision(0) << drop
                  << std::setw(7) << N
                  << std::setw(10) << std::setprecision(0) << floating_loss << "$"
                  << std::setw(8) << std::setprecision(0) << total_margin << "$"
                  << std::setw(10) << std::setprecision(0) << required << "$"
                  << std::setw(10) << std::setprecision(0) << (N > 0 ? required / N : 0) << "$"
                  << std::endl;
    }

    // Side-by-side comparison
    std::cout << "\n\n--- SIDE-BY-SIDE COMPARISON ---" << std::endl;
    std::cout << std::setw(4) << "X%"
              << std::setw(14) << "Gold Req$"
              << std::setw(14) << "Silver Req$"
              << std::setw(10) << "Ratio"
              << std::setw(14) << "Gold Trades"
              << std::setw(14) << "Silver Trd" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    for (int X = 1; X <= 20; X++) {
        // Gold
        double g_drop = gold_price * X / 100.0;
        int g_N = (int)std::floor(g_drop / spacing);
        double g_fl = gold_per_dollar * spacing * (double)g_N * (g_N + 1) / 2.0;
        double g_sp = (double)g_N * gold_price - spacing * (double)g_N * (g_N - 1) / 2.0;
        double g_margin = gold_lot * gold_contract / gold_leverage * g_sp;
        double g_req = g_fl + stop_out_level * g_margin;

        // Silver
        double s_drop = silver_price * X / 100.0;
        int s_N = (int)std::floor(s_drop / spacing);
        double s_fl = silver_per_dollar * spacing * (double)s_N * (s_N + 1) / 2.0;
        double s_sp = (double)s_N * silver_price - spacing * (double)s_N * (s_N - 1) / 2.0;
        double s_margin = silver_lot * silver_contract / silver_leverage * s_sp;
        double s_req = s_fl + stop_out_level * s_margin;

        double ratio = (s_req > 0) ? g_req / s_req : 0;

        std::cout << std::setw(3) << X << "%"
                  << std::setw(12) << std::setprecision(0) << g_req << "$"
                  << std::setw(12) << std::setprecision(0) << s_req << "$"
                  << std::setw(9) << std::setprecision(1) << ratio << "x"
                  << std::setw(10) << g_N
                  << std::setw(12) << s_N
                  << std::endl;
    }

    // Key insight: what's equivalent for both?
    std::cout << "\n--- KEY INSIGHT ---" << std::endl;
    std::cout << "$1 spacing on Gold ($5000) = " << std::setprecision(3) << (1.0/5000*100) << "% of price" << std::endl;
    std::cout << "$1 spacing on Silver ($100) = " << (1.0/100*100) << "% of price" << std::endl;
    std::cout << "\nFor EQUIVALENT relative spacing:" << std::endl;
    std::cout << "  Gold $1 spacing ≈ Silver $0.02 spacing (both = 0.02% of price)" << std::endl;
    std::cout << "  Gold $50 spacing ≈ Silver $1 spacing (both = 1% of price)" << std::endl;

    std::cout << "\n--- PRACTICAL: SAME $10,000 BUDGET ---" << std::endl;
    std::cout << "With $10,000 capital, what X% can you survive?" << std::endl;
    double budget = 10000.0;

    // Gold
    for (int X = 1; X <= 20; X++) {
        double drop = gold_price * X / 100.0;
        int N = (int)std::floor(drop / spacing);
        double fl = gold_per_dollar * spacing * (double)N * (N + 1) / 2.0;
        double sp = (double)N * gold_price - spacing * (double)N * (N - 1) / 2.0;
        double margin = gold_lot * gold_contract / gold_leverage * sp;
        double req = fl + stop_out_level * margin;
        if (req > budget) {
            std::cout << "  Gold:   survive up to " << (X-1) << "% ($"
                      << std::setprecision(0) << (gold_price*(X-1)/100.0)
                      << " drop, " << (int)std::floor(gold_price*(X-1)/100.0) << " trades)" << std::endl;
            break;
        }
    }

    // Silver
    for (int X = 1; X <= 20; X++) {
        double drop = silver_price * X / 100.0;
        int N = (int)std::floor(drop / spacing);
        double fl = silver_per_dollar * spacing * (double)N * (N + 1) / 2.0;
        double sp = (double)N * silver_price - spacing * (double)N * (N - 1) / 2.0;
        double margin = silver_lot * silver_contract / silver_leverage * sp;
        double req = fl + stop_out_level * margin;
        if (req > budget) {
            std::cout << "  Silver: survive up to " << (X-1) << "% ($"
                      << std::setprecision(0) << (silver_price*(X-1)/100.0)
                      << " drop, " << (X-1) << " trades)" << std::endl;
            break;
        }
    }

    std::cout << "\nDone." << std::endl;
    return 0;
}
