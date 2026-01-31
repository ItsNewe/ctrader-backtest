# Bypass Permission Prompts Guide

## MT5 "Allow" Prompts - How to Disable

### Issue:
MT5 asks "Allow this Expert Advisor to..." every time you run backtests in Strategy Tester.

---

## Solution 1: MT5 Auto Trading Permissions (RECOMMENDED)

**Step 1: Enable AutoTrading Globally**
1. In MT5, click **Tools → Options** (or press Ctrl+O)
2. Go to **Expert Advisors** tab
3. Enable these checkboxes:
   - ✅ **Allow automated trading**
   - ✅ **Allow DLL imports** (if EA uses DLLs)
   - ✅ **Allow WebRequest for listed URL** (if EA uses web)
4. Click **OK**

**Step 2: Disable Confirmation Prompts**
1. In the same **Expert Advisors** tab
2. Find: **"Confirm DLL function calls"**
3. **UNCHECK** this option
4. Click **OK**

This should eliminate most prompts.

---

## Solution 2: Add EA to Trusted List

**If prompts still appear:**

1. **Open MT5 Data Folder**
   - Click **File → Open Data Folder** in MT5
   - Navigate to `MQL5\Experts\`

2. **EA Properties**
   - Right-click `test_a_sl_tp_order.ex5`
   - Select **Properties**
   - Go to **Security** tab
   - Click **Edit**
   - Select your user account
   - Check **Full Control**
   - Click **OK**

3. **Compile with Trusted Settings**
   - In MetaEditor, open the `.mq5` file
   - Add at the top:
   ```mql5
   #property strict
   #property copyright "Trusted"
   ```
   - Recompile (F7)

---

## Solution 3: Strategy Tester Specific Settings

**For Strategy Tester:**

1. In **Strategy Tester** window
2. Click **Settings** (gear icon)
3. Under **Expert Properties**:
   - Set **"Allow DLL imports"**: `true`
   - Set **"Allow external experts imports"**: `true`
4. These settings are saved per EA

---

## Solution 4: Windows UAC (If Python/Terminal Prompts)

**If you're getting Windows UAC prompts when running Python scripts:**

### Option A: Run as Administrator (Quick Fix)
1. Right-click Command Prompt or Terminal
2. Select **Run as administrator**
3. Navigate to project directory
4. Run Python scripts - no more prompts

### Option B: Disable UAC for Specific Apps (Better)
1. Press **Win+R**, type `taskschd.msc`, press Enter
2. Click **Create Task** (not Basic Task)
3. **General** tab:
   - Name: `Python No UAC`
   - ✅ **Run with highest privileges**
4. **Actions** tab:
   - New → Start a program
   - Program: `C:\Users\user\AppData\Local\Programs\Python\Python313\python.exe`
   - Arguments: `validation\run_step1_mt5_export.py`
   - Start in: `C:\Users\user\Documents\ctrader-backtest`
5. **Conditions/Settings**: Adjust as needed
6. Click **OK**

Now you can run this task without UAC prompts.

### Option C: Disable UAC Entirely (NOT RECOMMENDED for security)
1. Press **Win+R**, type `UserAccountControlSettings`, press Enter
2. Move slider to **Never notify**
3. Click **OK**, restart computer

**WARNING:** This reduces Windows security. Only do this on development machines.

---

## Solution 5: Create Batch File with Elevated Privileges

**Create:** `run_validation.bat`
```batch
@echo off
cd /d C:\Users\user\Documents\ctrader-backtest
python validation\run_step1_mt5_export.py
pause
```

**Make it always run elevated:**
1. Right-click `run_validation.bat` → **Create Shortcut**
2. Right-click the shortcut → **Properties**
3. Click **Advanced**
4. ✅ Check **Run as administrator**
5. Click **OK**

Now double-click the shortcut - runs elevated automatically.

---

## For Your Specific Case: MT5 Strategy Tester

**The issue you're experiencing is likely MT5 asking for EA permissions.**

### Quick Fix (Do This Now):

1. **In MT5:** Tools → Options → Expert Advisors
2. **Uncheck:** "Confirm DLL function calls"
3. **Check:** "Allow automated trading"
4. Click **OK**

5. **In Strategy Tester:**
   - When configuring the test
   - Look for **EA Properties** or **Settings**
   - Enable **"Allow DLL imports"** if present
   - Enable **"Allow external experts imports"**

6. **Run test once more**
   - Click "Allow" and check "Don't ask again"
   - Should remember for this EA

### If Still Asking:

**The EA might be using features that require explicit permission:**

Let me check our EA:

```mql5
// Check if EA uses any protected functions:
- FileOpen() - YES (writes CSV)
- WebRequest() - NO
- DLL imports - NO
- External libraries - NO
```

**Our EA only uses FileOpen** which should be safe.

### Alternative: Sign the EA

**If MT5 keeps asking despite settings:**

1. **Add signature to EA**
   - In MetaEditor, open `test_a_sl_tp_order.mq5`
   - Add at top:
   ```mql5
   #property strict
   #property copyright "YourName"
   #property link      "https://yourdomain.com"
   #property version   "1.00"
   ```
   - Recompile (F7)

2. **Trusted sources list**
   - Tools → Options → Expert Advisors
   - Click **"Expert Advisors"** button
   - Add your copyright name to trusted list

---

## Testing if It Worked

**Run this to verify no more prompts:**

```bash
python validation/run_step1_mt5_export.py
```

**Then in MT5:**
1. Open Strategy Tester (Ctrl+R)
2. Select `test_a_sl_tp_order`
3. Configure and click **Start**
4. **Should NOT ask for permission**

---

## Summary: Quick Steps

**To eliminate MT5 prompts:**
1. ✅ Tools → Options → Expert Advisors
2. ✅ Uncheck "Confirm DLL function calls"
3. ✅ Check "Allow automated trading"
4. ✅ In Strategy Tester: Enable DLL imports in settings
5. ✅ Run test once, click "Allow and don't ask again"

**To eliminate Python/Windows prompts:**
1. ✅ Run terminal as Administrator
2. ✅ Or create scheduled task with elevated privileges
3. ✅ Or disable UAC (not recommended)

---

## What I Recommend for You

**Safest approach:**

1. **For MT5:** Disable "Confirm DLL function calls" in Options
2. **For Python:** Run Command Prompt as Administrator when doing validation
3. **First run:** Click "Allow and don't ask again" for the EA
4. **After that:** Should be silent

**This maintains security while eliminating annoying prompts.**

---

Let me know if prompts still appear after trying these steps!
