External Code Review — Uncovered Issues and Proposed Fixes

Date: 2026-01-10
Repo: ctrader-backtest

Summary

This document collects the issues I discovered during a focused review of the repository and proposes concrete fixes. Severity levels: Critical, High, Medium, Low. Files referenced are working-copy paths and the suggested fixes are prioritized for safety and minimal disruption.

1) High-level / security (Critical)
- Issue: Server exposes sensitive endpoints (`/api/broker/*`, `/api/backtest/*`) with global CORS and no authentication. Secrets (API keys/passwords) may be logged or persisted.
  - Files: `server.py`, `broker_api.py`
  - Risk: Unauthorized access, secret leakage.
  - Proposed fix:
    - Add authentication (API key or JWT) and require auth on broker/backtest endpoints.
    - Restrict CORS to specific origins in production.
    - Stop logging secrets; redact `api_secret`, `password`, and `api_key` fields before logging or saving.
    - Avoid writing secrets to disk; if necessary, encrypt them and use secure file permissions.

2) Server runtime errors / missing functions (Critical)
- Issue: `server.py` calls undefined names: `run_backtest_sync`, `validate_backtest_config`, `generate_backtest_params`, `execute_backtest`. It also starts a background thread but then calls a synchronous runner.
  - Files: `server.py`
  - Risk: Immediate runtime failures (NameError) and duplicated/blocked work.
  - Proposed fix:
    - Implement or import the missing functions consistently, or replace with a clear job model.
    - Choose one execution model: async background job (return job id) OR synchronous (return results). If background jobs are used, return the job id immediately and remove the blocking synchronous call.
    - Protect `backtest_results` with a `threading.Lock()` or use a thread-safe store (e.g., `queue` or database).

3) Inconsistent/global state (High)
- Issue: Two `BrokerManager` instances may exist (`broker_api.py` defines a global `broker_manager`, and `server.py` creates its own `BrokerManager()`), causing state divergence.
  - Files: `broker_api.py`, `server.py`
  - Proposed fix:
    - Use a single shared instance exported from `broker_api.py` or provide a factory. Import and reuse that instance in `server.py` instead of creating a new one.

4) SQLite concurrency and durability (High)
- Issue: `SweepExecutor` and `_save_result` open many short-lived SQLite connections from worker threads, risking `database is locked` and corruption.
  - Files: `backtest_sweep.py`
  - Proposed fix:
    - Enable WAL mode: `PRAGMA journal_mode=WAL` on DB init.
    - Use a single dedicated DB writer thread or a thread-safe write queue that batches inserts in one connection.
    - Use transactions and proper error handling/ retry with backoff on busy errors.

5) Secrets persisted in cleartext (High)
- Issue: `BrokerManager.save_config()` writes broker credentials to `broker_connections.json`; `BrokerAPI` caches use plain JSON files.
  - Files: `broker_api.py`
  - Proposed fix:
    - Stop writing credentials to disk by default. If persistence is required, encrypt them (e.g., use OS keystore or symmetric key with restricted permissions) and document secure storage process.
    - Mark any persisted secrets as sensitive and ensure file system permissions are 600 (owner read/write).

6) Floating-point loop correctness (Medium)
- Issue: `ParameterGenerator.grid_search` uses floating increments which can produce off-by-one or infinite loops.
  - Files: `backtest_sweep.py`
  - Proposed fix:
    - Re-implement grid generation using integer step counts or use Decimal to avoid FP accumulation error. Example: compute number of steps n = int(round((max-min)/step)) and iterate i in range(n+1) to derive value = min + i*step.

7) Large memory usage from generator consumption (Medium)
- Issue: `execute_sweep` converts parameter generator to list (`list(parameters_generator)`), which can OOM for large searches.
  - Files: `backtest_sweep.py`
  - Proposed fix:
    - Stream parameters: use a bounded queue of parameter batches and process in streaming fashion.
    - If count is required, compute expected count separately or derive from generator metadata.

