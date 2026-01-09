#!/usr/bin/env python3
"""
Unit Test Runner for C++ Backtesting Engine

Compiles and runs unit tests for validation components:
- PositionValidator
- CurrencyConverter
- CurrencyRateManager
- MarginManager
- SwapManager
"""

import subprocess
import sys
import os
from pathlib import Path

# Color codes for terminal output
class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    RESET = '\033[0m'
    BOLD = '\033[1m'

def print_header(text):
    print(f"\n{Colors.BOLD}{Colors.BLUE}{'='*60}{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.BLUE}{text}{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.BLUE}{'='*60}{Colors.RESET}\n")

def print_success(text):
    print(f"{Colors.GREEN}[OK] {text}{Colors.RESET}")

def print_error(text):
    print(f"{Colors.RED}[FAIL] {text}{Colors.RESET}")

def print_warning(text):
    print(f"{Colors.YELLOW}[WARN] {text}{Colors.RESET}")

def compile_test(source_file, output_file, include_dirs=None):
    """Compile a C++ test file"""
    if include_dirs is None:
        include_dirs = ['../include']

    # Build g++ command
    gcc_cmd = f"g++ -std=c++17 -Wall -Wextra -O2"
    for inc_dir in include_dirs:
        gcc_cmd += f" -I {inc_dir}"
    gcc_cmd += f" {source_file} -o {output_file}"

    # Wrap in MSYS2 shell for Windows
    msys2_shell = 'C:/msys64/msys2_shell.cmd'
    cmd = [
        msys2_shell,
        '-ucrt64',
        '-defterm',
        '-no-start',
        '-here',
        '-c',
        gcc_cmd
    ]

    print(f"Compiling {source_file}...")
    print(f"Command: {gcc_cmd}")

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=30
        )

        if result.returncode == 0:
            print_success(f"Compilation successful: {output_file}")
            return True
        else:
            print_error(f"Compilation failed!")
            if result.stderr:
                print(f"\nCompiler errors:\n{result.stderr}")
            if result.stdout:
                print(f"\nCompiler output:\n{result.stdout}")
            return False

    except subprocess.TimeoutExpired:
        print_error("Compilation timeout!")
        return False
    except FileNotFoundError:
        print_error(f"Compiler not found: {compiler}")
        print_warning("Please install MinGW-w64 or ensure g++ is in PATH")
        return False
    except Exception as e:
        print_error(f"Compilation error: {e}")
        return False

def run_test(executable):
    """Run a compiled test executable"""
    print(f"\nRunning {executable}...")
    print(f"{Colors.BLUE}{'-'*60}{Colors.RESET}")

    # Convert Windows path to MSYS2 path
    exec_path = f"./{executable}"

    # Wrap in MSYS2 shell for Windows
    msys2_shell = 'C:/msys64/msys2_shell.cmd'
    cmd = [
        msys2_shell,
        '-ucrt64',
        '-defterm',
        '-no-start',
        '-here',
        '-c',
        exec_path
    ]

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=os.getcwd()
        )

        # Print test output
        if result.stdout:
            print(result.stdout)
        if result.stderr:
            print(result.stderr)

        print(f"{Colors.BLUE}{'-'*60}{Colors.RESET}")

        if result.returncode == 0:
            print_success(f"Test passed: {executable}")
            return True
        else:
            print_error(f"Test failed with exit code {result.returncode}")
            return False

    except subprocess.TimeoutExpired:
        print_error("Test execution timeout!")
        return False
    except FileNotFoundError:
        print_error(f"MSYS2 shell not found: {msys2_shell}")
        return False
    except Exception as e:
        print_error(f"Test execution error: {e}")
        return False

def main():
    """Main test runner"""
    print_header("C++ Backtesting Engine - Unit Test Runner")

    # Change to validation directory
    script_dir = Path(__file__).parent
    os.chdir(script_dir)

    tests = [
        {
            'name': 'Position Validator Test',
            'source': 'test_position_validator.cpp',
            'output': 'test_position_validator.exe'
        },
        {
            'name': 'Currency Converter Test',
            'source': 'test_currency_converter.cpp',
            'output': 'test_currency_converter.exe'
        },
        {
            'name': 'Currency Rate Manager Test',
            'source': 'test_currency_rate_manager.cpp',
            'output': 'test_currency_rate_manager.exe'
        },
        {
            'name': 'Simple Validator Test',
            'source': 'test_validator_simple.cpp',
            'output': 'test_validator_simple.exe'
        },
    ]

    total_tests = len(tests)
    passed_tests = 0
    failed_tests = 0

    for test in tests:
        print_header(f"Test: {test['name']}")

        # Compile
        if not compile_test(test['source'], test['output']):
            print_error(f"Skipping {test['name']} due to compilation failure")
            failed_tests += 1
            continue

        # Run
        if run_test(test['output']):
            passed_tests += 1
        else:
            failed_tests += 1

    # Print summary
    print_header("Test Summary")
    print(f"Total tests:  {total_tests}")
    print(f"{Colors.GREEN}Passed:       {passed_tests}{Colors.RESET}")
    if failed_tests > 0:
        print(f"{Colors.RED}Failed:       {failed_tests}{Colors.RESET}")
    else:
        print(f"Failed:       {failed_tests}")

    if failed_tests == 0:
        print(f"\n{Colors.GREEN}{Colors.BOLD}ALL TESTS PASSED!{Colors.RESET}\n")
        return 0
    else:
        print(f"\n{Colors.RED}{Colors.BOLD}SOME TESTS FAILED{Colors.RESET}\n")
        return 1

if __name__ == '__main__':
    sys.exit(main())
