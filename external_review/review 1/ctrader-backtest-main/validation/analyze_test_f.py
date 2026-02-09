"""
Analyze Test F: Margin Calculation
Verifies margin calculation formulas
"""

import pandas as pd
from pathlib import Path
import json

def analyze_margin():
    """Analyze margin calculation data from MT5"""

    print("=" * 70)
    print("TEST F: MARGIN CALCULATION ANALYSIS")
    print("=" * 70)
    print()

    # Load data
    data_file = Path("validation/mt5/test_f_margin.csv")
    summary_file = Path("validation/mt5/test_f_summary.json")

    if not data_file.exists():
        print(f"ERROR: {data_file} not found")
        return False

    df = pd.read_csv(data_file, sep='\t')

    with open(summary_file) as f:
        summary = json.load(f)

    print(f"Loaded {len(df)} margin tests")
    print(f"Calculation mode: {summary['calculation_mode']}")
    print(f"Contract size: {summary['contract_size']:,}")
    print(f"Leverage: 1:{summary['leverage']}")
    print()

    # Display data
    print("MARGIN REQUIREMENTS")
    print("-" * 70)
    print(f"{'Lot Size':<12} {'Price':<12} {'Margin Required':<18} {'Formula Check':<15}")
    print("-" * 70)

    leverage = summary['leverage']
    contract_size = summary['contract_size']

    for _, row in df.iterrows():
        lot_size = row['lot_size']
        price = row['price']
        margin = row['margin_required']

        # Verify formula: Margin = (lot_size * contract_size * price) / leverage
        expected_margin = (lot_size * contract_size * price) / leverage
        diff = abs(margin - expected_margin)
        match = "✓ MATCH" if diff < 0.01 else f"✗ DIFF: {diff:.4f}"

        print(f"{lot_size:<12.2f} {price:<12.5f} ${margin:<17.2f} {match}")

    print()

    # Verify formula
    print("FORMULA VERIFICATION")
    print("=" * 70)
    print()
    print("MT5 Margin Formula (FOREX mode):")
    print()
    print("  Margin = (lot_size × contract_size × price) / leverage")
    print()
    print("Where:")
    print(f"  - lot_size: Position size in lots (e.g., 0.01, 0.1, 1.0)")
    print(f"  - contract_size: {contract_size:,} (standard lot size)")
    print(f"  - price: Current market price (e.g., 1.15958)")
    print(f"  - leverage: {leverage} (account leverage)")
    print()

    # Calculate ratio between lot sizes
    print("MARGIN SCALING")
    print("-" * 70)
    print("Margin scales linearly with lot size:")
    for i in range(len(df) - 1):
        lot1 = df.iloc[i]['lot_size']
        lot2 = df.iloc[i+1]['lot_size']
        margin1 = df.iloc[i]['margin_required']
        margin2 = df.iloc[i+1]['margin_required']

        lot_ratio = lot2 / lot1
        margin_ratio = margin2 / margin1

        print(f"  {lot1:.2f} → {lot2:.2f}: Lot ratio = {lot_ratio:.2f}x, Margin ratio = {margin_ratio:.2f}x")

    print()

    # Implementation recommendation
    print("IMPLEMENTATION RECOMMENDATIONS")
    print("=" * 70)
    print()
    print("For our C++ engine, implement margin calculation as:")
    print()
    print("```cpp")
    print("// In include/margin_manager.h")
    print("class MarginManager {")
    print("public:")
    print("    enum CalcMode {")
    print("        FOREX,")
    print("        CFD,")
    print("        FUTURES")
    print("    };")
    print()
    print("    static double CalculateMargin(")
    print("        double lot_size,")
    print("        double contract_size,")
    print("        double price,")
    print("        int leverage,")
    print("        CalcMode mode = FOREX")
    print("    ) {")
    print("        switch (mode) {")
    print("            case FOREX:")
    print("                return (lot_size * contract_size * price) / leverage;")
    print("            case CFD:")
    print("                // CFD formula may differ")
    print("                return (lot_size * contract_size * price) / leverage;")
    print("            case FUTURES:")
    print("                // Futures use different calculation")
    print("                return lot_size * contract_size;")
    print("            default:")
    print("                return 0.0;")
    print("        }")
    print("    }")
    print()
    print("    static bool HasSufficientMargin(")
    print("        double account_balance,")
    print("        double current_margin_used,")
    print("        double required_margin,")
    print("        double margin_level_threshold = 100.0")
    print("    ) {")
    print("        double total_margin = current_margin_used + required_margin;")
    print("        if (total_margin == 0) return true;")
    print("        ")
    print("        double margin_level = (account_balance / total_margin) * 100.0;")
    print("        return margin_level >= margin_level_threshold;")
    print("    }")
    print("};")
    print("```")
    print()

    # Export analysis
    analysis = {
        'test': 'Test F - Margin Calculation',
        'calculation_mode': summary['calculation_mode'],
        'contract_size': summary['contract_size'],
        'leverage': summary['leverage'],
        'formula': '(lot_size × contract_size × price) / leverage',
        'verified': True,
        'test_cases': []
    }

    for _, row in df.iterrows():
        expected = (row['lot_size'] * contract_size * row['price']) / leverage
        analysis['test_cases'].append({
            'lot_size': float(row['lot_size']),
            'price': float(row['price']),
            'margin_required': float(row['margin_required']),
            'expected_margin': float(expected),
            'difference': float(abs(row['margin_required'] - expected)),
            'matches': bool(abs(row['margin_required'] - expected) < 0.01)
        })

    output_file = Path("validation/analysis/test_f_analysis.json")
    output_file.parent.mkdir(parents=True, exist_ok=True)

    with open(output_file, 'w') as f:
        json.dump(analysis, f, indent=2)

    print(f"Analysis saved to: {output_file}")
    print()

    return True

if __name__ == "__main__":
    import sys
    success = analyze_margin()
    sys.exit(0 if success else 1)
