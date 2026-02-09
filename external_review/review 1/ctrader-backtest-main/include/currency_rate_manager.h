#ifndef CURRENCY_RATE_MANAGER_H
#define CURRENCY_RATE_MANAGER_H

#include "currency_converter.h"
#include <string>
#include <map>
#include <vector>
#include <ctime>

/**
 * CurrencyRateManager - Manage conversion rates for cross-currency calculations
 *
 * This class:
 * - Determines which rates are needed for a symbol/account combination
 * - Queries those rates from the broker
 * - Updates the CurrencyConverter with current rates
 * - Handles rate caching and updates
 */
class CurrencyRateManager {
private:
    std::string account_currency_;
    std::map<std::string, double> cached_rates_;  // currency -> rate
    std::map<std::string, time_t> rate_timestamps_;  // currency -> last update time
    int cache_expiry_seconds_;  // How long rates are valid

public:
    /**
     * Constructor
     * @param account_currency Account base currency (e.g., "USD")
     * @param cache_expiry_seconds How long to cache rates (default: 60 seconds)
     */
    explicit CurrencyRateManager(
        const std::string& account_currency,
        int cache_expiry_seconds = 60
    ) : account_currency_(account_currency),
        cache_expiry_seconds_(cache_expiry_seconds) {}

    /**
     * Determine which conversion rates are needed for a symbol
     *
     * @param symbol_base Symbol's base currency (e.g., "GBP" from GBPJPY)
     * @param symbol_quote Symbol's quote currency (e.g., "JPY" from GBPJPY)
     * @return Vector of currency pairs to query (e.g., ["GBPUSD", "USDJPY"])
     */
    std::vector<std::string> GetRequiredConversionPairs(
        const std::string& symbol_base,
        const std::string& symbol_quote
    ) const {
        std::vector<std::string> required_pairs;

        // Margin conversion: symbol_base → account_currency
        if (symbol_base != account_currency_) {
            std::string pair = GetConversionPair(symbol_base, account_currency_);
            if (!pair.empty()) {
                required_pairs.push_back(pair);
            }
        }

        // Profit conversion: symbol_quote → account_currency
        if (symbol_quote != account_currency_) {
            std::string pair = GetConversionPair(symbol_quote, account_currency_);
            if (!pair.empty()) {
                required_pairs.push_back(pair);
            }
        }

        return required_pairs;
    }

    /**
     * Determine the symbol name for a currency conversion
     *
     * Examples:
     * - GetConversionPair("EUR", "USD") → "EURUSD"
     * - GetConversionPair("JPY", "USD") → "USDJPY"
     * - GetConversionPair("GBP", "USD") → "GBPUSD"
     *
     * @param from_currency Source currency
     * @param to_currency Target currency
     * @return Symbol name for conversion (e.g., "EURUSD", "USDJPY")
     */
    std::string GetConversionPair(
        const std::string& from_currency,
        const std::string& to_currency
    ) const {
        if (from_currency == to_currency) {
            return "";  // No conversion needed
        }

        // Standard forex pair naming:
        // - Major currencies typically have USD as quote (EURUSD, GBPUSD)
        // - Exceptions: USD is base for JPY, CHF, CAD (USDJPY, USDCHF, USDCAD)

        // If converting TO USD
        if (to_currency == "USD") {
            // Check if from_currency is a "USD base" currency
            if (from_currency == "JPY" || from_currency == "CHF" || from_currency == "CAD") {
                return "USD" + from_currency;  // e.g., "USDJPY"
            }
            return from_currency + "USD";  // e.g., "EURUSD", "GBPUSD"
        }

        // If converting FROM USD
        if (from_currency == "USD") {
            // Check if to_currency is a "USD quote" currency
            if (to_currency == "EUR" || to_currency == "GBP" || to_currency == "AUD" || to_currency == "NZD") {
                return to_currency + "USD";  // e.g., "EURUSD"
            }
            return "USD" + to_currency;  // e.g., "USDJPY", "USDCHF"
        }

        // For cross pairs (neither is USD)
        // Format: base + quote (e.g., EURJPY, GBPJPY)
        return from_currency + to_currency;
    }

    /**
     * Update cached rate for a currency
     *
     * @param currency Currency code (e.g., "EUR", "GBP", "JPY")
     * @param rate Exchange rate to account currency
     */
    void UpdateRate(const std::string& currency, double rate) {
        if (rate > 0) {
            cached_rates_[currency] = rate;
            rate_timestamps_[currency] = std::time(nullptr);
        }
    }

    /**
     * Update cached rate from a symbol price
     *
     * @param symbol Symbol name (e.g., "EURUSD", "GBPJPY")
     * @param bid Current bid price
     */
    void UpdateRateFromSymbol(const std::string& symbol, double bid) {
        // Parse symbol to extract currencies
        // Examples:
        // - EURUSD → base=EUR, quote=USD
        // - GBPJPY → base=GBP, quote=JPY
        // - USDJPY → base=USD, quote=JPY

        if (symbol.length() != 6) {
            return;  // Invalid symbol format
        }

        std::string base = symbol.substr(0, 3);
        std::string quote = symbol.substr(3, 3);

        // Update rate depending on account currency
        if (quote == account_currency_) {
            // Symbol like EURUSD with USD account
            // Rate is direct: 1 EUR = bid USD
            UpdateRate(base, bid);
        } else if (base == account_currency_) {
            // Symbol like USDCHF with USD account
            // Rate is inverse: 1 CHF = 1/bid USD
            UpdateRate(quote, 1.0 / bid);
        } else {
            // Cross-currency pair (e.g., GBPJPY with USD account)
            // Would need additional logic to calculate conversion via USD
            // For now, store as-is
            UpdateRate(base + "/" + quote, bid);
        }
    }

