#!/usr/bin/env python3
"""Test parallel sweep execution"""

import sys
from backtest_sweep import ParameterGenerator, SweepExecutor

def test_parallel_sweep():
    """Test running a small parallel sweep"""
    print("Testing parallel sweep execution (10 combinations)...")
    
    executor = SweepExecutor(
        backtest_exe=r'build\bin\backtest.exe',
        data_file=r'data\EURUSD_2023.csv',
        max_workers=2
    )
    
    # Generate 10 parameter combinations
    gen = ParameterGenerator.grid_search(
        survive_range=(0.5, 1.5, 0.5),
        size_range=(0.5, 1.0, 0.5),
        spacing_range=(0.5, 1.0, 0.5)
    )
    
    # Convert to list (to count them)
    params_list = list(gen)
    print(f"Generated {len(params_list)} combinations for testing")
    
    # Create new generator for actual sweep
    gen = ParameterGenerator.grid_search(
        survive_range=(0.5, 1.5, 0.5),
        size_range=(0.5, 1.0, 0.5),
        spacing_range=(0.5, 1.0, 0.5)
    )
    
    def progress_callback(completed, total, result):
        profit = result.metrics.get('profit_loss', 0)
        print(f"  [{completed}/{total}] {result.parameters} -> Profit: {profit:.2f}")
    
    results = executor.execute_sweep(gen, progress_callback=progress_callback)
    
    print(f"\nSweep Results:")
    print(f"  Total combinations: {results['total_combinations']}")
    print(f"  Completed: {results['completed']}")
    print(f"  Best composite score: {results['statistics']['best_result'].get('composite_score', 'N/A')}")
    print(f"  Best parameters: {results['statistics']['best_result']['parameters']}")
    
    return True

if __name__ == '__main__':
    try:
        test_parallel_sweep()
        print("\n[OK] Parallel sweep test passed!")
    except Exception as e:
        print(f"\n[FAIL] Test failed: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
