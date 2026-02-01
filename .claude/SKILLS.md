# Available Skills

Custom slash commands for the ctrader-backtest project.

**See `/CLAUDE.md` for full framework documentation on building strategy tests.**

## Strategy Validation

### /validate-strategy
Compare C++ backtest results against MT5 for a specific strategy.
```
/validate-strategy fill_up Broker
```

### /compare-reports
Side-by-side comparison of two MT5 reports.
```
/compare-reports validation/Broker/fill_up/Report.xlsx validation/Grid/fill_up/Report.xlsx
```

## Debugging

### /debug-swap
Deep analysis of swap calculation differences.
```
/debug-swap Broker
```

### /verify-logs
Tick-by-tick verification against MT5 SQL logs.
```
/verify-logs validation/Broker/fill_up_log_XAUUSD.csv
```

## Configuration

### /sync-broker
Fetch latest broker settings from MT5.
```
/sync-broker Broker
```

---

## Adding New Skills

Create a new markdown file in `.claude/skills/` with:
- Skill name as filename (e.g., `my-skill.md`)
- Usage section with command syntax
- Parameters section
- What it does section
- Example output
- Related files

Skills are invoked with `/skill-name` in the chat.
