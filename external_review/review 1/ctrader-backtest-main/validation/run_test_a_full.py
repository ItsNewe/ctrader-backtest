"""
Complete Test A Automation
Runs entire workflow: MT5 test → export data → our backtest → comparison
"""

import subprocess
import sys
import os
from pathlib import Path
import time

def print_header(title):
    """Print formatted header"""
    print()
    print("=" * 70)
    print(title.center(70))
    print("=" * 70)
    print()

def print_step(step_num, title):
    """Print step header"""
    print()
    print("-" * 70)
    print(f"STEP {step_num}: {title}")
    print("-" * 70)

def check_file_exists(filepath, description):
    """Check if required file exists"""
    path = Path(filepath)
    if path.exists():
        size = path.stat().st_size
        print(f"  ✓ {description}: {filepath}")
        print(f"    Size: {size:,} bytes")
        return True
    else:
        print(f"  ❌ {description} not found: {filepath}")
        return False

def run_command(cmd, description, capture_output=False):
    """Run shell command and handle errors"""
    print(f"  Running: {description}")
    print(f"  Command: {' '.join(cmd)}")
    print()

    try:
        if capture_output:
            result = subprocess.run(cmd, capture_output=True, text=True, check=True)
            print(result.stdout)
            if result.stderr:
                print("STDERR:", result.stderr)
            return True
        else:
            result = subprocess.run(cmd, check=True)
            return True
    except subprocess.CalledProcessError as e:
        print(f"  ❌ Command failed with exit code {e.returncode}")
        if hasattr(e, 'stderr') and e.stderr:
            print(f"  Error: {e.stderr}")
        return False
    except FileNotFoundError:
        print(f"  ❌ Command not found: {cmd[0]}")
        return False

