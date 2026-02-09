"""
Automated MT5 Strategy Tester Execution
Uses .ini configuration files to run tests without manual interaction
"""

import subprocess
import os
import sys
import time
from pathlib import Path
from datetime import datetime, timedelta
import shutil

class MT5StrategyTesterRunner:
    """Automates MT5 Strategy Tester via .ini configuration files"""

    def __init__(self):
        # MT5 terminal path
        self.mt5_path = r"C:\Program Files\ MetaTrader 5\terminal64.exe"

        # Alternative paths to try
        self.alternative_paths = [
            r"C:\Program Files\MetaTrader 5\terminal64.exe",
            r"C:\Program Files (x86)\MetaTrader 5\terminal64.exe",
        ]

        # Find actual MT5 installation
        if not os.path.exists(self.mt5_path):
            for alt_path in self.alternative_paths:
                if os.path.exists(alt_path):
                    self.mt5_path = alt_path
                    break

        # MT5 data directory (for results)
        self.data_path = Path(r"C:\Users\user\AppData\Roaming\MetaQuotes\Terminal\930119AA53207C8778B41171FBFFB46F")
        # Tester directory is separate from Terminal directory
        self.tester_base = Path(r"C:\Users\user\AppData\Roaming\MetaQuotes\Tester\930119AA53207C8778B41171FBFFB46F")

        # Configuration directory
        self.config_dir = Path("validation/configs/tester_ini")
        self.config_dir.mkdir(parents=True, exist_ok=True)

    def create_tester_ini(self, test_name, expert_name, test_config):
        """
        Create .ini configuration file for MT5 Strategy Tester

        Args:
            test_name: e.g., "test_b"
            expert_name: e.g., "test_b_tick_synthesis"
            test_config: dict with test parameters
        """
        # Default configuration
        config = {
            'Expert': f'Experts\\{expert_name}.ex5',
            'Symbol': test_config.get('symbol', 'EURUSD'),
            'Period': test_config.get('period', 'H1'),  # H1, M15, etc.
            'Deposit': test_config.get('deposit', 10000),
            'Leverage': test_config.get('leverage', '1:500'),
            'Model': test_config.get('model', 0),  # 0=Every tick, 1=1 min OHLC, 2=Open prices
            'ExecutionMode': 0,  # Normal
            'Optimization': 0,   # Disabled
            'FromDate': test_config.get('from_date', '2025.12.01'),
            'ToDate': test_config.get('to_date', '2026.01.06'),
            'ForwardMode': 0,
            'Report': f'Reports\\{test_name}_report.htm',
            'ReplaceReport': 1,
            'Visual': test_config.get('visual', 0),  # 0=No visualization
            'ShutdownTerminal': 1  # Close terminal after test
        }

        # Create .ini file
        ini_path = self.config_dir / f"{test_name}.ini"

        with open(ini_path, 'w') as f:
            f.write("[Tester]\n")
            for key, value in config.items():
                f.write(f"{key}={value}\n")

        print(f"Created configuration: {ini_path}")
        return ini_path

    def run_test(self, test_name, expert_name, test_config):
        """
        Run a single test in MT5 Strategy Tester

        Args:
            test_name: e.g., "test_f"
            expert_name: e.g., "test_f_margin_calc"
            test_config: dict with test parameters
        """
        print("=" * 70)
        print(f"RUNNING {test_name.upper()}: {expert_name}")
        print("=" * 70)
        print()

        # Check if MT5 exists
        if not os.path.exists(self.mt5_path):
            print(f"ERROR: MT5 terminal not found at: {self.mt5_path}")
            return False

        # Create .ini configuration
        ini_path = self.create_tester_ini(test_name, expert_name, test_config)

        # Copy config to MT5's config directory as common.ini
        mt5_config_dir = self.data_path / "config"
        mt5_config_dir.mkdir(parents=True, exist_ok=True)
        common_ini = mt5_config_dir / "common.ini"

        # Backup existing common.ini if it exists
        if common_ini.exists():
            backup = mt5_config_dir / "common.ini.backup"
            shutil.copy2(common_ini, backup)
            print(f"Backed up existing config to: {backup}")

        # Copy our test config as common.ini
        shutil.copy2(ini_path, common_ini)
        print(f"Copied config to: {common_ini}")

        # Run MT5 Strategy Tester with /portable flag
        command = [self.mt5_path, "/portable"]

        print(f"Executing: {' '.join(command)}")
        print()
        print("MT5 Strategy Tester starting...")
        print("Please verify that the test starts automatically")
        print("(Terminal will close automatically when test completes)")
        print()

        try:
            # Start MT5 tester
            result = subprocess.run(command, check=False)

            # Wait a bit for files to be written
            time.sleep(2)

            print("Test execution complete")

            # Restore backup if it exists
            backup = mt5_config_dir / "common.ini.backup"
            if backup.exists():
                shutil.copy2(backup, common_ini)
                backup.unlink()
                print("Restored original config")

            return True

        except Exception as e:
            print(f"ERROR: {e}")
            return False

    def retrieve_test_results(self, test_name):
        """Retrieve test results from tester directory"""
        print()
        print(f"Retrieving results for {test_name}...")

        # Map test names to result files
        result_files = {
            'test_b': ['test_b_ticks.csv', 'test_b_summary.json'],
            'test_c': ['test_c_slippage.csv', 'test_c_summary.json'],
            'test_d': ['test_d_spread.csv', 'test_d_summary.json'],
            'test_e': ['test_e_swap_timing.csv', 'test_e_summary.json'],
            'test_f': ['test_f_margin.csv', 'test_f_summary.json']
        }

        if test_name not in result_files:
            print(f"Unknown test: {test_name}")
            return False

        # Find tester files directory
        agent_dirs = list(self.tester_base.glob("Agent-*/MQL5/Files"))
        if not agent_dirs:
            print(f"ERROR: Tester files directory not found")
            print(f"Expected under: {self.tester_base}")
            return False

        tester_files_dir = agent_dirs[0]
        print(f"Tester directory: {tester_files_dir}")

        # Copy each result file
        all_found = True
        for filename in result_files[test_name]:
            source = tester_files_dir / filename
            dest = Path(f"validation/mt5/{filename}")

            if source.exists():
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, dest)
                print(f"  Retrieved: {filename}")
            else:
                print(f"  NOT FOUND: {filename}")
                all_found = False

        print()
        return all_found

    def run_all_tests(self):
        """Run all validation tests"""

        # Define all tests
        tests = [
            {
                'name': 'test_f',
                'expert': 'test_f_margin_calc',
                'description': 'Margin Calculation',
                'config': {
                    'symbol': 'EURUSD',
                    'period': 'H1',
                    'model': 0,  # Every tick
                    'from_date': '2025.12.01',
                    'to_date': '2025.12.05',  # Short test
                }
            },
            {
                'name': 'test_c',
                'expert': 'test_c_slippage',
                'description': 'Slippage Distribution',
                'config': {
                    'symbol': 'EURUSD',
                    'period': 'H1',
                    'model': 0,
                    'from_date': '2025.12.01',
                    'to_date': '2025.12.05',
                }
            },
            {
                'name': 'test_b',
                'expert': 'test_b_tick_synthesis',
                'description': 'Tick Synthesis',
                'config': {
                    'symbol': 'EURUSD',
                    'period': 'H1',
                    'model': 0,
                    'from_date': '2025.12.01',
                    'to_date': '2025.12.10',
                }
            },
            {
                'name': 'test_d',
                'expert': 'test_d_spread_widening',
                'description': 'Spread Widening',
                'config': {
                    'symbol': 'EURUSD',
                    'period': 'H1',
                    'model': 0,
                    'from_date': '2025.12.01',
                    'to_date': '2025.12.05',
                }
            },
            {
                'name': 'test_e',
                'expert': 'test_e_swap_timing',
                'description': 'Swap Timing',
                'config': {
                    'symbol': 'EURUSD',
                    'period': 'H1',
                    'model': 0,
                    'from_date': '2025.12.01',
                    'to_date': '2025.12.10',  # Need multiple days
                }
            }
        ]

        results = {}

        for test in tests:
            print()
            print("#" * 70)
            print(f"TEST: {test['description']}")
            print("#" * 70)
            print()

            # Run test
            success = self.run_test(
                test['name'],
                test['expert'],
                test['config']
            )

            if not success:
                print(f"Failed to run {test['name']}")
                results[test['name']] = False
                continue

            # Wait for test to complete
            print("Waiting for test to complete...")
            time.sleep(5)  # Give time for files to be written

            # Retrieve results
            retrieved = self.retrieve_test_results(test['name'])
            results[test['name']] = retrieved

            print()
            input("Press Enter to continue to next test...")

        # Summary
        print()
        print("=" * 70)
        print("TEST SUITE COMPLETE")
        print("=" * 70)
        print()
        for test in tests:
            status = "✓ PASS" if results.get(test['name']) else "✗ FAIL"
            print(f"{status}  {test['name']}: {test['description']}")

        return results

def main():
    print("=" * 70)
    print("MT5 STRATEGY TESTER - AUTOMATED EXECUTION")
    print("=" * 70)
    print()
    print("This script will:")
    print("  1. Create .ini configuration files")
    print("  2. Launch MT5 Strategy Tester for each test")
    print("  3. Automatically retrieve results")
    print()

    response = input("Continue? (y/n): ")
    if response.lower() != 'y':
        print("Aborted")
        return False

    runner = MT5StrategyTesterRunner()
    runner.run_all_tests()

    return True

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
