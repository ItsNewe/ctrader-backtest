# MT5 Validation Framework

Complete validation system for ensuring backtest engine exactly reproduces MT5 Strategy Tester behavior.

## Directory Structure

```
validation/
├── micro_tests/          # MQL5 test EAs
│   ├── test_a_sl_tp_order.mq5
│   ├── test_b_tick_synthesis.mq5
│   ├── test_c_slippage.mq5
│   ├── test_d_spread_widening.mq5
│   ├── test_e_swap_timing.mq5
│   └── test_f_margin_calc.mq5
│
├── mt5/                  # MT5 test results
│   ├── test_b_ticks.csv (19.4 MB - 191,449 ticks)
│   ├── test_c_slippage.csv
│   ├── test_d_spread.csv
│   ├── test_e_swap_timing.csv
│   ├── test_f_margin.csv
│   └── *.json summary files
│
├── analysis/             # Analysis outputs
│   ├── complete_analysis.json
│   ├── mt5_validated_config.json
│   └── test_*_analysis.json
│
├── configs/              # Test configurations
│   └── tester_ini/       # MT5 .ini files
│
├── Python Tools
├── retrieve_results.py   # Auto-retrieve MT5 results
├── analyze_all_tests.py  # Analyze all test data
├── verify_mt5_data.py    # Verify data and extract config
├── compare_backtest_results.py  # Compare engine vs MT5
│
├── C++ Tests
├── test_margin_swap.cpp  # Unit tests
├── example_integration.cpp  # Integration example
│
└── Documentation
    ├── TEST_INSTRUCTIONS.md
    └── README.md (this file)
```

## Quick Commands

### Verify All MT5 Data
```bash
python validation/verify_mt5_data.py
```
Outputs:
- `validation/analysis/mt5_validated_config.json`
- `include/mt5_validated_constants.h`

### Analyze Test Results
```bash
python validation/analyze_all_tests.py
```
Outputs:
- Statistical analysis of all tests
- Summary in `validation/analysis/complete_analysis.json`

### Retrieve MT5 Test Results
After running a test manually in MT5:
```bash
python validation/retrieve_results.py test_f
```

### Compare Backtest Results
After running same strategy in both engines:
```bash
python validation/compare_backtest_results.py
```

## Test Summary

### Test A: SL/TP Execution Order
- **Status:** ✓ VALIDATED
- **Finding:** Both execute TP first
- **Action:** No changes needed

### Test B: Tick Synthesis
- **Status:** ✓ DATA COLLECTED (19.4 MB)
- **Finding:** ~1,914 ticks per H1 bar
- **Data:** `mt5/test_b_ticks.csv`
- **Next:** Implement tick generator

### Test C: Slippage
- **Status:** ✓ ANALYZED
- **Finding:** Zero slippage in MT5 Tester
- **Data:** `mt5/test_c_slippage.csv`
- **Implementation:** Zero slippage mode

### Test D: Spread
- **Status:** ✓ ANALYZED
- **Finding:** Constant 0.71 pips
- **Data:** `mt5/test_d_spread.csv`
- **Implementation:** Constant spread model

### Test E: Swap Timing
- **Status:** ✓ VERIFIED
- **Finding:** 00:00 daily, triple Wednesday
- **Data:** `mt5/test_e_swap_timing.csv`
- **Implementation:** SwapManager class

### Test F: Margin Calculation
- **Status:** ✓ VERIFIED
- **Finding:** `(lots × 100k × price) / leverage`
- **Data:** `mt5/test_f_margin.csv`
- **Implementation:** MarginManager class

## Integration Checklist

- [x] All tests executed
- [x] All data collected
- [x] Data analyzed and verified
- [x] Constants extracted
- [x] MarginManager implemented
- [x] SwapManager implemented
- [ ] Integrate into BacktestEngine
- [ ] Run comparison backtest
- [ ] Achieve <1% difference

## Validated Constants

All constants auto-generated in `include/mt5_validated_constants.h`:

```cpp
// From Test F
LEVERAGE = 500
CONTRACT_SIZE = 100,000

// From Test E
SWAP_HOUR = 0 (midnight)
TRIPLE_SWAP_DAY = 3 (Wednesday)

// From Test C
SLIPPAGE_POINTS = 0.0 (MT5 Tester)

// From Test D
MEAN_SPREAD_POINTS = 7.08
MEAN_SPREAD_PIPS = 0.71

// From Test B
AVG_TICKS_PER_H1_BAR = 1914
```

## Files Generated

### Auto-Generated (Don't Edit)
- `include/mt5_validated_constants.h` - C++ constants
- `validation/analysis/mt5_validated_config.json` - JSON config
- `validation/analysis/complete_analysis.json` - Analysis results

### Templates (Edit For Your Tests)
- `validation/our_results.json` - Your engine results
- `validation/mt5_results.json` - MT5 reference results

## Validation Workflow

1. **Run MT5 Tests** (if not already done)
   - Follow `TEST_INSTRUCTIONS.md`
   - Use `retrieve_results.py` to collect data

2. **Verify Data**
   ```bash
   python validation/verify_mt5_data.py
   ```

3. **Integrate Classes**
   - Use MarginManager in OpenPosition()
   - Use SwapManager in main loop
   - Reference `../INTEGRATION_GUIDE.md`

4. **Run Comparison Test**
   - Same strategy in both engines
   - Save results to JSON
   - Run `compare_backtest_results.py`

5. **Iterate Until <1% Difference**

## Support

- **Main Docs:** `../VALIDATION_TESTS_COMPLETE.md`
- **Integration:** `../INTEGRATION_GUIDE.md`
- **Quick Start:** `../QUICK_START.md`
- **Index:** `../MT5_VALIDATION_INDEX.md`

## Test Data Statistics

- Total ticks analyzed: 191,449
- Margin tests: 5 lot sizes
- Slippage trades: 50
- Spread samples: 219
- Swap events: 2
- Total data size: 19.4 MB
