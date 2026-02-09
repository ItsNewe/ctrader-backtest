# MT5 Data Reference Guide

## 1. Downloading Tick History

### Python Script Template
```python
import MetaTrader5 as mt5
from datetime import datetime
import pandas as pd

# Initialize with specific terminal
mt5.initialize(path=r"C:\Program Files\ MetaTrader 5\terminal64.exe")

# Get ticks
symbol = "NAS100"  # or "XAUUSD", "EURUSD", etc.
start_date = datetime(2025, 4, 7, 0, 0, 0)
end_date = datetime(2025, 10, 30, 23, 59, 59)

# Download in chunks (weekly recommended for large ranges)
ticks = mt5.copy_ticks_range(symbol, start_date, end_date, mt5.COPY_TICKS_ALL)

# Convert to DataFrame
df = pd.DataFrame(ticks)
df['time'] = pd.to_datetime(df['time'], unit='s')

# Save to CSV (MT5 format: tab-separated)
df.to_csv(f"{symbol}_TICKS.csv", sep='\t', index=False)

mt5.shutdown()
```

### Tick Flags
| Flag | Value | Meaning |
|------|-------|---------|
| TICK_FLAG_BID | 2 | Bid price changed |
| TICK_FLAG_ASK | 4 | Ask price changed |
| TICK_FLAG_LAST | 8 | Last deal price changed |
| TICK_FLAG_VOLUME | 16 | Volume changed |
| TICK_FLAG_BUY | 32 | Last deal was buy |
| TICK_FLAG_SELL | 64 | Last deal was sell |

---

## 2. Account Info

### Query
```python
account = mt5.account_info()
```

### Key Fields
| Field | Type | Meaning |
|-------|------|---------|
| `login` | int | Account number |
| `leverage` | int | Account leverage (e.g., 500 = 1:500) |
| `balance` | float | Account balance (excluding floating P/L) |
| `equity` | float | Balance + floating P/L |
| `margin` | float | Currently used margin |
| `margin_free` | float | Free margin available |
| `margin_level` | float | Margin level % (equity/margin × 100) |
| `profit` | float | Current floating profit/loss |
| `currency` | str | Account currency (e.g., "USD") |
| `server` | str | Trade server name |
| `trade_mode` | int | 0=demo, 1=contest, 2=real |
| `limit_orders` | int | Max pending orders allowed |
| `margin_so_mode` | int | Stop-out mode (0=%, 1=money) |
| `margin_so_call` | float | Margin call level |
| `margin_so_so` | float | Stop-out level |

### Trade Modes
| Value | Meaning |
|-------|---------|
| 0 | ACCOUNT_TRADE_MODE_DEMO |
| 1 | ACCOUNT_TRADE_MODE_CONTEST |
| 2 | ACCOUNT_TRADE_MODE_REAL |

---

## 3. Symbol Info

### Query
```python
info = mt5.symbol_info("NAS100")
tick = mt5.symbol_info_tick("NAS100")
```

### Key Symbol Fields
| Field | Type | Meaning | Example (NAS100) |
|-------|------|---------|------------------|
| `name` | str | Symbol name | "NAS100" |
| `description` | str | Full name | "US Tech 100 Index" |
| `digits` | int | Price decimal places | 2 |
| `point` | float | Minimum price change | 0.01 |
| `trade_contract_size` | float | Contract size | 1.0 |
| `volume_min` | float | Min lot size | 0.01 |
| `volume_max` | float | Max lot size | 100.0 |
| `volume_step` | float | Lot step | 0.01 |

### Margin Calculation Fields
| Field | Meaning |
|-------|---------|
| `trade_calc_mode` | Margin calculation method (see below) |
| `margin_initial` | Initial margin (0 = use formula) |
| `margin_maintenance` | Maintenance margin |
| `margin_hedged` | Margin for hedged positions |
| `currency_margin` | Margin currency |

### trade_calc_mode Values
| Value | Name | Formula |
|-------|------|---------|
| 0 | FOREX | Lots × ContractSize × MarketPrice / Leverage |
| 1 | CFD | Lots × ContractSize × MarketPrice × MarginRate |
| 2 | FUTURES | Lots × InitialMargin |
| 3 | CFD_INDEX | Lots × ContractSize × MarketPrice × TickPrice / TickSize |
| 4 | CFD_LEVERAGE | Lots × ContractSize × MarketPrice / Leverage |

**Note**: NAS100 at  uses CFD mode with margin_rate = 0.01 (1%), giving ~$258/lot at price 25,872.

### Tick Value Fields
| Field | Meaning |
|-------|---------|
| `trade_tick_size` | Min price increment |
| `trade_tick_value` | Value of 1 tick move in account currency |
| `trade_tick_value_profit` | Tick value for profit |
| `trade_tick_value_loss` | Tick value for loss |

### Swap Fields
| Field | Meaning |
|-------|---------|
| `swap_mode` | How swap is calculated (see below) |
| `swap_long` | Swap for long positions |
| `swap_short` | Swap for short positions |
| `swap_rollover3days` | Day when 3x swap charged (0=Sun, 5=Fri) |

### swap_mode Values
| Value | Meaning |
|-------|---------|
| 0 | Disabled |
| 1 | Points |
| 2 | Symbol currency |
| 3 | Margin currency |
| 4 | Deposit currency |
| 5 | % of current price |
| 6 | % of open price |

### Current Tick Fields
```python
tick = mt5.symbol_info_tick("NAS100")
```
| Field | Meaning |
|-------|---------|
| `bid` | Current bid price |
| `ask` | Current ask price |
| `last` | Last deal price |
| `volume` | Last deal volume |
| `time` | Last tick time (Unix) |
| `time_msc` | Time in milliseconds |

---

## 4. Quick Reference: NAS100 ()

```python
# Symbol specs
contract_size = 1.0
digits = 2
point = 0.01
volume_min = 0.01
volume_max = 100.0

# Margin calculation (CFD mode)
# Margin = Lots × ContractSize × Price × MarginRate
# MarginRate = 0.01 (1%)
# At price 25,872: 1 lot = $258.72 margin

# Swaps (% of current price)
swap_mode = 5
swap_long = -5.96  # % per year
swap_short = 1.6   # % per year
swap_3days = 5     # Friday

# Tick value
tick_value = 0.01  # $0.01 per 0.01 move per lot
# So 1 point (1.00) = $1.00 per lot
```

---

## 5. Common Queries

### Get All Available Symbols
```python
symbols = mt5.symbols_get()
for s in symbols:
    print(f"{s.name}: {s.description}")
```

### Filter Symbols by Pattern
```python
# All indices
indices = mt5.symbols_get(group="*Index*")

# All forex
forex = mt5.symbols_get(group="*,!*CFD*,!*Index*")
```

### Check Symbol Availability
```python
info = mt5.symbol_info("NAS100")
if info is None:
    print("Symbol not found")
elif not info.visible:
    # Make visible in Market Watch
    mt5.symbol_select("NAS100", True)
```

---

## 6. Error Handling

```python
if not mt5.initialize():
    print(f"Initialize failed: {mt5.last_error()}")

# Error codes
# (1, "Success")
# (-1, "Generic error")
# (-2, "Invalid params")
# (-3, "No memory")
# (-4, "Not found")
# (-5, "Invalid version")
# (-6, "Auth failed")
# (-7, "Unsupported")
# (-10002, "Network")
# (-10003, "Not connected")
# (-10004, "Not enough data")
```

---

*Updated: 2025-02*
