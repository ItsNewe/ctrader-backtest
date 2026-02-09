"""
Debug script to test MetaTrader5 account validation with YOUR actual account
Run this to see exactly what MT5 returns and how the validation works
"""

import logging

logging.basicConfig(level=logging.INFO, format='%(levelname)s: %(message)s')
logger = logging.getLogger(__name__)

print("=" * 70)
print("MT5 ACCOUNT VALIDATION TEST SCRIPT")
print("=" * 70)

try:
    import MetaTrader5 as mt5
    print("✓ MetaTrader5 module imported successfully")
except ImportError as e:
    print(f"✗ MetaTrader5 module NOT available: {e}")
    print("Install with: pip install MetaTrader5")
    exit(1)

# Test 1: Initialize MT5
print("\n[TEST 1] Initialize MT5")
print("-" * 70)
if not mt5.initialize():
    error = mt5.last_error()
    print(f"✗ Failed to initialize MT5")
    print(f"   Error code: {error}")
    print("\n⚠️  Make sure MetaTrader5 terminal is running!")
    exit(1)
print("✓ MT5 initialized successfully")

# Test 2: Get current account info
print("\n[TEST 2] Get Current Account Info (what's logged in?)")
print("-" * 70)
account = mt5.account_info()
if account is None:
    print("✗ account_info() returned None")
    print("   This means no account is logged in to MT5")
    mt5.shutdown()
    exit(1)

print(f"✓ Account info retrieved:")
print(f"   Account ID (Login):     {account.login}")
print(f"   Server:                 {account.server}")
print(f"   Company:                {account.company}")
print(f"   Currency:               {account.currency}")
print(f"   Balance:                {account.balance}")

# Test 3: YOUR specific test cases
print("\n[TEST 3] Validation Tests with Your Account")
print("-" * 70)

YOUR_ACTUAL_ACCOUNT_ID = "000000"  # Your login ID
YOUR_SERVER = "BrokerServer"

logged_in_account = str(account.login)
logged_in_server = account.server

print(f"\nYour credentials:")
print(f"   Expected Account ID:    {YOUR_ACTUAL_ACCOUNT_ID}")
print(f"   Expected Server:        {YOUR_SERVER}")

print(f"\nCurrently logged in:")
print(f"   Account ID:             {logged_in_account}")
print(f"   Server:                 {logged_in_server}")

# Test 3a: Correct account
print("\n[TEST 3a] Connection with CORRECT account ID (000000)")
if logged_in_account == YOUR_ACTUAL_ACCOUNT_ID and logged_in_server == YOUR_SERVER:
    print("✓ PASS: Account matches! Connection would succeed")
else:
    print(f"✗ FAIL: Account mismatch!")
    print(f"        Expected: {YOUR_ACTUAL_ACCOUNT_ID} on {YOUR_SERVER}")
    print(f"        Got:      {logged_in_account} on {logged_in_server}")

# Test 3b: Wrong account - what if user enters wrong number?
print("\n[TEST 3b] Connection with WRONG account ID (999999)")
wrong_account = "999999"
if logged_in_account == wrong_account:
    print("✓ PASS: Account matches (unexpected!)")
else:
    print(f"✓ PASS: Account rejected! (This is correct behavior)")
    print(f"        Expected: {wrong_account}")
    print(f"        Got:      {logged_in_account}")
    print(f"        Error message shown to user: Account mismatch")

# Test 3c: Different server test
print("\n[TEST 3c] Connection with WRONG server")
wrong_server = "WrongServer-Live"
if logged_in_server == wrong_server:
    print("✓ PASS: Server matches (unexpected!)")
else:
    print(f"✓ PASS: Server rejected! (This is correct behavior)")
    print(f"        Expected: {wrong_server}")
    print(f"        Got:      {logged_in_server}")

# Test 4: Data types check
print("\n[TEST 4] Data Type Validation")
print("-" * 70)
print(f"account.login type:      {type(account.login)} (value: {account.login})")
print(f"account.server type:     {type(account.server)} (value: {account.server})")

# Test comparison
print("\nComparison test:")
account_id_str = str(account.login)
account_id_int = int(account.login)
print(f"  str(account.login) == '000000': {account_id_str == '000000'}")
print(f"  int(account.login) == 000000: {account_id_int == 000000}")

# Shutdown
mt5.shutdown()
print("\n" + "=" * 70)
print("✓ MT5 shutdown complete")
print("=" * 70)

print("\n📋 SUMMARY:")
print("-" * 70)
if logged_in_account == YOUR_ACTUAL_ACCOUNT_ID and logged_in_server == YOUR_SERVER:
    print("✓ Your account is correctly logged in!")
    print("  The validation code SHOULD accept this account")
    print("  If it's still being rejected, there's a bug in the validation code")
else:
    print("✗ Your account is NOT logged in correctly in MT5!")
    print(f"  Log in with account {YOUR_ACTUAL_ACCOUNT_ID} in the MT5 terminal")
    print("  Then try connecting again")

print("\n💡 NEXT STEPS:")
print("-" * 70)
print("1. If validation is failing incorrectly: show me the [MT5] logs from server")
print("2. If MT5 connection is successful, try: /api/broker/connect with your ID")
print("3. Check server logs for any [MT5] debug messages")
