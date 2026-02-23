"""Structured error types for actionable user feedback."""

from enum import Enum
from typing import Optional


class ErrorCategory(str, Enum):
    USER_ERROR = "user_error"        # Bad input, fixable by user
    MISSING_DATA = "missing_data"    # Tick files not found
    SYSTEM_ERROR = "system_error"    # CLI crash, timeout, internal error
    VALIDATION = "validation"        # Parameter out of bounds


class BacktestError:
    """Structured error with category, message, and suggested fix."""
    def __init__(self, category: ErrorCategory, message: str, suggestion: str = "", details: str = ""):
        self.category = category
        self.message = message
        self.suggestion = suggestion
        self.details = details

    def to_dict(self) -> dict:
        return {
            "error_category": self.category.value,
            "message": self.message,
            "suggestion": self.suggestion,
            "details": self.details,
        }


# Pre-built common errors
def missing_tick_data_error(symbol: str) -> BacktestError:
    return BacktestError(
        category=ErrorCategory.MISSING_DATA,
        message=f"No tick data file found for {symbol}",
        suggestion=f"Go to Settings > Data Manager to download tick data for {symbol}, or place a CSV file in the validation/ directory.",
    )

def cli_not_found_error() -> BacktestError:
    return BacktestError(
        category=ErrorCategory.SYSTEM_ERROR,
        message="Backtest engine (dashboard_cli.exe) not found",
        suggestion="Build the C++ engine: cd build && cmake .. -G 'MinGW Makefiles' && mingw32-make dashboard_cli",
    )

def timeout_error(timeout_sec: int) -> BacktestError:
    return BacktestError(
        category=ErrorCategory.SYSTEM_ERROR,
        message=f"Backtest timed out after {timeout_sec // 60} minutes",
        suggestion="Try a shorter date range or simpler strategy parameters.",
    )

def cli_crash_error(stderr: str) -> BacktestError:
    return BacktestError(
        category=ErrorCategory.SYSTEM_ERROR,
        message="Backtest engine crashed",
        suggestion="Check that your parameters are reasonable. Very extreme values can cause numerical issues.",
        details=stderr[:500] if stderr else "",
    )

def validation_error(message: str) -> BacktestError:
    return BacktestError(
        category=ErrorCategory.VALIDATION,
        message=message,
        suggestion="Adjust the parameter value to be within the allowed range.",
    )

def invalid_json_error(raw: str) -> BacktestError:
    return BacktestError(
        category=ErrorCategory.SYSTEM_ERROR,
        message="Backtest engine returned invalid output",
        suggestion="This is likely a bug in the C++ engine. Try different parameters.",
        details=raw[:300] if raw else "",
    )
