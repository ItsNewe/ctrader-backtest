"""
Parallel Backtest Parameter Sweep Executor
Generates parameter combinations and runs backtests in parallel
"""

import json
import sqlite3
import subprocess
import threading
import queue
import logging
from pathlib import Path
from typing import List, Dict, Optional, Generator
from datetime import datetime
from concurrent.futures import ThreadPoolExecutor, as_completed
import time

from metrics_calculator import MetricsCalculator

logger = logging.getLogger(__name__)

class SweepResult:
    """Container for a single backtest result"""
    def __init__(self, parameters: Dict, backtest_output: str, metrics: Dict = None):
        self.parameters = parameters
        self.backtest_output = backtest_output
        self.metrics = metrics or {}
        self.timestamp = datetime.now().isoformat()
        
    def to_dict(self) -> dict:
        return {
            'parameters': self.parameters,
            'metrics': self.metrics,
            'timestamp': self.timestamp,
        }


class ParameterGenerator:
    """Generate parameter combinations for grid search or random search"""
    
    @staticmethod
    def grid_search(
        survive_range: tuple = (0.5, 5.0, 0.5),      # (min, max, step)
        size_range: tuple = (0.1, 10.0, 0.5),        # (min, max, step)
        spacing_range: tuple = (0.5, 10.0, 0.5),     # (min, max, step)
    ) -> Generator[Dict, None, None]:
        """
        Generate parameter combinations using grid search

        Args:
            survive_range: (min, max, step) for survive parameter
            size_range: (min, max, step) for size parameter
            spacing_range: (min, max, step) for spacing parameter

        Yields:
            Dict with parameter combinations
        """
        survive_min, survive_max, survive_step = survive_range
        size_min, size_max, size_step = size_range
        spacing_min, spacing_max, spacing_step = spacing_range

        # Use integer step counts to avoid floating-point accumulation errors
        survive_steps = int(round((survive_max - survive_min) / survive_step)) + 1
        size_steps = int(round((size_max - size_min) / size_step)) + 1
        spacing_steps = int(round((spacing_max - spacing_min) / spacing_step)) + 1

        for i in range(survive_steps):
            survive = survive_min + i * survive_step
            for j in range(size_steps):
                size = size_min + j * size_step
                for k in range(spacing_steps):
                    spacing = spacing_min + k * spacing_step
                    yield {
                        'survive': round(survive, 2),
                        'size': round(size, 2),
                        'spacing': round(spacing, 2),
                    }
    
    @staticmethod
    def random_search(
        num_combinations: int,
        survive_range: tuple = (0.5, 5.0),
        size_range: tuple = (0.1, 10.0),
        spacing_range: tuple = (0.5, 10.0),
    ) -> Generator[Dict, None, None]:
        """Generate random parameter combinations"""
        import random
        
        survive_min, survive_max = survive_range
        size_min, size_max = size_range
        spacing_min, spacing_max = spacing_range
        
        for _ in range(num_combinations):
            yield {
                'survive': round(random.uniform(survive_min, survive_max), 2),
                'size': round(random.uniform(size_min, size_max), 2),
                'spacing': round(random.uniform(spacing_min, spacing_max), 2),
            }


