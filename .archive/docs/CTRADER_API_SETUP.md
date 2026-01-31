# cTrader OpenAPI Credentials Setup Guide

This guide shows you exactly how to get your cTrader API Key and API Secret for use with the broker integration.

## Step 1: Access Spotware Open API Portal

1. Go to https://spotware.com/openapi/ (OpenAPI Developer Portal)
2. Click **"Create Account"** or **"Sign In"** if you already have a Spotware account

## Step 2: Create/Sign Into Your Spotware Account

If you don't have one:
- Click **"Create Account"**
- Enter your email and create a password
- Verify your email address
- Fill in your details

If you have one:
- Sign in with your credentials

## Step 3: Create a New Application

1. In the OpenAPI portal dashboard, look for **"My Applications"** or **"Applications"**
2. Click **"Create New Application"** or **"+"** button
3. Fill in the application details:
   - **Application Name**: e.g., "cTrader Backtest Engine"
   - **Application Type**: Select **"Desktop"** (most common) or **"Web"** if applicable
   - **Description**: e.g., "Backtesting and automated trading system"
   - **Redirect URI** (if using Web type): Use `http://localhost:5000/api/callback` or your app's callback URL

4. Click **"Create"** or **"Submit"**

## Step 4: Get Your API Credentials

Once your application is created, you'll see a page with:

- **Client ID** (also called API Key)
- **Client Secret** (also called API Secret)

### ⚠️ Important: Save These Immediately

These credentials are sensitive and typically shown only once. Copy them to a safe location:

1. **Copy Client ID** → This is your `API Key`
2. **Copy Client Secret** → This is your `API Secret`
3. Store these securely (password manager recommended)

## Step 5: Add Your Credentials to the Dashboard

1. Open the broker integration dashboard (usually http://localhost:5000)
2. Go to **"Broker Settings"** or **"Connect Broker"**
3. Select **"cTrader"** from the broker dropdown
4. Enter:
   - **Account ID**: Your cTrader account number (usually shown in your cTrader terminal)
   - **Username**: Your cTrader login username
   - **Password**: Your cTrader password
   - **API Key**: Paste the Client ID from Step 4
   - **API Secret**: Paste the Client Secret from Step 4

5. Click **"Connect"**

## Step 6: Verify Connection

Check that:
- Connection status shows **"Connected"** (green indicator)
- You can see your account balance
- You can fetch instrument specs

## Getting Account Details

Your **Account ID** can be found in:

1. **cTrader Desktop Terminal**:
   - Open your cTrader terminal
   - Look at the top left corner - Account number appears there
   - Format: Usually a 6-digit number like `12345678`

2. **cTrader Web**:
   - Log in to web.ctrader.com
   - Check "Account Settings" → Account Number

3. **My Accounts Tab**:
   - In your cTrader terminal
   - Under "My Accounts" section

## Demo vs Live Accounts

- **Demo Account**: Use for testing (no real money)
  - Create free demo account in cTrader
  - Get API credentials for demo account
  
- **Live Account**: Use for real trading
  - Only available with real money account
  - API credentials work with live market data

### Recommended: Test with Demo First
1. Create a demo cTrader account
2. Get demo API credentials
3. Test the connection and backtests
4. Only use live account when confident

## Troubleshooting

### "Invalid API Key/Secret"
- Check you copied the entire Client ID and Client Secret
- Verify no extra spaces at beginning/end
- Confirm application is still active in portal

### "Authentication Failed"
- Verify your username and password are correct
- Check Account ID matches your cTrader terminal
- Try resetting password in cTrader

### "No Data Available"
- Ensure your cTrader account is not on holiday pause
- Verify the market is open (forex/indices trading hours)
- Check your internet connection

### "Application Not Found"
- Go back to https://spotware.com/open-api
- Verify the application is listed in "My Applications"
- Regenerate credentials if needed

## API Documentation

For technical details about the cTrader OpenAPI:
- **Official Docs**: https://help.spotware.com/open-api
- **GitHub Examples**: https://github.com/spotware/openapi-connector
- **Support**: Contact Spotware directly via their portal

## Security Best Practices

1. **Never share your API Secret** publicly
2. **Store credentials securely** (use password manager)
3. **Rotate credentials** periodically
4. **Use IP whitelisting** if available in cTrader settings
5. **Enable 2FA** on your Spotware account
6. **Monitor API usage** in the portal

## Next Steps

Once connected:
1. Go to **"Fetch Instrument Specs"** to get symbol data
2. Configure your trading strategy
3. Run backtests with real market data
4. Monitor the diagnostic console if issues occur

---

**Need Help?**
- Check the dashboard error messages (red alerts)
- Open browser DevTools (F12) → Console tab for detailed errors
- Review the server logs for connection debugging
- Contact Spotware support at https://spotware.com/help