    /**
     * Get cached rate for a currency
     *
     * @param currency Currency code
     * @param is_valid Output parameter indicating if rate is still valid
     * @return Cached rate, or 1.0 if not found/expired
     */
    double GetCachedRate(const std::string& currency, bool* is_valid = nullptr) const {
        auto it = cached_rates_.find(currency);
        if (it == cached_rates_.end()) {
            if (is_valid) *is_valid = false;
            return 1.0;
        }

        // Check if rate has expired
        auto ts_it = rate_timestamps_.find(currency);
        if (ts_it != rate_timestamps_.end()) {
            time_t now = std::time(nullptr);
            time_t age = now - ts_it->second;
            if (age > cache_expiry_seconds_) {
                if (is_valid) *is_valid = false;
                return it->second;  // Return stale rate but mark as invalid
            }
        }

        if (is_valid) *is_valid = true;
        return it->second;
    }

    /**
     * Calculate margin conversion rate for a symbol
     *
     * @param symbol_base Symbol base currency (margin currency)
     * @param symbol_quote Symbol quote currency
     * @param symbol_price Current symbol price
     * @return Conversion rate to multiply margin by
     */
    double GetMarginConversionRate(
        const std::string& symbol_base,
        const std::string& symbol_quote,
        double symbol_price
    ) const {
        if (symbol_base == account_currency_) {
            return 1.0;  // No conversion needed
        }

        // If quote currency matches account, use symbol price directly
        // Example: USD account, EURUSD
        // Margin in EUR, multiply by EURUSD to get USD
        if (symbol_quote == account_currency_) {
            return symbol_price;
        }

        // Cross-currency scenario - need additional rate
        // Example: USD account, GBPJPY
        // Margin in GBP, need GBPUSD rate
        bool is_valid = false;
        double rate = GetCachedRate(symbol_base, &is_valid);
        if (is_valid) {
            return rate;
        }

        // Fallback: return 1.0 (no conversion)
        return 1.0;
    }

    /**
     * Calculate profit conversion rate for a symbol
     *
     * @param symbol_quote Symbol quote currency (profit currency)
     * @param symbol_price Current symbol price (may be used for inverse conversion)
     * @return Conversion rate to divide profit by (for use with ConvertProfit)
     */
    double GetProfitConversionRate(
        const std::string& symbol_quote,
        double symbol_price
    ) const {
        (void)symbol_price;  // Unused parameter, avoid warning

        if (symbol_quote == account_currency_) {
            return 1.0;  // No conversion needed
        }

        // Need to convert quote currency to account currency
        // Example: USD account, GBPJPY
        // Profit in JPY, need USDJPY rate to convert to USD
        // Cached rate for JPY is 1/USDJPY, but ConvertProfit expects USDJPY
        // So return the inverse of the cached rate
        bool is_valid = false;
        double rate = GetCachedRate(symbol_quote, &is_valid);
        if (is_valid && rate > 0) {
            return 1.0 / rate;  // Return inverse for ConvertProfit compatibility
        }

        // Fallback: return 1.0 (no conversion)
        return 1.0;
    }

    /**
     * Clear all cached rates
     */
    void ClearCache() {
        cached_rates_.clear();
        rate_timestamps_.clear();
    }

    /**
     * Get number of cached rates
     */
    size_t GetCacheSize() const {
        return cached_rates_.size();
    }

    /**
     * Check if a rate is cached and valid
     */
    bool HasValidRate(const std::string& currency) const {
        bool is_valid = false;
        GetCachedRate(currency, &is_valid);
        return is_valid;
    }

    /**
     * Get account currency
     */
    const std::string& GetAccountCurrency() const {
        return account_currency_;
    }

    /**
     * Set cache expiry time
     */
    void SetCacheExpiry(int seconds) {
        cache_expiry_seconds_ = seconds;
    }
};

/**
 * Example usage:
 *
 * // Create rate manager for USD account
 * CurrencyRateManager rate_mgr("USD");
 *
 * // Determine which rates are needed for GBPJPY
 * auto required_pairs = rate_mgr.GetRequiredConversionPairs("GBP", "JPY");
 * // Returns: ["GBPUSD", "USDJPY"]
 *
 * // Query those symbols from broker
 * for (const auto& pair : required_pairs) {
 *     MTSymbol symbol = connector.GetSymbolInfo(pair);
 *     rate_mgr.UpdateRateFromSymbol(pair, symbol.bid);
 * }
 *
 * // Get conversion rates
 * double margin_rate = rate_mgr.GetMarginConversionRate("GBP", "JPY", gbpjpy_price);
 * double profit_rate = rate_mgr.GetProfitConversionRate("JPY", gbpjpy_price);
 *
 * // Use rates in currency converter
 * double margin_usd = converter.ConvertMargin(margin_gbp, "GBP", margin_rate);
 * double profit_usd = converter.ConvertProfit(profit_jpy, "JPY", profit_rate);
 */

#endif // CURRENCY_RATE_MANAGER_H