class SweepExecutor:
    """Execute backtests in parallel with parameter sweeps"""
    
    def __init__(
        self,
        backtest_exe: str = r'build\bin\backtest.exe',
        data_file: str = r'data\EURUSD_2023.csv',
        max_workers: int = None,
    ):
        """
        Initialize sweep executor
        
        Args:
            backtest_exe: Path to compiled backtest executable
            data_file: Path to OHLC data CSV
            max_workers: Number of parallel workers (default: CPU count)
        """
        self.backtest_exe = backtest_exe
        self.data_file = data_file
        self.max_workers = max_workers or 4  # Default to 4 workers
        self.results_db = Path('results/sweeps.db')
        self._init_database()
    
    def _init_database(self):
        """Initialize SQLite database for results"""
        self.results_db.parent.mkdir(exist_ok=True)
        
        conn = sqlite3.connect(str(self.results_db))
        cursor = conn.cursor()
        
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS sweeps (
                sweep_id TEXT PRIMARY KEY,
                created_at TEXT,
                data_file TEXT,
                total_combinations INTEGER,
                completed_combinations INTEGER,
                status TEXT
            )
        ''')
        
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS results (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                sweep_id TEXT,
                parameters TEXT,
                metrics TEXT,
                timestamp TEXT,
                FOREIGN KEY (sweep_id) REFERENCES sweeps(sweep_id)
            )
        ''')
        
        conn.commit()
        conn.close()
    
    def execute_sweep(
        self,
        parameters_generator: Generator,
        sweep_id: str = None,
        progress_callback = None,
    ) -> Dict:
        """
        Execute parameter sweep with parallel workers
        
        Args:
            parameters_generator: Generator yielding parameter dicts
            sweep_id: Unique ID for this sweep (auto-generated if None)
            progress_callback: Function called with (completed, total, result)
            
        Returns:
            Dict with sweep results and statistics
        """
        if sweep_id is None:
            sweep_id = f"sweep_{int(time.time())}"
        
        # Convert generator to list to know total count
        parameters_list = list(parameters_generator)
        total_combinations = len(parameters_list)
        
        logger.info(f"Starting sweep {sweep_id} with {total_combinations} combinations")
        
        # Initialize database record
        conn = sqlite3.connect(str(self.results_db))
        cursor = conn.cursor()
        cursor.execute('''
            INSERT INTO sweeps (sweep_id, created_at, data_file, total_combinations, completed_combinations, status)
            VALUES (?, ?, ?, ?, 0, 'running')
        ''', (sweep_id, datetime.now().isoformat(), self.data_file, total_combinations))
        conn.commit()
        conn.close()
        
        all_results = []
        completed = 0
        
        # Execute with thread pool
        with ThreadPoolExecutor(max_workers=self.max_workers) as executor:
            futures = {
                executor.submit(self._run_backtest, params): params
                for params in parameters_list
            }
            
            for future in as_completed(futures):
                try:
                    result = future.result()
                    all_results.append(result)
                    completed += 1
                    
                    # Store result in database
                    self._save_result(sweep_id, result)
                    
                    if progress_callback:
                        progress_callback(completed, total_combinations, result)
                    
                    logger.info(f"Completed {completed}/{total_combinations} - Profit: {result.metrics.get('profit_loss', 0):.2f}")
                    
                except Exception as e:
                    logger.error(f"Backtest failed: {str(e)}")
                    completed += 1
        
        # Calculate statistics
        stats = self._calculate_sweep_stats(all_results)
        
        # Update database with completion
        conn = sqlite3.connect(str(self.results_db))
        cursor = conn.cursor()
        cursor.execute('''
            UPDATE sweeps SET status = 'completed', completed_combinations = ?
            WHERE sweep_id = ?
        ''', (completed, sweep_id))
        conn.commit()
        conn.close()
        
        logger.info(f"Sweep {sweep_id} complete. Best strategy: {stats['best_result']['composite_score']:.2f}")
        
        return {
            'sweep_id': sweep_id,
            'total_combinations': total_combinations,
            'completed': completed,
            'statistics': stats,
            'results': all_results,
        }
    
    def _run_backtest(self, parameters: Dict) -> SweepResult:
        """Execute single backtest with given parameters"""
        try:
            # Build command line
            cmd = [
                self.backtest_exe,
                f"--data={self.data_file}",
                f"--survive={parameters['survive']}",
                f"--size={parameters['size']}",
                f"--spacing={parameters['spacing']}",
                "--json",  # Request JSON output
            ]
            
            # Run backtest
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=300  # 5 minute timeout per backtest
            )
            
            # Parse results
            output = result.stdout
            metrics = {}
            
            if result.returncode == 0:
                try:
                    # Try to parse JSON output
                    json_data = json.loads(output)
                    # Extract metrics from JSON (structure: {parameters: {...}, metrics: {...}})
                    if isinstance(json_data, dict) and 'metrics' in json_data:
                        metrics = json_data['metrics']
                    else:
                        metrics = json_data
                except json.JSONDecodeError:
                    # Fall back to parsing text output
                    metrics = self._parse_text_output(output)
            else:
                logger.warning(f"Backtest failed for params {parameters}: {result.stderr}")
            
            return SweepResult(parameters, output, metrics)
            
        except subprocess.TimeoutExpired:
            logger.error(f"Backtest timeout for params {parameters}")
            return SweepResult(parameters, "TIMEOUT", {})
        except Exception as e:
            logger.error(f"Error running backtest: {str(e)}")
            return SweepResult(parameters, str(e), {})
    
    def _parse_text_output(self, output: str) -> Dict:
        """Parse text output from backtest executable"""
        metrics = {}
        
        # Look for key metrics in output
        for line in output.split('\n'):
            if 'profit' in line.lower():
                parts = line.split(':')
                if len(parts) == 2:
                    try:
                        metrics['profit_loss'] = float(parts[1].strip())
                    except ValueError:
                        pass
            elif 'win rate' in line.lower():
                parts = line.split(':')
                if len(parts) == 2:
                    try:
                        metrics['win_rate'] = float(parts[1].strip().rstrip('%'))
                    except ValueError:
                        pass
        
        return metrics
    
    def _save_result(self, sweep_id: str, result: SweepResult):
        """Save result to database"""
        conn = sqlite3.connect(str(self.results_db))
        cursor = conn.cursor()
        
        cursor.execute('''
            INSERT INTO results (sweep_id, parameters, metrics, timestamp)
            VALUES (?, ?, ?, ?)
        ''', (
            sweep_id,
            json.dumps(result.parameters),
            json.dumps(result.metrics),
            result.timestamp,
        ))
        
        conn.commit()
        conn.close()
    
    def _calculate_sweep_stats(self, results: List[SweepResult]) -> Dict:
        """Calculate statistics across all results"""
        if not results:
            return {'best_result': {}, 'statistics': {}}
        
        # Extract metrics for ranking
        results_dicts = [r.to_dict() for r in results]
        
        # Rank strategies
        ranked = MetricsCalculator.rank_strategies(results_dicts)
        
        return {
            'best_result': ranked[0] if ranked else {},
            'top_10': ranked[:10],
            'average_metrics': self._avg_metrics(results_dicts),
            'total_profitable': sum(1 for r in results_dicts if r['metrics'].get('profit_loss', 0) > 0),
            'total_results': len(results_dicts),
        }
    
    def _avg_metrics(self, results: List[Dict]) -> Dict:
        """Calculate average metrics across results"""
        if not results:
            return {}
        
        metrics_list = [r['metrics'] for r in results if r['metrics']]
        
        if not metrics_list:
            return {}
        
        avg = {}
        for key in metrics_list[0].keys():
            values = [m.get(key, 0) for m in metrics_list if isinstance(m.get(key, 0), (int, float))]
            if values:
                avg[key] = sum(values) / len(values)
        
        return avg
    
    def get_sweep_results(self, sweep_id: str) -> Dict:
        """Retrieve results for a specific sweep"""
        conn = sqlite3.connect(str(self.results_db))
        cursor = conn.cursor()
        
        # Get sweep info
        cursor.execute('SELECT * FROM sweeps WHERE sweep_id = ?', (sweep_id,))
        sweep = cursor.fetchone()
        
        if not sweep:
            return None
        
        # Get all results for this sweep
        cursor.execute('SELECT parameters, metrics, timestamp FROM results WHERE sweep_id = ? ORDER BY timestamp', (sweep_id,))
        results = cursor.fetchall()
        conn.close()
        
        results_list = [
            {
                'parameters': json.loads(r[0]),
                'metrics': json.loads(r[1]),
                'timestamp': r[2],
            }
            for r in results
        ]
        
        # Rank results
        ranked = MetricsCalculator.rank_strategies(results_list)
        
        return {
            'sweep_id': sweep_id,
            'created_at': sweep[1],
            'total_combinations': sweep[3],
            'completed_combinations': sweep[4],
            'status': sweep[5],
            'results': ranked,
            'best_result': ranked[0] if ranked else None,
            'statistics': self._calculate_sweep_stats([SweepResult(r['parameters'], '', r['metrics']) for r in ranked]),
        }