8) Fragile output parsing (Medium)
- Issue: `_parse_text_output` is brittle (string matching 'profit' or 'win rate') and may miss data. The executor falls back to text parsing frequently.
  - Files: `backtest_sweep.py`, `cache_optimized_executor.py`
  - Proposed fix:
    - Enforce a stable JSON output contract from the backtest executable and fail loudly if invalid.
    - Add schema validation for expected JSON keys.

9) Logging and error handling anti-patterns (Medium)
- Issue: Widespread use of broad `except Exception:` and continued execution; many logs include raw exception strings (may leak secrets) and duplicate messages.
  - Files: multiple (e.g., `broker_api.py`, `backtest_sweep.py`, `metrics_calculator.py`, `server.py`)
  - Proposed fix:
    - Replace broad excepts with specific exceptions where possible. At API boundaries, log full trace; sanitize logs for user-visible messages.
    - Centralize logging configuration (call `logging.basicConfig` only in app entry point).

10) `metrics_calculator` correctness issues (Medium)
- Issues:
  - `BacktestMetrics.to_dict()` contains duplicate keys (e.g., `sortino_ratio`, `return_percent`) and inconsistent rounding.
  - Sharpe/Sortino calculations operate on profit values rather than normalized returns; `_estimate_daily_returns` uses a confusing heuristic (`total_trades/252`).
  - `rank_strategies` normalization is arbitrary.
  - Files: `metrics_calculator.py`
  - Proposed fix:
    - Fix `to_dict()` to list each metric once and document units.
    - Clarify inputs: accept returns normalized by capital (percentage) or compute returns from an equity curve. Adjust Sharpe/Sortino to use return series.
    - Add unit tests with known examples (edge cases: zero-variance, single trade).
    - Make composite ranking configurable and document normalization approach.

11) Path and cross-platform fragility (Medium)
- Issue: Many hard-coded Windows-style paths (backslashes) and inconsistent use of `Path`.
  - Files: `server.py`, `backtest_sweep.py`, `sweep_cli.py`
  - Proposed fix:
    - Use `pathlib.Path` consistently and avoid hard-coded backslashes. Provide cross-platform defaults or detect OS.
    - Ensure `mkdir(..., parents=True, exist_ok=True)` used when creating nested dirs.

12) Duplicate/shallow example code and stubs (Low)
- Issue: Several modules have stubbed or example functions (`compare_with_metatrader` in `cache_optimized_executor.py`) and unused constants.
  - Files: `cache_optimized_executor.py` and others
  - Proposed fix:
    - Remove or mark example functions; add unit tests or example scripts in a `examples/` directory.

13) Tests, CI, dependencies (Low)
- Issue: No unit tests covering critical behavior, `requirements.txt` not pinned.
  - Proposed fix:
    - Add unit tests for `ParameterGenerator` and `MetricsCalculator`.
    - Pin `requirements.txt` with exact versions and add a CI job to run tests and lint.

Minimal, high-impact changes to implement first (recommended order)
1. Fix `server.py` runtime errors: implement stubs or import real functions; choose background-job model and protect `backtest_results` with a lock. (Critical)
2. Stop persisting secrets: modify `BrokerManager.save_config()` to omit secrets or encrypt them. (High)
3. Fix SQLite concurrency: enable WAL and provide a single DB writer. (High)
4. Make grid generator stable (change to integer-step implementation). (Medium)
5. Add authentication to `server.py` and restrict CORS by default. (Critical/High)

Suggested next steps I can implement for you
- Apply minimal code changes for the top 3 items and run quick smoke tests.
- Or produce PR-ready patches for each proposed fix, one at a time, with unit tests.

Document saved to: `external_review/external_review.md`

If you want, I will now implement the top-priority fixes (server runtime errors, secrets persistence, and SQLite writer). Reply with: `fix critical` to proceed, or pick specific items to implement.