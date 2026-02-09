#!/usr/bin/env python3
"""
Quick test to verify price history functionality
This test checks that all components are in place
"""

import sys
import json
from pathlib import Path

def test_broker_api_methods():
    """Test that broker API has fetch_price_history method"""
    print("Testing broker_api.py...")
    try:
        # Check broker_api.py file exists
        broker_api_path = Path("broker_api.py")
        if not broker_api_path.exists():
            print("✗ broker_api.py not found")
            return False
        
        # Read file and check for method definitions
        with open(broker_api_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Check for abstract method
        if "def fetch_price_history" not in content:
            print("✗ fetch_price_history method not found in BrokerAPI")
            return False
        
        # Check for CTraderAPI implementation
        if "class CTraderAPI" not in content:
            print("✗ CTraderAPI class not found")
            return False
        
        # Check for MetaTrader5API implementation
        if "class MetaTrader5API" not in content:
            print("✗ MetaTrader5API class not found")
            return False
        
        # Check BrokerManager has the method
        if 'def fetch_price_history(self, symbol' not in content:
            print("✗ fetch_price_history not in BrokerManager")
            return False
        
        print("✓ broker_api.py has all required methods")
        return True
        
    except Exception as e:
        print(f"✗ Error checking broker_api.py: {e}")
        return False

def test_server_endpoint():
    """Test that server has the API endpoint"""
    print("\nTesting server.py...")
    try:
        server_path = Path("server.py")
        if not server_path.exists():
            print("✗ server.py not found")
            return False
        
        with open(server_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Check for price_history endpoint
        if "/api/broker/price_history" not in content:
            print("✗ /api/broker/price_history endpoint not found")
            return False
        
        # Check for timeframe validation
        if "timeframe" not in content and "price_history" in content:
            print("✗ timeframe parameter not found in price_history function")
            return False
        
        print("✓ server.py has price_history endpoint")
        return True
        
    except Exception as e:
        print(f"✗ Error checking server.py: {e}")
        return False

def test_ui_components():
    """Test that UI has the necessary components"""
    print("\nTesting ui/index.html...")
    try:
        ui_path = Path("ui") / "index.html"
        if not ui_path.exists():
            print("✗ ui/index.html not found")
            return False
        
        with open(ui_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Check for price chart panel
        if "Price History Chart" not in content:
            print("✗ Price History Chart panel not found")
            return False
        
        # Check for timeframe selector
        if "priceTimeframe" not in content:
            print("✗ priceTimeframe selector not found")
            return False
        
        # Check for chart container
        if "priceChart" not in content:
            print("✗ priceChart canvas not found")
            return False
        
        # Check for load button
        if "fetchPriceHistory()" not in content:
            print("✗ fetchPriceHistory() button not found")
            return False
        
        print("✓ ui/index.html has all required components")
        return True
        
    except Exception as e:
        print(f"✗ Error checking ui/index.html: {e}")
        return False

def test_javascript_functions():
    """Test that dashboard.js has the required functions"""
    print("\nTesting ui/dashboard.js...")
    try:
        js_path = Path("ui") / "dashboard.js"
        if not js_path.exists():
            print("✗ ui/dashboard.js not found")
            return False
        
        with open(js_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Check for fetchPriceHistory function
        if "async function fetchPriceHistory()" not in content:
            print("✗ fetchPriceHistory() function not found")
            return False
        
        # Check for renderCandlestickChart function
        if "function renderCandlestickChart(" not in content:
            print("✗ renderCandlestickChart() function not found")
            return False
        
        # Check for Chart.js usage
        if "new Chart(ctx" not in content:
            print("✗ Chart.js instantiation not found")
            return False
        
        # Check for API call
        if "/api/broker/price_history/" not in content:
            print("✗ API call to price_history endpoint not found")
            return False
        
        print("✓ ui/dashboard.js has all required functions")
        return True
        
    except Exception as e:
        print(f"✗ Error checking ui/dashboard.js: {e}")
        return False

def main():
    print("=" * 60)
    print("Price History & Candlestick Charts - Component Test")
    print("=" * 60)
    
    tests = [
        test_broker_api_methods,
        test_server_endpoint,
        test_ui_components,
        test_javascript_functions
    ]
    
    results = [test() for test in tests]
    
    print("\n" + "=" * 60)
    passed = sum(results)
    total = len(results)
    print(f"Results: {passed}/{total} tests passed")
    print("=" * 60)
    
    if all(results):
        print("\n✓ All components are in place!")
        print("\nTo test the feature:")
        print("1. Start the server: python server.py")
        print("2. Connect to a broker in the UI")
        print("3. Load instruments")
        print("4. Select an instrument")
        print("5. Choose a timeframe")
        print("6. Click 'Load Chart'")
        return 0
    else:
        print("\n✗ Some components are missing. Check errors above.")
        return 1

if __name__ == "__main__":
    sys.exit(main())
