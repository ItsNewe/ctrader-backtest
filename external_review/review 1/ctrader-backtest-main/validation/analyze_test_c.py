"""
Analyze Test C: Slippage Distribution
Extracts slippage statistics to implement in our engine
"""

import pandas as pd
import numpy as np
from pathlib import Path
import json

def analyze_slippage():
    """Analyze slippage data from MT5"""

    print("=" * 70)
    print("TEST C: SLIPPAGE DISTRIBUTION ANALYSIS")
    print("=" * 70)
    print()

    # Load data
    data_file = Path("validation/mt5/test_c_slippage.csv")
    if not data_file.exists():
        print(f"ERROR: {data_file} not found")
        return False

    df = pd.read_csv(data_file, sep='\t')

    print(f"Loaded {len(df)} trades")
    print()

    # Basic statistics
    print("SLIPPAGE STATISTICS")
    print("-" * 70)
    print(f"Mean slippage:       {df['slippage_points'].mean():.4f} points")
    print(f"Std deviation:       {df['slippage_points'].std():.4f} points")
    print(f"Min slippage:        {df['slippage_points'].min():.4f} points")
    print(f"Max slippage:        {df['slippage_points'].max():.4f} points")
    print(f"Median slippage:     {df['slippage_points'].median():.4f} points")
    print()

    # Percentiles
    print("PERCENTILES")
    print("-" * 70)
    for p in [10, 25, 50, 75, 90, 95, 99]:
        val = np.percentile(df['slippage_points'], p)
        print(f"  {p:2d}th percentile: {val:8.4f} points")
    print()

    # Buy vs Sell analysis
    buy_trades = df[df['type'] == 'BUY']
    sell_trades = df[df['type'] == 'SELL']

    print("BUY vs SELL COMPARISON")
    print("-" * 70)
    print(f"BUY trades:  {len(buy_trades):3d}  |  Mean: {buy_trades['slippage_points'].mean():7.4f}  |  Std: {buy_trades['slippage_points'].std():7.4f}")
    print(f"SELL trades: {len(sell_trades):3d}  |  Mean: {sell_trades['slippage_points'].mean():7.4f}  |  Std: {sell_trades['slippage_points'].std():7.4f}")
    print()

    # Check if all slippage is zero
    if df['slippage_points'].abs().max() < 0.0001:
        print("⚠ WARNING: All slippage values are zero!")
        print("This means MT5 Strategy Tester executed all trades at exact requested prices.")
        print("In real trading, there would typically be some slippage.")
        print("For validation purposes, we can implement zero slippage to match MT5 Tester behavior.")
        print()

    # Distribution analysis
    print("DISTRIBUTION ANALYSIS")
    print("-" * 70)

    # Test for normal distribution
    from scipy import stats
    _, p_value = stats.normaltest(df['slippage_points'])
    print(f"Normal distribution test (p-value): {p_value:.6f}")
    if p_value > 0.05:
        print("  → Data appears normally distributed")
    else:
        print("  → Data does NOT appear normally distributed")
    print()

    # Histogram bins
    print("HISTOGRAM")
    print("-" * 70)
    hist, bins = np.histogram(df['slippage_points'], bins=10)
    for i in range(len(hist)):
        bar = '█' * int(hist[i] * 50 / hist.max())
        print(f"  {bins[i]:6.2f} to {bins[i+1]:6.2f}: {hist[i]:3d} {bar}")
    print()

    # Implementation recommendations
    print("IMPLEMENTATION RECOMMENDATIONS")
    print("=" * 70)
    print()

    mean_slip = df['slippage_points'].mean()
    std_slip = df['slippage_points'].std()

    print("For our C++ engine, implement slippage as:")
    print()
    print("```cpp")
    print("// In include/slippage_model.h")
    print("class MT5SlippageModel {")
    print("private:")
    print(f"    const double mean_ = {mean_slip:.6f};  // points")
    print(f"    const double std_dev_ = {std_slip:.6f};  // points")
    print("    std::normal_distribution<double> distribution_;")
    print()
    print("public:")
    print("    MT5SlippageModel() : distribution_(mean_, std_dev_) {}")
    print()
    print("    double GetSlippage(std::mt19937& rng) {")
    print("        return distribution_(rng);")
    print("    }")
    print()
    print("    double ApplySlippage(double requested_price, bool is_buy, ")
    print("                         double point_value, std::mt19937& rng) {")
    print("        double slippage_points = distribution_(rng);")
    print("        double slippage = slippage_points * point_value;")
    print("        ")
    print("        // Slippage works against the trader")
    print("        if (is_buy) {")
    print("            return requested_price + slippage;  // Pay more")
    print("        } else {")
    print("            return requested_price - slippage;  // Receive less")
    print("        }")
    print("    }")
    print("};")
    print("```")
    print()

    # Export summary
    summary = {
        'test': 'Test C - Slippage Distribution',
        'trades_analyzed': len(df),
        'statistics': {
            'mean_points': float(mean_slip),
            'std_dev_points': float(std_slip),
            'min_points': float(df['slippage_points'].min()),
            'max_points': float(df['slippage_points'].max()),
            'median_points': float(df['slippage_points'].median())
        },
        'distribution': {
            'is_normal': bool(p_value > 0.05),
            'normal_test_p_value': float(p_value)
        },
        'by_direction': {
            'buy': {
                'count': len(buy_trades),
                'mean_points': float(buy_trades['slippage_points'].mean()),
                'std_dev_points': float(buy_trades['slippage_points'].std())
            },
            'sell': {
                'count': len(sell_trades),
                'mean_points': float(sell_trades['slippage_points'].mean()),
                'std_dev_points': float(sell_trades['slippage_points'].std())
            }
        }
    }

    output_file = Path("validation/analysis/test_c_analysis.json")
    output_file.parent.mkdir(parents=True, exist_ok=True)

    with open(output_file, 'w') as f:
        json.dump(summary, f, indent=2)

    print(f"Analysis saved to: {output_file}")
    print()

    return True

if __name__ == "__main__":
    import sys
    success = analyze_slippage()
    sys.exit(0 if success else 1)
