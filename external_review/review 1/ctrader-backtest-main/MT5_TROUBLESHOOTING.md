# MetaTrader 5 Connection Troubleshooting

## Error: "Unexpected token '<', "<!doctype"... is not valid JSON"

This error means the **server is returning HTML instead of JSON**, which indicates an unhandled server error.

---

## 🔧 Troubleshooting Steps

### Step 1: Check if MetaTrader5 Module is Installed

**Run this in your terminal:**

```bash
pip install MetaTrader5
```

**Verify installation:**

```bash
python -c "import MetaTrader5; print('✓ MetaTrader5 installed')"
```

### Step 2: Check Broker-Specific MT5 Compatibility

Some brokers provide **modified versions of MetaTrader5** that may not support Python API connectivity.

**Check:**
- Is this from the official MetaTrader developer or from your broker?
- Does your broker's documentation mention Python API support?

**If it's broker-specific MT5:**
- Contact broker support to ask about Python API compatibility
- They may not support this feature

### Step 3: Verify MetaTrader5 Terminal is Running

The Python module **requires** MetaTrader5 terminal to be running.

**Check:**
```
1. Make sure MetaTrader5 is OPEN
2. Make sure you're LOGGED IN to your account
3. Keep it running in the background
```

### Step 4: Use the Diagnostic Tool

**In Dashboard**, open browser console (F12) and run:

```javascript
checkDiagnostics()
```

This will show:
- ✅ Is MetaTrader5 module installed?
- ✅ Python version compatibility
- ✅ What's currently connected

**Look for:**
```
metatrader5_details: {
  module_available: true,    // ← Should be true
  version: "..."
}
```

### Step 5: Check Flask Server Logs

When you try to connect, **check the Flask server console** for detailed error messages:

```
ERROR:__main__:Broker connection error: ...
```

This will show the actual problem.

---

## Common Issues & Fixes

### Issue 1: "MetaTrader5 module not installed"

**Error:**
```
MetaTrader5 Python module not installed. 
Run: pip install MetaTrader5
```

**Fix:**
```bash
pip install MetaTrader5
# Then restart Flask server
python server.py
```

### Issue 2: "Failed to connect to MetaTrader5"

**Possible causes:**

1. **MT5 terminal not running**
   - ✓ Open MetaTrader5
   - ✓ Log in to your account
   - ✓ Keep it open while using dashboard

2. **Wrong account ID**
   - ✓ In MT5: View menu → Account Information
   - ✓ Copy exact account number
   - ✓ Paste into dashboard

3. **Broker-specific MT5 doesn't support Python**
   - ✓ Some brokers disable Python API
   - ✓ Try using cTrader instead (if available)
   - ✓ Contact broker support

4. **Python version mismatch**
   - ✓ Check Python version: `python --version`
   - ✓ MetaTrader5 requires Python 3.6+
   - ✓ Update if needed

### Issue 3: Symbol Not Found

**Error:**
```
Spec not found for EURUSD
```

**Fixes:**
- ✓ Verify symbol name matches MT5 (e.g., "EURUSD" not "EUR/USD")
- ✓ Ensure symbol exists in your broker's MT5
- ✓ Check symbol is enabled for trading

---

## System Diagnostic Checklist

Use this to debug the issue:

```bash
# 1. Check Python version
python --version
# Should be 3.6 or higher

# 2. Check MetaTrader5 installation
pip show MetaTrader5
# Should show version and location

# 3. Test MetaTrader5 import
python -c "import MetaTrader5; print(MetaTrader5.version)"

# 4. Check Flask is running
# Server should show: "Running on http://127.0.0.1:5000"

# 5. Check broker diagnostics
# Open dashboard, press F12, run: checkDiagnostics()
```

---

## Browser Console Debugging

### Open Browser Console (F12)

1. Press **F12** to open Developer Tools
2. Go to **Console** tab
3. Run diagnostic:

```javascript
// Check MetaTrader5 availability
checkDiagnostics()

// Check server connectivity
fetch('/api/broker/diagnose').then(r => r.json()).then(d => console.log(d))
```

### What to Look For

**✅ Good response:**
```javascript
{
  modules: {
    metatrader5: "installed"  // ← Good!
  },
  brokers: {
    connected: [],
    active: null
  }
}
```

**❌ Bad response:**
```javascript
{
  modules: {
    metatrader5: "NOT installed"  // ← Problem!
  }
}
```

---

## For Broker-Specific MetaTrader5

If you're using a **broker-provided MetaTrader5** instead of official:

### Option A: Check with Broker

```
Contact your broker's support:
"Does your MetaTrader5 support Python API (MetaTrader5 module)?"

If NO → Cannot use Python connectivity
If YES → Ask for documentation/setup guide
```

### Option B: Use cTrader Instead

If your broker offers **cTrader**, switch to that:

1. Go to Broker Settings
2. Select **cTrader** (not MetaTrader5)
3. Get API credentials from cTrader OpenAPI
4. Connect using cTrader

### Option C: Use Official MetaTrader5

Download official MT5 from MetaTrader website:
1. Close broker's MT5
2. Download from https://www.metatrader5.com
3. Log in with your account
4. Try again

---

## Alternative Solutions

If MetaTrader5 won't work, you have options:

### Option 1: Use cTrader
✅ Better Python support
✅ Faster API
✅ Cloud-based (always available)

**Get cTrader API credentials:**
- https://spotware.com/open-api
- Takes 5 minutes to set up

### Option 2: Use Manual Configuration
✅ Works without brokers API
✅ Just requires broker specs

**Manual steps:**
1. Get specs from broker (margin, contract size, etc.)
2. Enter in dashboard or config file
3. Run backtest

### Option 3: Request Broker Support
Some brokers provide MT5 with better Python support if you request it.

---

## Error Messages Reference

| Error | Cause | Fix |
|-------|-------|-----|
| "module not installed" | pip install missing | `pip install MetaTrader5` |
| "terminal not running" | MT5 is closed | Open MT5 terminal |
| "account not logged in" | Not authenticated | Log in to MT5 |
| "symbol not found" | Wrong symbol name | Use MT5 symbol format |
| "initialization failed" | MT5 compatibility issue | Check MT5 version |
| "permission denied" | Module access blocked | Run as admin (if needed) |

---

## Get Help

### Check These Resources

1. **Server Logs** - Run Flask server and watch terminal
   ```bash
   python server.py  # Watch for ERROR messages
   ```

2. **Browser Console** - Press F12 and check for JS errors

3. **Diagnostics** - Run `checkDiagnostics()` in console

4. **Official Docs** - https://www.mql5.com/en/docs/integration/python

---

## Quick Fix Summary

```
✅ MetaTrader5 open?        → Keep it running
✅ Logged in?               → Check account
✅ Module installed?        → pip install MetaTrader5
✅ Account ID correct?      → Match MT5 exactly
✅ Broker supports API?     → Ask broker support
✅ Still failing?           → Use cTrader instead
```

---

## Next Steps

### If Issue Persists

1. Open terminal where Flask is running
2. Look for ERROR messages
3. Share the error message
4. Try cTrader as alternative

### If You Want to Switch Brokers

1. Dashboard → Broker Settings
2. Select **cTrader**
3. Get free API credentials from Spotware
4. Takes 5 minutes to set up

### If You Need Manual Specs

1. Get specs from broker website
2. Enter manually in dashboard
3. Run backtest normally

---

**Need help?** Check Flask server console for detailed error logs when connecting.
