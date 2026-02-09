/**
 * Visualize how room decreases as price rises on actual NAS100 data
 * Outputs data points for the hyperbolic room curve
 */

#include "../include/strategy_nasdaq_up.h"
#include "../include/tick_based_engine.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <fstream>

using namespace backtest;

struct TradePoint {
    double price;
    double price_gain;
    double room;
    double room_pct;  // room as % of price
    int trade_num;
};

int main() {
    std::cout << "=== NAS100 Room Curve Visualization ===" << std::endl;

    // Load tick data
    TickDataConfig tick_config;
    tick_config.file_path = "C:\\Users\\user\\.claude-worktrees\\ctrader-backtest\\beautiful-margulis\\validation\\Grid\\NAS100_TICKS_2025.csv";
    tick_config.format = TickDataFormat::MT5_CSV;

    TickBacktestConfig config;
    config.symbol = "NAS100";
    config.initial_balance = 10000.0;
    config.contract_size = 1.0;
    config.leverage = 100.0;
    config.pip_size = 0.01;
    config.swap_long = -17.14;
    config.swap_short = 5.76;
    config.swap_mode = 5;
    config.swap_3days = 5;
    config.start_date = "2025.04.07";
    config.end_date = "2025.10.30";
    config.tick_data_config = tick_config;
    config.verbose = false;

    // Use profitable parameters from sweep
    NasdaqUp::Config strat_config;
    strat_config.multiplier = 10.0;
    strat_config.power = -0.5;
    strat_config.stop_out_margin = 500.0;
    strat_config.contract_size = 1.0;
    strat_config.leverage = 100.0;
    strat_config.verbose = false;

    // Track trade entry points
    std::vector<TradePoint> trade_points;

    // Custom tracking strategy
    class RoomTracker {
    public:
        double multiplier;
        double power;
        double starting_price = 0.0;
        double starting_room = 0.0;
        double last_entry_price = 0.0;
        int trade_count = 0;
        std::vector<TradePoint>& points;

        RoomTracker(double mult, double pow, std::vector<TradePoint>& pts)
            : multiplier(mult), power(pow), points(pts) {}

        void OnTick(const Tick& tick, TickBasedEngine& engine) {
            double ask = tick.ask;

            // First trade
            if (trade_count == 0) {
                starting_price = ask;
                starting_room = ask * multiplier / 100.0;
                last_entry_price = ask;
                trade_count++;

                TradePoint pt;
                pt.price = ask;
                pt.price_gain = 0.0;
                pt.room = starting_room;
                pt.room_pct = (starting_room / ask) * 100.0;
                pt.trade_num = trade_count;
                points.push_back(pt);
                return;
            }

            // New high - would open trade
            if (ask > last_entry_price + starting_room * 0.1) {  // Small threshold to avoid noise
                double price_gain = ask - starting_price;
                double room = starting_room;

                if (price_gain > 1e-6) {
                    room = starting_room * std::pow(price_gain, power);
                    room = std::max(0.01, std::min(1e9, room));
                }

                last_entry_price = ask;
                trade_count++;

                TradePoint pt;
                pt.price = ask;
                pt.price_gain = price_gain;
                pt.room = room;
                pt.room_pct = (room / ask) * 100.0;
                pt.trade_num = trade_count;
                points.push_back(pt);
            }
        }
    };

    RoomTracker tracker(strat_config.multiplier, strat_config.power, trade_points);

    TickBasedEngine engine(config);
    engine.Run([&tracker](const Tick& tick, TickBasedEngine& eng) {
        tracker.OnTick(tick, eng);
    });

    // Output the curve data
    std::cout << "\n=== Room Curve Data (mult=" << strat_config.multiplier
              << ", power=" << strat_config.power << ") ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\nTrade#  Price      Gain       Room       Room%\n";
    std::cout << "------  ---------  ---------  ---------  ------\n";

    for (const auto& pt : trade_points) {
        std::cout << std::setw(6) << pt.trade_num
                  << "  " << std::setw(9) << pt.price
                  << "  " << std::setw(9) << pt.price_gain
                  << "  " << std::setw(9) << pt.room
                  << "  " << std::setw(6) << std::setprecision(4) << pt.room_pct << "%"
                  << std::endl;
        std::cout << std::setprecision(2);
    }

    // ASCII visualization
    std::cout << "\n=== Visual: Price vs Room (Hyperbola) ===" << std::endl;

    if (trade_points.empty()) {
        std::cout << "No trade points recorded!" << std::endl;
        return 1;
    }

    double min_price = trade_points.front().price;
    double max_price = trade_points.back().price;
    double max_room = trade_points.front().room;
    double min_room = trade_points.back().room;

    // Price chart with room bars
    int chart_width = 60;
    int chart_height = 25;

    std::cout << "\nPrice Movement with Room Tolerance:\n";
    std::cout << "(Each row = ~$" << (max_price - min_price) / chart_height << " price increment)\n\n";

    // Sample points for display (take every Nth point to fit chart)
    int sample_step = std::max(1, (int)trade_points.size() / chart_height);

    std::vector<TradePoint> sampled;
    for (size_t i = 0; i < trade_points.size(); i += sample_step) {
        sampled.push_back(trade_points[i]);
    }
    if (sampled.back().trade_num != trade_points.back().trade_num) {
        sampled.push_back(trade_points.back());
    }

    std::cout << "Price     | Room tolerance (hyperbolic decay)\n";
    std::cout << "----------|" << std::string(chart_width, '-') << "\n";

    for (const auto& pt : sampled) {
        // Normalize room to bar width
        double room_normalized = (max_room > min_room)
            ? (pt.room - min_room) / (max_room - min_room)
            : 1.0;
        int bar_len = std::max(1, (int)(room_normalized * (chart_width - 10)));

        std::cout << std::setw(8) << std::setprecision(0) << pt.price << "  |";
        std::cout << std::string(bar_len, '#');
        std::cout << " $" << std::setprecision(2) << pt.room;
        std::cout << std::endl;
    }

    std::cout << "----------|" << std::string(chart_width, '-') << "\n";
    std::cout << "          Start" << std::string(chart_width/2 - 10, ' ') << "→ End (price rising)\n";

    // Summary
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Starting price: $" << min_price << std::endl;
    std::cout << "Ending price:   $" << max_price << std::endl;
    std::cout << "Price gain:     $" << (max_price - min_price)
              << " (+" << ((max_price - min_price) / min_price * 100) << "%)" << std::endl;
    std::cout << "Starting room:  $" << max_room
              << " (" << (max_room / min_price * 100) << "% of price)" << std::endl;
    std::cout << "Ending room:    $" << min_room
              << " (" << (min_room / max_price * 100) << "% of price)" << std::endl;
    std::cout << "Room shrinkage: " << ((1.0 - min_room/max_room) * 100) << "%" << std::endl;
    std::cout << "Trade entries:  " << trade_points.size() << std::endl;

    // === NEW: Price chart with room floor overlay ===
    std::cout << "\n\n=== Price Chart with Room Floor (Hyperbola) ===" << std::endl;
    std::cout << "Legend: @ = Price level, # = Room floor (how far price can drop)\n" << std::endl;

    // Find the absolute min/max for the chart (including room floor)
    double chart_min_y = min_price;
    double chart_max_y = max_price;

    // The room floor at any point is: price - room
    // At start: 16753 - 1675 = 15078
    // At end: 26130 - 17 = 26113
    double lowest_floor = trade_points.front().price - trade_points.front().room;
    for (const auto& pt : trade_points) {
        double floor_price = pt.price - pt.room;
        lowest_floor = std::min(lowest_floor, floor_price);
    }
    chart_min_y = std::min(chart_min_y, lowest_floor);

    // Chart dimensions
    int overlay_height = 35;
    int overlay_width = 70;
    double price_range = chart_max_y - chart_min_y;
    double price_per_row = price_range / overlay_height;

    // Create chart grid
    std::vector<std::string> chart_grid(overlay_height, std::string(overlay_width, ' '));

    // Sample trade points evenly across the chart width
    int num_samples = std::min((int)trade_points.size(), overlay_width);
    int step2 = std::max(1, (int)trade_points.size() / num_samples);

    std::vector<TradePoint> samples2;
    for (size_t i = 0; i < trade_points.size(); i += step2) {
        samples2.push_back(trade_points[i]);
    }
    if (samples2.back().trade_num != trade_points.back().trade_num) {
        samples2.push_back(trade_points.back());
    }

    // Plot each sample point
    for (size_t col = 0; col < samples2.size() && col < (size_t)overlay_width; col++) {
        const auto& pt = samples2[col];

        // Price position (row from top)
        int price_row = overlay_height - 1 - (int)((pt.price - chart_min_y) / price_per_row);
        price_row = std::max(0, std::min(overlay_height - 1, price_row));

        // Room floor position
        double floor_price = pt.price - pt.room;
        int floor_row = overlay_height - 1 - (int)((floor_price - chart_min_y) / price_per_row);
        floor_row = std::max(0, std::min(overlay_height - 1, floor_row));

        // Draw the price point
        if (col < (size_t)overlay_width) {
            chart_grid[price_row][col] = '@';

            // Draw the room (gap between price and floor)
            for (int r = price_row + 1; r <= floor_row && r < overlay_height; r++) {
                if (chart_grid[r][col] == ' ') {
                    chart_grid[r][col] = ':';  // Room gap
                }
            }

            // Draw the floor
            if (floor_row < overlay_height) {
                chart_grid[floor_row][col] = '#';
            }
        }
    }

    // Print the chart with Y-axis labels
    std::cout << std::fixed << std::setprecision(0);
    for (int row = 0; row < overlay_height; row++) {
        double price_at_row = chart_max_y - row * price_per_row;
        if (row % 5 == 0) {
            std::cout << std::setw(7) << price_at_row << " |";
        } else {
            std::cout << "        |";
        }
        std::cout << chart_grid[row] << std::endl;
    }

    // X-axis
    std::cout << "        +" << std::string(overlay_width, '-') << std::endl;
    std::cout << "         Start                                                      End" << std::endl;
    std::cout << "         (Apr 2025)                                            (Oct 2025)" << std::endl;

    std::cout << "\n@ = Price at trade entry" << std::endl;
    std::cout << ": = Room for price to drop (tolerance zone)" << std::endl;
    std::cout << "# = Floor - if price drops here, margin stop-out triggers" << std::endl;
    std::cout << "\nNotice: The gap between @ and # (the room) SHRINKS as price rises!" << std::endl;
    std::cout << "This is the hyperbolic trap - success breeds fragility." << std::endl;

    return 0;
}
