#!/usr/bin/env python3
"""
Command-line interface for parameter sweeps
Allows running sweeps from CLI or AI agents
"""

import argparse
import json
import sys
from pathlib import Path
from backtest_sweep import SweepExecutor, ParameterGenerator

def main():
    parser = argparse.ArgumentParser(description='Run parameter sweeps for backtesting')
    
    parser.add_argument('--type', choices=['grid', 'random'], default='grid',
                       help='Sweep type: grid search or random search')
    
    parser.add_argument('--survive', type=str, default='0.5,5.0,0.5',
                       help='Survive parameter range (grid: min,max,step or random: min,max)')
    
    parser.add_argument('--size', type=str, default='0.1,10.0,0.5',
                       help='Size parameter range')
    
    parser.add_argument('--spacing', type=str, default='0.5,10.0,0.5',
                       help='Spacing parameter range')
    
    parser.add_argument('--num-combinations', type=int, default=100,
                       help='Number of combinations for random search')
    
    parser.add_argument('--data-file', type=str, default=r'data\EURUSD_2023.csv',
                       help='Path to OHLC data CSV')
    
    parser.add_argument('--backtest-exe', type=str, default=r'build\bin\backtest.exe',
                       help='Path to backtest executable')
    
    parser.add_argument('--workers', type=int, default=4,
                       help='Number of parallel workers')
    
    parser.add_argument('--output', type=str, default=None,
                       help='Output file for results (JSON)')
    
    parser.add_argument('--verbose', action='store_true',
                       help='Verbose output')
    
    args = parser.parse_args()
    
    # Parse ranges
    survive_parts = [float(x) for x in args.survive.split(',')]
    size_parts = [float(x) for x in args.size.split(',')]
    spacing_parts = [float(x) for x in args.spacing.split(',')]
    
    survive_range = tuple(survive_parts)
    size_range = tuple(size_parts)
    spacing_range = tuple(spacing_parts)
    
    # Generate parameters
    if args.type == 'grid':
        param_gen = ParameterGenerator.grid_search(survive_range, size_range, spacing_range)
    else:
        param_gen = ParameterGenerator.random_search(
            args.num_combinations,
            (survive_range[0], survive_range[1]),
            (size_range[0], size_range[1]),
            (spacing_range[0], spacing_range[1])
        )
    
    # Create executor
    executor = SweepExecutor(
        backtest_exe=args.backtest_exe,
        data_file=args.data_file,
        max_workers=args.workers
    )
    
    print(f"Starting parameter sweep (type={args.type}, data={args.data_file})")
    
    # Progress callback
    def progress(completed, total, result):
        if args.verbose:
            profit = result.metrics.get('profit_loss', 0)
            params = result.parameters
            print(f"  [{completed}/{total}] survive={params['survive']}, size={params['size']}, spacing={params['spacing']} → Profit: {profit:.2f}")
    
    # Run sweep
    results = executor.execute_sweep(param_gen, progress_callback=progress)
    
    # Display results
    print(f"\nSweep completed: {results['completed']}/{results['total_combinations']}")
    print(f"\nBest Strategy:")
    best = results['statistics']['best_result']
    print(json.dumps(best, indent=2))
    
    # Save to file if requested
    if args.output:
        with open(args.output, 'w') as f:
            json.dump(results, f, indent=2)
        print(f"\nResults saved to: {args.output}")
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
