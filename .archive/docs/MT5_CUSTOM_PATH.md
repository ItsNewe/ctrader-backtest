# MetaTrader5 Custom Installation Path Guide

If your MetaTrader5 terminal is installed in a custom location (not the default), you need to provide the path in the broker connection settings.

## Common MT5 Installation Paths

### Standard MetaTrader5 (Official)
- **Default**: `C:\Program Files\MetaTrader 5`
- **Alt 32-bit**: `C:\Program Files (x86)\MetaTrader 5`

### Broker-Branded MetaTrader5

****
```
C:\Program Files\ MetaTrader 5\terminal64.exe
```

**FXCM**
```
C:\Program Files\FXCM\MetaTrader 5\terminal64.exe
```

**Pepperstone**
```
C:\Program Files\Pepperstone\MetaTrader 5\terminal64.exe
```

**IC Markets**
```
C:\Program Files\IC Markets\MetaTrader 5\terminal64.exe
```

**Other Brokers**
- Check your desktop shortcut → right-click → Properties → Look at "Target" field
- Or check: `C:\Program Files\[YourBroker]\MetaTrader 5\`

## How to Find Your MT5 Installation Path

### Method 1: Check Desktop Shortcut
1. Right-click on your MetaTrader5 desktop icon
2. Select **"Properties"**
3. Look for **"Target:"** field
4. Copy the path (usually ends with `terminal64.exe` or `terminal.exe`)

**Example:**
```
"C:\Program Files\ MetaTrader 5\terminal64.exe"
```

Remove the quotes and `.exe` part if needed—you can provide either:
- Full path to executable: `C:\Program Files\ MetaTrader 5\terminal64.exe`
- Path to folder: `C:\Program Files\ MetaTrader 5`

### Method 2: Check Program Files Folder
1. Open **File Explorer**
2. Navigate to `C:\Program Files\`
3. Look for MetaTrader5-related folder
4. Check the folder name (usually contains "MetaTrader" or your broker name)
5. Look inside for `terminal64.exe` or `terminal.exe`

### Method 3: Check Start Menu
1. Open **Windows Start Menu**
2. Search for "MetaTrader"
3. Right-click the result → **"Open file location"**
4. Note the folder path shown

## Using Custom Path in Dashboard

Once you find your MT5 installation path:

1. Open the **Broker Settings** section in the dashboard
2. Select **"MetaTrader 5"** from the Broker dropdown
3. Fill in:
   - **Account Type**: Demo or Live
   - **Account ID**: Your MT5 account number
   - **Leverage**: Usually 100-500
   - **Account Currency**: USD (default)

4. **New Field - MT5 Terminal Path** (Optional):
   ```
   C:\Program Files\ MetaTrader 5\terminal64.exe
   ```
   Or just the folder:
   ```
   C:\Program Files\ MetaTrader 5
   ```

5. Click **"🔗 Connect Broker"**

## Troubleshooting

### "Failed to initialize MT5 at path: [path]"
- **Check**: Is the path spelled correctly?
- **Check**: Does `terminal64.exe` actually exist at that location?
- **Fix**: Copy-paste from File Explorer to avoid typos

### "Account not found"
- **Check**: Is MetaTrader5 running? (icon should be in system tray)
- **Check**: Is the correct account logged in? (check MT5 window)
- **Check**: Account ID matches what's shown in MT5

### "Module not installed" (despite custom path)
- This means the **Python MetaTrader5 module** is missing
- Not related to the path—run: `pip install MetaTrader5`

### Path with Spaces Not Working
- **Make sure**: Spaces are preserved: `C:\Program Files\ MetaTrader 5\`
- **Don't use**: Quotes in the input field
- **Test**: Copy-paste directly from File Explorer

## Example Configurations

###  MT5
```
Path: C:\Program Files\ MetaTrader 5\terminal64.exe
Account Type: Demo
Account ID: [your account number]
Leverage: 500
Currency: USD
```

### Standard MetaTrader5
```
Path: C:\Program Files\MetaTrader 5
Account Type: Live
Account ID: [your account number]
Leverage: 100
Currency: USD
```

### Pepperstone MT5
```
Path: C:\Program Files\Pepperstone\MetaTrader 5\terminal64.exe
Account Type: Live
Account ID: [your account number]
Leverage: 200
Currency: USD
```

## What Happens Without a Custom Path?

If you leave the **MT5 Terminal Path** field empty:
- The system will try common default locations
- Works for standard MetaTrader5 installations
- May fail for broker-branded terminals

**When You NEED to Provide Path:**
- Broker-branded MT5 (, FXCM, etc.)
- Custom installation location
- Multiple MT5 installations on same machine
- 64-bit vs 32-bit version mismatch

## Next Steps

Once connected:
1. Click **"📥 Fetch Specs"** to load instrument data
2. Select symbols for backtesting
3. Configure and run backtests

---

**Need Help?**
- Check the server console (Flask output) for detailed error messages
- Open DevTools (F12) → Console tab to see frontend errors
- Verify MT5 is running with correct account logged in
