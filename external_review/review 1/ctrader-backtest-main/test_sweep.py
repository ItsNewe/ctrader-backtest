#!/usr/bin/env python3
"""Test parameter sweep system"""

import sys
from pathlib import Path
from backtest_sweep import ParameterGenerator, SweepExecutor

def test_parameter_generation():
    """Test that parameter generator works"""
    print("Testing parameter generation...")
    
    gen = ParameterGenerator.grid_search(
        survive_range=(0.5, 1.5, 0.5),
        size_range=(0.5, 1.5, 0.5),
        spacing_range=(0.5, 1.5, 0.5)
    )
    
    params_list = list(gen)
    print(f"[OK] Generated {len(params_list)} parameter combinations")
    print(f"  First 3:")
    for p in params_list[:3]:
        print(f"    {p}")
    
    return True

def test_executor_init():
    """Test that executor initializes"""
    print("\nTesting executor initialization...")
    
    executor = SweepExecutor(
        backtest_exe=r'build\bin\backtest.exe',
        data_file=r'data\EURUSD_2023.csv',
        max_workers=2
    )
    
    print(f"[OK] Executor initialized")
    print(f"  Executable: {executor.backtest_exe}")
    print(f"  Data file: {executor.data_file}")
    print(f"  Max workers: {executor.max_workers}")
    print(f"  Database: {executor.results_db}")
    
    return True

def test_single_backtest():
    """Test running a single backtest"""
    print("\nTesting single backtest execution...")
    
    executor = SweepExecutor(
        backtest_exe=r'build\bin\backtest.exe',
        data_file=r'data\EURUSD_2023.csv',
        max_workers=1
    )
    
    params = {'survive': 1.0, 'size': 1.0, 'spacing': 1.0}
    
    try:
        result = executor._run_backtest(params)
        print(f"[OK] Backtest completed")
        print(f"  Parameters: {result.parameters}")
        print(f"  Metrics: {result.metrics}")
        print(f"  Profit/Loss: {result.metrics.get('profit_loss', 'N/A')}")
        return True
    except Exception as e:
        print(f"[FAIL] Backtest failed: {e}")
        return False

if __name__ == '__main__':
    try:
        test_parameter_generation()
        test_executor_init()
        test_single_backtest()
        print("\n[OK] All tests passed!")
    except Exception as e:
        print(f"\n[FAIL] Test failed: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
