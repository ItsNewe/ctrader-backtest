#!/usr/bin/env python3
"""
AI Strategy Agent
Automatically tests strategies, analyzes results, and generates improved parameters
Uses the backtest sweep system to evaluate parameter combinations
"""

import json
import requests
from typing import Dict, List, Tuple
from dataclasses import dataclass
from backtest_sweep import SweepExecutor, ParameterGenerator
from metrics_calculator import MetricsCalculator

@dataclass
class StrategyIteration:
    """Tracks a single iteration of strategy optimization"""
    iteration: int
    strategy_name: str
    parameter_ranges: Dict
    best_parameters: Dict
    best_metrics: Dict
    profit: float
    sharpe_ratio: float
    max_drawdown: float
    
    def __repr__(self):
        return (f"Iteration {self.iteration}: "
                f"Profit={self.profit:.2f}, "
                f"Sharpe={self.sharpe_ratio:.2f}, "
                f"MaxDD={self.max_drawdown:.2f}%")

class AIStrategyAgent:
    """
    AI Agent for autonomous strategy optimization
    Uses backtest results to reason about parameter changes and test improvements
    """
    
    def __init__(self, backtest_exe: str = r'build\bin\backtest.exe',
                 data_file: str = r'data\EURUSD_2023.csv',
                 max_workers: int = 4,
                 api_base_url: str = 'http://localhost:5000'):
        """
        Initialize AI agent
        
        Args:
            backtest_exe: Path to backtest executable
            data_file: Path to OHLC data
            max_workers: Number of parallel workers
            api_base_url: Base URL for Flask API
        """
        self.executor = SweepExecutor(backtest_exe, data_file, max_workers)
        self.api_base_url = api_base_url
        self.iterations = []
        self.best_strategy = None
        
        # Initial parameter exploration space
        self.initial_ranges = {
            'survive': (0.5, 5.0),
            'size': (0.1, 10.0),
            'spacing': (0.5, 10.0)
        }
    
    def run_optimization_loop(self, iterations: int = 3, exploration_ratio: float = 0.7):
        """
        Run the strategy optimization loop
        
        Args:
            iterations: Number of optimization iterations
            exploration_ratio: Ratio of grid search (0.7 = 70% grid, 30% random)
        
        Returns:
            List of StrategyIteration objects
        """
        print("=" * 80)
        print("AI STRATEGY OPTIMIZATION LOOP")
        print("=" * 80)
        
        current_ranges = self.initial_ranges.copy()
        
        for i in range(iterations):
            print(f"\n--- Iteration {i+1}/{iterations} ---")
            
            # Generate parameter combinations
            if i == 0 or exploration_ratio > 0.5:
                # Wide exploration for first iteration or exploration-focused
                param_gen = ParameterGenerator.grid_search(
                    current_ranges['survive'],
                    current_ranges['size'],
                    current_ranges['spacing']
                )
                search_type = "grid"
            else:
                # Random search for exploitation
                param_gen = ParameterGenerator.random_search(
                    200,  # 200 random combinations
                    (current_ranges['survive'][0], current_ranges['survive'][1]),
                    (current_ranges['size'][0], current_ranges['size'][1]),
                    (current_ranges['spacing'][0], current_ranges['spacing'][1])
                )
                search_type = "random"
            
            print(f"Search type: {search_type}")
            
            # Run sweep
            results = self.executor.execute_sweep(param_gen)
            
            # Extract best result
            best = results['statistics']['best_result']
            iteration_record = StrategyIteration(
                iteration=i+1,
                strategy_name=f"fill_up_v{i+1}",
                parameter_ranges=current_ranges,
                best_parameters=best['parameters'],
                best_metrics=best['metrics'],
                profit=best['metrics'].get('profit_loss', 0),
                sharpe_ratio=best['metrics'].get('sharpe_ratio', 0),
                max_drawdown=best['metrics'].get('max_drawdown_pct', 0)
            )
            
            self.iterations.append(iteration_record)
            print(f"✓ {iteration_record}")
            
            # Analyze results and generate new parameter ranges
            current_ranges = self._analyze_and_adapt(results, i)
            self.best_strategy = best
        
        print("\n" + "=" * 80)
        print("OPTIMIZATION COMPLETE")
        print("=" * 80)
        self._print_summary()
        
        return self.iterations
    
    def _analyze_and_adapt(self, results: Dict, iteration: int) -> Dict:
        """
        Analyze sweep results and adapt parameter ranges for next iteration
        
        Strategy:
        1. Identify which parameters most affect profitability
        2. Tighten ranges around best performers
        3. Adjust exploration/exploitation balance
        
        Args:
            results: Sweep results from executor
            iteration: Current iteration number
            
        Returns:
            Updated parameter ranges
        """
        all_results = results['results']  # List of all backtest results
        
        # Sort by profitability
        sorted_results = sorted(
            all_results,
            key=lambda x: x['metrics'].get('profit_loss', 0),
            reverse=True
        )
        
        # Analyze top 20% of results
        top_n = max(1, len(sorted_results) // 5)
        top_results = sorted_results[:top_n]
        
        # Calculate parameter ranges for top performers
        survive_values = [r['parameters']['survive'] for r in top_results]
        size_values = [r['parameters']['size'] for r in top_results]
        spacing_values = [r['parameters']['spacing'] for r in top_results]
        
        survive_min, survive_max = min(survive_values), max(survive_values)
        size_min, size_max = min(size_values), max(size_values)
        spacing_min, spacing_max = min(spacing_values), max(spacing_values)
        
        # Expand ranges slightly for exploration
        margin = 0.15
        
        survive_range = (
            max(0.5, survive_min - margin),
            min(5.0, survive_max + margin)
        )
        size_range = (
            max(0.1, size_min - margin),
            min(10.0, size_max + margin)
        )
        spacing_range = (
            max(0.5, spacing_min - margin),
            min(10.0, spacing_max + margin)
        )
        
        new_ranges = {
            'survive': survive_range,
            'size': size_range,
            'spacing': spacing_range
        }
        
        print(f"\nParameter Adaptation:")
        print(f"  survive: {survive_range[0]:.2f} - {survive_range[1]:.2f}")
        print(f"  size: {size_range[0]:.2f} - {size_range[1]:.2f}")
        print(f"  spacing: {spacing_range[0]:.2f} - {spacing_range[1]:.2f}")
        
        return new_ranges
    
    def _print_summary(self):
        """Print optimization summary"""
        print("\nOptimization History:")
        print("-" * 80)
        for it in self.iterations:
            print(f"{it}")
        
        if self.iterations:
            print(f"\nBest Overall: Iteration {self.best_strategy['iteration']}")
            print(f"Parameters: {json.dumps(self.best_strategy['parameters'], indent=2)}")
            print(f"Metrics: {json.dumps(self.best_strategy['metrics'], indent=2)}")
    
    def submit_to_api(self, sweep_config: Dict) -> Dict:
        """
        Submit a sweep to the Flask API
        
        Args:
            sweep_config: Configuration dict with sweep parameters
            
        Returns:
            API response with sweep_id
        """
        try:
            url = f"{self.api_base_url}/api/sweep/start"
            response = requests.post(url, json=sweep_config)
            return response.json()
        except Exception as e:
            print(f"Error submitting to API: {e}")
            return None
    
    def get_sweep_results(self, sweep_id: str) -> Dict:
        """
        Retrieve sweep results from API
        
        Args:
            sweep_id: ID of sweep to retrieve
            
        Returns:
            Sweep results with all combinations and rankings
        """
        try:
            url = f"{self.api_base_url}/api/sweep/{sweep_id}"
            response = requests.get(url)
            return response.json()
        except Exception as e:
            print(f"Error retrieving sweep: {e}")
            return None
    
    def get_best_strategy(self, sweep_id: str) -> Dict:
        """Get best performing strategy from a sweep"""
        try:
            url = f"{self.api_base_url}/api/sweep/{sweep_id}/best"
            response = requests.get(url)
            return response.json()
        except Exception as e:
            print(f"Error getting best strategy: {e}")
            return None
    
    def save_optimization_report(self, filepath: str):
        """
        Save optimization report to JSON file
        
        Args:
            filepath: Where to save the report
        """
        report = {
            'iterations': [
                {
                    'iteration': it.iteration,
                    'strategy_name': it.strategy_name,
                    'best_parameters': it.best_parameters,
                    'best_metrics': it.best_metrics,
                    'profit': it.profit,
                    'sharpe_ratio': it.sharpe_ratio,
                    'max_drawdown': it.max_drawdown
                }
                for it in self.iterations
            ],
            'best_strategy': {
                'parameters': self.best_strategy['parameters'],
                'metrics': self.best_strategy['metrics']
            } if self.best_strategy else None
        }
        
        with open(filepath, 'w') as f:
            json.dump(report, f, indent=2)
        
        print(f"\nReport saved to: {filepath}")


def main():
    """Example usage"""
    import argparse
    
    parser = argparse.ArgumentParser(description='AI Strategy Optimization Agent')
    parser.add_argument('--iterations', type=int, default=3, help='Number of optimization iterations')
    parser.add_argument('--report', type=str, default='ai_optimization_report.json', help='Output report file')
    parser.add_argument('--workers', type=int, default=4, help='Number of parallel workers')
    
    args = parser.parse_args()
    
    # Create and run agent
    agent = AIStrategyAgent(max_workers=args.workers)
    agent.run_optimization_loop(iterations=args.iterations)
    agent.save_optimization_report(args.report)


if __name__ == '__main__':
    main()