def main():
    print_header("TEST A: COMPLETE VALIDATION WORKFLOW")

    print("This script automates the entire Test A process:")
    print()
    print("  1. Run MT5 test (semi-automated)")
    print("  2. Export historical data from MT5")
    print("  3. Build our C++ backtest")
    print("  4. Run our backtest")
    print("  5. Compare results")
    print()
    print("Requirements:")
    print("  - MT5 installed and logged in")
    print("  - Python packages: MetaTrader5, pandas")
    print("  - CMake and C++ compiler")
    print()

    response = input("Ready to proceed? (y/n): ")
    if response.lower() != 'y':
        print("Aborted.")
        return False

    # Change to project root directory
    project_root = Path(__file__).parent.parent
    os.chdir(project_root)
    print(f"\nWorking directory: {os.getcwd()}")

    # ===================================================================
    # STEP 1: MT5 Test Execution and Data Export
    # ===================================================================
    print_step(1, "MT5 Test Execution and Data Export")

    print("  Running MT5 automation script...")
    print()

    # Run MT5 automation
    mt5_script = "validation/run_mt5_test.py"
    if not run_command([sys.executable, mt5_script], "MT5 automation"):
        print()
        print("  ⚠ MT5 automation had issues")
        print("  You may need to complete some steps manually")
        print()
        response = input("  Continue anyway? (y/n): ")
        if response.lower() != 'y':
            return False

    # Verify MT5 results and data were obtained
    print()
    print("  Verifying MT5 output files...")
    mt5_result_ok = check_file_exists("validation/mt5/test_a_mt5_result.csv",
                                      "MT5 result file")
    price_data_ok = check_file_exists("validation/configs/test_a_data.csv",
                                      "Price data")

    if not mt5_result_ok or not price_data_ok:
        print()
        print("  ❌ Required files missing from MT5 step")
        print()
        print("  Manual intervention needed:")
        if not price_data_ok:
            print("    - Export EURUSD H1 data to validation/configs/test_a_data.csv")
        if not mt5_result_ok:
            print("    - Run test_a_sl_tp_order EA in MT5 Strategy Tester")
            print("    - Copy result to validation/mt5/test_a_mt5_result.csv")
        print()
        response = input("  Completed manual steps? (y/n): ")
        if response.lower() != 'y':
            return False

        # Re-check files
        mt5_result_ok = check_file_exists("validation/mt5/test_a_mt5_result.csv",
                                          "MT5 result file")
        price_data_ok = check_file_exists("validation/configs/test_a_data.csv",
                                          "Price data")

        if not mt5_result_ok or not price_data_ok:
            print("  ❌ Still missing required files. Cannot continue.")
            return False

    print()
    print("  ✓ MT5 step complete")

    # ===================================================================
    # STEP 2: Build Our C++ Backtest
    # ===================================================================
    print_step(2, "Build Our C++ Backtest")

    # Check if build directory exists
    build_dir = Path("build")
    if not build_dir.exists():
        print("  Creating build directory...")
        build_dir.mkdir()

    # Configure with CMake
    print()
    print("  Configuring CMake...")
    if not run_command(["cmake", ".."], "CMake configure", capture_output=True):
        print()
        print("  ❌ CMake configuration failed")
        print()
        print("  Fallback: Try manual compilation")
        print("  Command: g++ -std=c++17 -O3 validation/test_a_our_backtest.cpp \\")
        print("           src/backtest_engine.cpp -I include -o validation/test_a_backtest")
        print()
        response = input("  Try manual compilation? (y/n): ")
        if response.lower() == 'y':
            manual_cmd = [
                "g++", "-std=c++17", "-O3",
                "validation/test_a_our_backtest.cpp",
                "src/backtest_engine.cpp",
                "-I", "include",
                "-o", "validation/test_a_backtest"
            ]
            if not run_command(manual_cmd, "Manual compilation"):
                print("  ❌ Manual compilation also failed")
                return False
        else:
            return False

    # Build the test
    print()
    print("  Building test_a_backtest...")
    if not run_command(["cmake", "--build", "build", "--target", "test_a_backtest",
                       "--config", "Release"],
                      "Build test_a_backtest"):
        print()
        print("  ⚠ Build failed with CMake")
        # Check if manual compilation worked
        if not Path("validation/test_a_backtest").exists() and \
           not Path("validation/test_a_backtest.exe").exists():
            print("  ❌ No executable found")
            return False
        else:
            print("  ✓ Using manually compiled executable")

    # Verify executable exists
    print()
    print("  Verifying executable...")
    exe_paths = [
        "build/validation/test_a_backtest",
        "build/validation/test_a_backtest.exe",
        "validation/test_a_backtest",
        "validation/test_a_backtest.exe"
    ]

    exe_found = None
    for exe_path in exe_paths:
        if Path(exe_path).exists():
            exe_found = exe_path
            print(f"  ✓ Found executable: {exe_path}")
            break

    if not exe_found:
        print("  ❌ Executable not found in any expected location")
        return False

    print()
    print("  ✓ Build step complete")

    # ===================================================================
    # STEP 3: Run Our Backtest
    # ===================================================================
    print_step(3, "Run Our C++ Backtest")

    print("  Executing backtest with exported data...")
    print()

    # Run our backtest
    if not run_command([exe_found, "validation/configs/test_a_data.csv"],
                      "Our backtest engine"):
        print()
        print("  ❌ Backtest execution failed")
        return False

    # Verify output file
    print()
    print("  Verifying backtest output...")
    if not check_file_exists("validation/ours/test_a_our_result.csv",
                            "Our result file"):
        print("  ❌ Backtest did not produce result file")
        return False

    print()
    print("  ✓ Backtest step complete")

    # ===================================================================
    # STEP 4: Compare Results
    # ===================================================================
    print_step(4, "Compare MT5 vs Our Engine")

    print("  Running validation comparison...")
    print()

    # Run comparison script
    comparison_script = "validation/compare_test_a.py"
    result = subprocess.run([sys.executable, comparison_script],
                          capture_output=False)

    # The comparison script will print its own detailed output
    # and return 0 for pass, 1 for fail

    print()
    if result.returncode == 0:
        print_header("✓✓✓ TEST A PASSED ✓✓✓")
        print("Our engine correctly reproduces MT5 behavior!")
        print()
        print("Next steps:")
        print("  1. Document this finding in VALIDATION_RESULTS.md")
        print("  2. Proceed to Test B (Tick Synthesis Validation)")
        print("  3. Continue with full validation roadmap")
        print()
        return True
    else:
        print_header("TEST A REVEALED DIFFERENCES")
        print("This is EXPECTED and GOOD - we found what needs fixing!")
        print()
        print("Next steps:")
        print("  1. Review comparison output above")
        print("  2. Update src/backtest_engine.cpp based on findings")
        print("  3. Re-run this script to verify fix")
        print("  4. Once passing, proceed to Test B")
        print()
        return False

if __name__ == "__main__":
    try:
        success = main()
        print()
        print("=" * 70)
        if success:
            print("VALIDATION WORKFLOW COMPLETE: PASSED")
        else:
            print("VALIDATION WORKFLOW COMPLETE: NEEDS FIXES")
        print("=" * 70)
        sys.exit(0 if success else 1)

    except KeyboardInterrupt:
        print()
        print()
        print("Interrupted by user")
        sys.exit(1)
    except Exception as e:
        print()
        print(f"❌ Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
