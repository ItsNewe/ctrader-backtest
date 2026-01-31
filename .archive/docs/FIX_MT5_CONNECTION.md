# Fix for MetaTrader5 Connection Error

## Problem You Encountered

When selecting MetaTrader5 and clicking "Connect Broker", you got:
```
Error: Unexpected token '<', "<!doctype"... is not valid JSON
```

This happens when the server returns **HTML error page** instead of JSON.

---

## Root Cause

The most likely cause is that the **MetaTrader5 Python module is not installed**.

When the module is missing:
1. Python code tries to `import MetaTrader5`
2. This fails silently in some code paths
3. An error occurs but isn't handled properly
4. Flask returns HTML error page instead of JSON
5. JavaScript can't parse HTML as JSON → Your error

---

## Solution

### Step 1: Install MetaTrader5 Module

**Option A: Run the installer (Easiest)**
```bash
# Double-click this file:
install_mt5.bat
```

**Option B: Manual installation**
```bash
python -m pip install MetaTrader5
```

**Verify installation:**
```bash
python -c "import MetaTrader5; print('✓ MetaTrader5 installed')"
```

### Step 2: Restart Flask Server

```bash
# Stop the server (Ctrl+C)
# Then restart:
python server.py
```

### Step 3: Try Again

1. Open dashboard: http://localhost:5000
2. Go to Broker Settings
3. Select MetaTrader5
4. Enter your account ID
5. Click "Connect Broker"

Should now show **real error messages** instead of JSON parse error.

---

## Important: MetaTrader5 Terminal Must Be Running

After installing the Python module, **you MUST have MetaTrader5 terminal open and logged in**.

```
✓ MetaTrader5 application is running
✓ You are logged in with your account
✓ Terminal is in the background (doesn't need to be in focus)
```

If terminal is closed, you'll get:
```
Failed to connect to MetaTrader5:
1. MetaTrader5 terminal not running
2. Account not logged in to MT5 terminal
...
```

---

## Broker-Specific MetaTrader5

If you installed MetaTrader5 from your **broker** (not from MetaTrader developer), there's a possibility it doesn't support Python API.

**Check:**
1. Does your broker's documentation mention "Python API support"?
2. Or does it say "MetaTrader5 terminal"?

**If your broker doesn't support Python API:**

You have two options:

### Option A: Use cTrader Instead
If your broker offers cTrader:
1. Get free API credentials from https://spotware.com/open-api (5 min)
2. Use cTrader in dashboard (works better with Python anyway)
3. More stable and faster

### Option B: Use Manual Configuration
1. Get instrument specs from your broker
2. Enter specs manually in dashboard
3. Run backtest

---

## Improved Error Messages

I've improved the error handling, so you'll now see:

✅ **If MetaTrader5 module is missing:**
```
MetaTrader5 Python module not installed. 
Run: pip install MetaTrader5
```

✅ **If terminal isn't running:**
```
Failed to connect to MetaTrader5:
1. MetaTrader5 terminal not running
2. Account not logged in to MT5 terminal
...
```

✅ **If account ID is wrong:**
```
Failed to connect to MetaTrader5:
Check Flask console for detailed error logs
```

---

## Diagnostic Tools

I've added tools to help diagnose issues:

### In Browser Console (F12)

```javascript
// See what's installed
checkDiagnostics()

// You'll see:
{
  modules: {
    metatrader5: "installed" or "NOT installed"  ← Look here
  }
}
```

### In Flask Console

When you try to connect, watch for detailed error messages:

```
ERROR:__main__:Broker connection error: ...
```

---

## New Files Added

- ✅ **MT5_TROUBLESHOOTING.md** - Complete MT5 troubleshooting guide
- ✅ **install_mt5.bat** - Automated installer for Windows
- ✅ Improved error handling in server.py
- ✅ Better error messages in broker_api.py
- ✅ Diagnostic endpoint: `/api/broker/diagnose`

---

## Step-by-Step: What Changed

1. **Improved Error Handling**
   - All exceptions now caught properly
   - Always returns JSON, never HTML
   - Better error messages

2. **New Diagnostic Endpoint**
   - `GET /api/broker/diagnose` - Shows system info
   - Tells you if MetaTrader5 is installed
   - Useful for troubleshooting

3. **Better Frontend Messages**
   - Shows helpful tips for MT5 issues
   - Explains what to do next
   - Links to documentation

4. **Installation Script**
   - Double-click to install MetaTrader5
   - Automatic and foolproof

---

## Quick Troubleshooting Flowchart

```
Error connecting to MetaTrader5?

├─ Get "module not installed" message?
│  └─ Fix: Run install_mt5.bat
│
├─ Get "terminal not running" message?
│  └─ Fix: Open MetaTrader5 and log in
│
├─ Get "account not found" message?
│  └─ Fix: Check Account ID matches MT5
│
├─ Still getting HTML error (<!doctype)?
│  └─ Fix: Restart server (python server.py)
│
└─ Everything else?
   └─ Run checkDiagnostics() in console
```

---

## Next Steps

### Right Now (5 min)
1. Run: `install_mt5.bat`
2. Restart: `python server.py`
3. Try connecting again

### If Still Issues (10 min)
1. Open browser console (F12)
2. Run: `checkDiagnostics()`
3. Share the output

### If Broker Doesn't Support Python (15 min)
1. Check [MT5_TROUBLESHOOTING.md](./MT5_TROUBLESHOOTING.md)
2. Consider using cTrader instead
3. Or use manual configuration

---

## Summary

| Issue | Cause | Fix |
|-------|-------|-----|
| JSON parse error | Server error not handled | Restart server after install |
| Module not found | Not installed | Run `install_mt5.bat` |
| Terminal not found | MT5 not running | Open MT5 terminal |
| Connection fails | Broker doesn't support API | Use cTrader instead |

---

**Your system should now work much better!** 

Try connecting to MetaTrader5 again and you should get **clear, actionable error messages** instead of JSON parsing errors.

See [MT5_TROUBLESHOOTING.md](./MT5_TROUBLESHOOTING.md) for complete guide.
