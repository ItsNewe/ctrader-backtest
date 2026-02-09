#ifndef CURRENCY_CONVERTER_H
#define CURRENCY_CONVERTER_H

#include <string>
#include <map>
#include <cmath>

/**
 * CurrencyConverter - Handle cross-currency margin and profit calculations
 *
 * When account currency differs from symbol's base/profit currency,
 * margin and profit calculations require currency conversion.
 *
 * Examples:
 * - USD account trading EURUSD: Need EUR/USD rate for margin
 * - EUR account trading USDJPY: Need USD/EUR rate for margin
 * - GBP account trading EURUSD: Need EUR/GBP for margin, USD/GBP for profit
 */
class CurrencyConverter {
private:
    std::map<std::string, double> rates_;  // Conversion rates cache
    std::string base_currency_;             // Account currency

public:
    /**
     * Constructor
     * @param account_currency Account base currency (e.g., "USD", "EUR")
     */
    explicit CurrencyConverter(const std::string& account_currency)
        : base_currency_(account_currency) {
        // Self-conversion rate is always 1.0
        rates_[account_currency] = 1.0;
    }

    /**
     * Set conversion rate from account currency to target currency
     *
     * @param currency Target currency code (e.g., "EUR", "GBP")
     * @param rate Exchange rate (e.g., 1.20 means 1 USD = 1.20 EUR)
     */
    void SetRate(const std::string& currency, double rate) {
        if (rate > 0) {
            rates_[currency] = rate;
        }
    }

    /**
     * Get conversion rate to account currency
     *
     * @param from_currency Currency to convert from
     * @param to_account If true, convert TO account currency; if false, FROM account
     * @return Conversion rate, or 1.0 if same currency or rate not found
     */
    double GetRate(const std::string& from_currency, bool to_account = true) const {
        if (from_currency == base_currency_) {
            return 1.0;
        }

        auto it = rates_.find(from_currency);
        if (it == rates_.end()) {
            // Rate not found - return 1.0 as fallback (no conversion)
            return 1.0;
        }

        if (to_account) {
            // Convert FROM foreign TO account currency
            return 1.0 / it->second;
        } else {
            // Convert FROM account TO foreign currency
            return it->second;
        }
    }

    /**
     * Convert amount from one currency to account currency
     *
     * @param amount Amount in source currency
     * @param from_currency Source currency code
     * @return Amount in account currency
     */
    double ConvertToAccount(double amount, const std::string& from_currency) const {
        return amount * GetRate(from_currency, true);
    }

    /**
     * Convert amount from account currency to target currency
     *
     * @param amount Amount in account currency
     * @param to_currency Target currency code
     * @return Amount in target currency
     */
    double ConvertFromAccount(double amount, const std::string& to_currency) const {
        return amount * GetRate(to_currency, false);
    }

    /**
     * Calculate margin with currency conversion
     *
     * For FOREX pairs, margin is calculated in base currency, then converted to account currency
     * Example: USD account trading EURUSD
     *   - Base currency: EUR
     *   - Margin in EUR: (lots × 100000 × EURUSD_price) / leverage
     *   - Margin in USD: margin_eur × EURUSD_price
     *
     * @param margin_in_symbol_currency Margin calculated in symbol's margin currency
     * @param symbol_margin_currency Symbol's margin currency (usually base currency)
     * @param conversion_rate Current price for conversion (e.g., EURUSD rate)
     * @return Margin in account currency
     */
    double ConvertMargin(
        double margin_in_symbol_currency,
        const std::string& symbol_margin_currency,
        double conversion_rate = 1.0
    ) const {
        // If margin currency is same as account, no conversion needed
        if (symbol_margin_currency == base_currency_) {
            return margin_in_symbol_currency;
        }

        // For cross-currency, multiply by conversion rate
        // Example: EUR margin to USD account = margin_eur × EURUSD
        return margin_in_symbol_currency * conversion_rate;
    }

    /**
     * Calculate profit with currency conversion
     *
     * For FOREX pairs, profit is in quote currency (profit currency)
     * Example: USD account trading EURUSD
     *   - Profit currency: USD (quote)
     *   - No conversion needed
     *
     * Example: USD account trading GBPJPY
     *   - Profit currency: JPY (quote)
     *   - Profit in JPY needs conversion to USD via USDJPY rate
     *
     * @param profit_in_symbol_currency Profit in symbol's profit currency
     * @param symbol_profit_currency Symbol's profit currency (usually quote)
     * @param conversion_rate Rate to convert profit currency to account currency
     * @return Profit in account currency
     */
    double ConvertProfit(
        double profit_in_symbol_currency,
        const std::string& symbol_profit_currency,
        double conversion_rate = 1.0
    ) const {
        if (symbol_profit_currency == base_currency_) {
            return profit_in_symbol_currency;
        }

        // Convert profit to account currency
        return profit_in_symbol_currency / conversion_rate;
    }

    /**
     * Get account base currency
     */
    const std::string& GetBaseCurrency() const {
        return base_currency_;
    }

    /**
     * Check if conversion rate exists for currency
     */
    bool HasRate(const std::string& currency) const {
        return rates_.find(currency) != rates_.end();
    }

    /**
     * Clear all cached rates (except base currency)
     */
    void ClearRates() {
        rates_.clear();
        rates_[base_currency_] = 1.0;
    }
};

/**
 * Helper function to determine conversion rates for a symbol
 *
 * @param account_currency Account base currency (e.g., "USD")
 * @param symbol_base Symbol base currency (e.g., "EUR" from EURUSD)
 * @param symbol_quote Symbol quote currency (e.g., "USD" from EURUSD)
 * @param symbol_price Current symbol price
 * @return Pair of <margin_conversion_rate, profit_conversion_rate>
 */
inline std::pair<double, double> GetConversionRates(
    const std::string& account_currency,
    const std::string& symbol_base,
    const std::string& symbol_quote,
    double symbol_price
) {
    double margin_rate = 1.0;
    double profit_rate = 1.0;

    // Margin conversion (base currency to account)
    if (symbol_base != account_currency) {
        // For EURUSD with USD account: margin in EUR needs × EURUSD to get USD
        if (symbol_quote == account_currency) {
            margin_rate = symbol_price;
        } else {
            // Cross-currency: would need additional rate lookup
            // For now, assume broker provides this
            margin_rate = 1.0;
        }
    }

    // Profit conversion (quote currency to account)
    if (symbol_quote != account_currency) {
        // For GBPJPY with USD account: profit in JPY needs ÷ USDJPY to get USD
        // Would need to query USDJPY rate from broker
        profit_rate = 1.0;  // Placeholder
    }

    return {margin_rate, profit_rate};
}

#endif // CURRENCY_CONVERTER_H
