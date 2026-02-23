"""
Application configuration using Pydantic Settings.
Reads from environment variables or .env file.
"""

from pathlib import Path
from pydantic_settings import BaseSettings
from functools import lru_cache


class Settings(BaseSettings):
    # Project paths
    project_root: Path = Path(__file__).parent.parent
    build_dir: Path = Path(__file__).parent.parent / "build"
    data_dir: Path = Path(__file__).parent.parent / "validation"
    results_dir: Path = Path(__file__).parent.parent / "results"

    # C++ backtest CLI (dashboard_cli.exe = extended version with trade list + broker params)
    backtest_exe: str = "dashboard_cli.exe"

    # MT5 settings
    mt5_path: str = ""
    mt5_account_id: str = ""
    mt5_password: str = ""
    mt5_server: str = ""

    # Server
    host: str = "0.0.0.0"
    port: int = 8000
    debug: bool = True

    # Sweep defaults
    max_sweep_workers: int = 16
    sweep_timeout_seconds: int = 300

    model_config = {
        "env_file": ".env",
        "env_prefix": "BT_",
        "extra": "ignore",
    }

    @property
    def backtest_exe_path(self) -> Path:
        """Resolve backtest CLI executable path."""
        # Try validation/ first, then build/bin/, then build/
        candidates = [
            self.build_dir / "validation" / self.backtest_exe,
            self.build_dir / "bin" / self.backtest_exe,
            self.build_dir / self.backtest_exe,
            self.project_root / "build" / "validation" / self.backtest_exe,
        ]
        for p in candidates:
            if p.exists():
                return p
        # Return first candidate even if not found (for error messages)
        return candidates[0]


@lru_cache
def get_settings() -> Settings:
    return Settings()
