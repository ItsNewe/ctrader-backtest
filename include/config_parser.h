/**
 * Configuration File Parser
 *
 * Supports JSON configuration files for backtest settings.
 *
 * Example config.json:
 * {
 *   "symbol": "XAUUSD",
 *   "start_date": "2025.01.01",
 *   "end_date": "2025.12.31",
 *   "initial_balance": 10000,
 *   "strategy": "fillup",
 *   "strategy_params": {
 *     "survive_pct": 13.0,
 *     "base_spacing": 1.5
 *   },
 *   "broker": {
 *     "contract_size": 100,
 *     "leverage": 500,
 *     "pip_size": 0.01,
 *     "swap_long": -66.99,
 *     "swap_short": 41.2
 *   },
 *   "data_path": "path/to/ticks.csv"
 * }
 */

#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace backtest {

/**
 * Simple JSON-like config parser
 * Supports flat and nested key-value pairs
 */
class ConfigParser {
public:
    struct Config {
        // General settings
        std::string symbol = "XAUUSD";
        std::string start_date = "2025.01.01";
        std::string end_date = "2025.12.31";
        double initial_balance = 10000.0;
        std::string strategy = "fillup";
        std::string data_path;
        bool verbose = false;

        // Broker settings
        double contract_size = 100.0;
        double leverage = 500.0;
        double pip_size = 0.01;
        double swap_long = -66.99;
        double swap_short = 41.2;
        int swap_mode = 1;
        int swap_3days = 3;

        // Strategy parameters
        double survive_pct = 13.0;
        double base_spacing = 1.5;
        double min_volume = 0.01;
        double max_volume = 10.0;
        double lookback_hours = 4.0;
        double antifragile_scale = 0.1;
        double velocity_threshold = 30.0;
    };

    /**
     * Load configuration from a JSON file
     */
    static Config load(const std::string& filepath) {
        Config config;

        std::ifstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open config file: " + filepath);
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        // Simple JSON parsing (handles basic flat and nested structures)
        auto values = parse_json(content);

        // Map values to config
        if (values.count("symbol")) config.symbol = values["symbol"];
        if (values.count("start_date")) config.start_date = values["start_date"];
        if (values.count("end_date")) config.end_date = values["end_date"];
        if (values.count("initial_balance")) config.initial_balance = std::stod(values["initial_balance"]);
        if (values.count("strategy")) config.strategy = values["strategy"];
        if (values.count("data_path")) config.data_path = values["data_path"];
        if (values.count("verbose")) config.verbose = (values["verbose"] == "true");

        // Broker settings (may be nested under "broker")
        if (values.count("contract_size")) config.contract_size = std::stod(values["contract_size"]);
        if (values.count("leverage")) config.leverage = std::stod(values["leverage"]);
        if (values.count("pip_size")) config.pip_size = std::stod(values["pip_size"]);
        if (values.count("swap_long")) config.swap_long = std::stod(values["swap_long"]);
        if (values.count("swap_short")) config.swap_short = std::stod(values["swap_short"]);
        if (values.count("swap_mode")) config.swap_mode = std::stoi(values["swap_mode"]);
        if (values.count("swap_3days")) config.swap_3days = std::stoi(values["swap_3days"]);

        // Strategy parameters (may be nested under "strategy_params")
        if (values.count("survive_pct")) config.survive_pct = std::stod(values["survive_pct"]);
        if (values.count("base_spacing")) config.base_spacing = std::stod(values["base_spacing"]);
        if (values.count("min_volume")) config.min_volume = std::stod(values["min_volume"]);
        if (values.count("max_volume")) config.max_volume = std::stod(values["max_volume"]);
        if (values.count("lookback_hours")) config.lookback_hours = std::stod(values["lookback_hours"]);
        if (values.count("antifragile_scale")) config.antifragile_scale = std::stod(values["antifragile_scale"]);
        if (values.count("velocity_threshold")) config.velocity_threshold = std::stod(values["velocity_threshold"]);

        return config;
    }

    /**
     * Save configuration to a JSON file
     */
    static void save(const std::string& filepath, const Config& config) {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open config file for writing: " + filepath);
        }

        file << "{\n";
        file << "  \"symbol\": \"" << config.symbol << "\",\n";
        file << "  \"start_date\": \"" << config.start_date << "\",\n";
        file << "  \"end_date\": \"" << config.end_date << "\",\n";
        file << "  \"initial_balance\": " << config.initial_balance << ",\n";
        file << "  \"strategy\": \"" << config.strategy << "\",\n";
        file << "  \"data_path\": \"" << config.data_path << "\",\n";
        file << "  \"verbose\": " << (config.verbose ? "true" : "false") << ",\n";
        file << "\n";
        file << "  \"broker\": {\n";
        file << "    \"contract_size\": " << config.contract_size << ",\n";
        file << "    \"leverage\": " << config.leverage << ",\n";
        file << "    \"pip_size\": " << config.pip_size << ",\n";
        file << "    \"swap_long\": " << config.swap_long << ",\n";
        file << "    \"swap_short\": " << config.swap_short << ",\n";
        file << "    \"swap_mode\": " << config.swap_mode << ",\n";
        file << "    \"swap_3days\": " << config.swap_3days << "\n";
        file << "  },\n";
        file << "\n";
        file << "  \"strategy_params\": {\n";
        file << "    \"survive_pct\": " << config.survive_pct << ",\n";
        file << "    \"base_spacing\": " << config.base_spacing << ",\n";
        file << "    \"min_volume\": " << config.min_volume << ",\n";
        file << "    \"max_volume\": " << config.max_volume << ",\n";
        file << "    \"lookback_hours\": " << config.lookback_hours << ",\n";
        file << "    \"antifragile_scale\": " << config.antifragile_scale << ",\n";
        file << "    \"velocity_threshold\": " << config.velocity_threshold << "\n";
        file << "  }\n";
        file << "}\n";
    }

    /**
     * Create default config file
     */
    static void create_default(const std::string& filepath) {
        Config config;
        config.data_path = "validation/Grid/XAUUSD_TICKS_2025.csv";
        save(filepath, config);
    }

private:
    /**
     * Simple JSON parser - extracts key-value pairs (flattens nested objects)
     */
    static std::map<std::string, std::string> parse_json(const std::string& json) {
        std::map<std::string, std::string> result;

        size_t pos = 0;
        std::string current_key;
        bool in_string = false;
        bool reading_key = false;
        bool reading_value = false;
        std::string buffer;

        while (pos < json.size()) {
            char c = json[pos];

            if (c == '"') {
                if (!in_string) {
                    in_string = true;
                    buffer.clear();
                    if (!reading_value) reading_key = true;
                } else {
                    in_string = false;
                    if (reading_key) {
                        current_key = buffer;
                        reading_key = false;
                    } else if (reading_value) {
                        result[current_key] = buffer;
                        reading_value = false;
                    }
                }
            } else if (in_string) {
                buffer += c;
            } else if (c == ':') {
                reading_value = true;
                buffer.clear();
            } else if (c == ',' || c == '}' || c == ']') {
                if (reading_value && !buffer.empty()) {
                    // Trim whitespace
                    size_t start = buffer.find_first_not_of(" \t\n\r");
                    size_t end = buffer.find_last_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        result[current_key] = buffer.substr(start, end - start + 1);
                    }
                    reading_value = false;
                }
                buffer.clear();
            } else if (reading_value && c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                buffer += c;
            }

            pos++;
        }

        return result;
    }
};

} // namespace backtest

#endif // CONFIG_PARSER_H
