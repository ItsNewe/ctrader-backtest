/**
 * Parser Benchmark - Compare old vs new tick parsing
 *
 * Build: g++ -O3 -march=native -std=c++17 -I include tests/test_parser_benchmark.cpp -o test_parser_benchmark.exe
 */

#include "../include/fast_tick_parser.h"
#include "../include/tick_data_manager.h"
#include <iostream>
#include <chrono>
#include <iomanip>

using namespace backtest;
using namespace std::chrono;

int main(int argc, char* argv[]) {
    std::string tick_file = "C:\\Users\\user\\Documents\\ctrader-backtest\\validation\\Grid\\XAUUSD_TICKS_2025.csv";

    if (argc > 1) {
        tick_file = argv[1];
    }

    std::cout << "=== Tick Parser Benchmark ===" << std::endl;
    std::cout << "File: " << tick_file << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    // Test A: Original ifstream parser FIRST (to see if it works before mmap)
    std::cout << "\nA. Original ifstream Parser (streaming mode, 1M ticks):" << std::endl;
    {
        auto start = high_resolution_clock::now();

        TickDataConfig config;
        config.file_path = tick_file;
        config.format = TickDataFormat::MT5_CSV;
        config.load_all_into_memory = false;

        TickDataManager manager(config);
        Tick tick;
        size_t count = 0;
        const size_t MAX_TICKS = 1000000;
        Tick first_tick, last_tick;
        while (manager.GetNextTick(tick) && count < MAX_TICKS) {
            if (count == 0) first_tick = tick;
            last_tick = tick;
            count++;
        }

        auto end = high_resolution_clock::now();
        double elapsed_ms = duration_cast<milliseconds>(end - start).count();

        std::cout << "   Ticks loaded: " << count << std::endl;
        std::cout << "   Time: " << elapsed_ms << " ms" << std::endl;
        if (elapsed_ms > 0) {
            std::cout << "   Rate: " << (count / (elapsed_ms / 1000.0) / 1000000.0) << " M ticks/sec" << std::endl;
        }
        std::cout << "   First: " << first_tick.timestamp << " bid=" << first_tick.bid << std::endl;
        std::cout << "   Last:  " << last_tick.timestamp << " bid=" << last_tick.bid << std::endl;
    }

    // Test 1: Fast memory-mapped parser (streaming mode for fair comparison)
    std::cout << "\n1. Fast Memory-Mapped Parser (full file scan):" << std::endl;
    size_t fast_count = 0;
    Tick fast_first, fast_last;
    {
        auto start = high_resolution_clock::now();

        StreamingTickParser parser;
        if (parser.Open(tick_file)) {
            Tick tick;
            while (parser.GetNextTick(tick)) {
                if (fast_count == 0) fast_first = tick;
                fast_last = tick;
                fast_count++;
            }
            parser.Close();  // Explicitly close
        }

        auto end = high_resolution_clock::now();
        double elapsed_ms = duration_cast<milliseconds>(end - start).count();

        std::cout << "   Ticks scanned: " << fast_count << std::endl;
        std::cout << "   Time: " << elapsed_ms << " ms" << std::endl;
        if (elapsed_ms > 0) {
            std::cout << "   Rate: " << (fast_count / (elapsed_ms / 1000.0) / 1000000.0) << " M ticks/sec" << std::endl;
        }
        std::cout << "   First: " << fast_first.timestamp << " bid=" << fast_first.bid << std::endl;
        std::cout << "   Last:  " << fast_last.timestamp << " bid=" << fast_last.bid << std::endl;
    }
    std::cout << "   (mmap closed)" << std::endl;
    std::cout << std::flush;

    // Test 2: Original ifstream parser (streaming mode - limited to 1M ticks for speed)
    std::cout << "\n2. Original ifstream Parser (streaming mode, limited to 1M ticks):" << std::endl;
    std::cout << std::flush;  // Ensure output before potential crash
    {
        auto start = high_resolution_clock::now();

        std::cout << "   Creating config..." << std::flush;
        TickDataConfig config;
        config.file_path = tick_file;
        config.format = TickDataFormat::MT5_CSV;
        config.load_all_into_memory = false;  // Streaming mode
        std::cout << " done" << std::endl;

        std::cout << "   Creating manager..." << std::flush;
        TickDataManager manager(config);
        std::cout << " done" << std::endl;
        Tick tick;
        size_t count = 0;
        const size_t MAX_TICKS = 1000000;  // Limit for comparison
        Tick first_tick, last_tick;
        while (manager.GetNextTick(tick) && count < MAX_TICKS) {
            if (count == 0) first_tick = tick;
            last_tick = tick;
            count++;
        }

        auto end = high_resolution_clock::now();
        double elapsed_ms = duration_cast<milliseconds>(end - start).count();

        std::cout << "   Ticks loaded: " << count << std::endl;
        std::cout << "   Time: " << elapsed_ms << " ms" << std::endl;
        if (elapsed_ms > 0) {
            std::cout << "   Rate: " << (count / (elapsed_ms / 1000.0) / 1000000.0) << " M ticks/sec" << std::endl;
        }
        std::cout << "   First: " << first_tick.timestamp << " bid=" << first_tick.bid << std::endl;
        std::cout << "   Last:  " << last_tick.timestamp << " bid=" << last_tick.bid << std::endl;
    }

    // Test 3: Streaming comparison (1M ticks each for fair comparison)
    std::cout << "\n3. Streaming Mode Comparison (1M ticks each):" << std::endl;
    const size_t COMPARE_TICKS = 1000000;

    // Fast streaming
    {
        auto start = high_resolution_clock::now();

        StreamingTickParser parser;
        if (parser.Open(tick_file)) {
            Tick tick;
            size_t count = 0;
            while (parser.GetNextTick(tick) && count < COMPARE_TICKS) {
                count++;
            }

            auto end = high_resolution_clock::now();
            double elapsed_ms = duration_cast<milliseconds>(end - start).count();

            std::cout << "   Fast streaming: " << count << " ticks in " << elapsed_ms << " ms";
            if (elapsed_ms > 0) {
                std::cout << " (" << (count / (elapsed_ms / 1000.0) / 1000000.0) << " M/sec)";
            }
            std::cout << std::endl;
        }
    }

    // Original streaming
    {
        auto start = high_resolution_clock::now();

        TickDataConfig config;
        config.file_path = tick_file;
        config.format = TickDataFormat::MT5_CSV;
        config.load_all_into_memory = false;

        TickDataManager manager(config);
        Tick tick;
        size_t count = 0;
        while (manager.GetNextTick(tick) && count < COMPARE_TICKS) {
            count++;
        }

        auto end = high_resolution_clock::now();
        double elapsed_ms = duration_cast<milliseconds>(end - start).count();

        std::cout << "   Original streaming: " << count << " ticks in " << elapsed_ms << " ms";
        if (elapsed_ms > 0) {
            std::cout << " (" << (count / (elapsed_ms / 1000.0) / 1000000.0) << " M/sec)";
        }
        std::cout << std::endl;
    }

    // Test 4: Accuracy verification (compare first 10K ticks to avoid memory issues)
    std::cout << "\n4. Accuracy Verification (first 10000 ticks):" << std::endl;
    {
        const size_t VERIFY_COUNT = 10000;

        // Load fast parser ticks
        StreamingTickParser fast_parser;
        std::vector<Tick> fast_ticks;
        if (fast_parser.Open(tick_file)) {
            Tick tick;
            while (fast_parser.GetNextTick(tick) && fast_ticks.size() < VERIFY_COUNT) {
                fast_ticks.push_back(tick);
            }
        }

        // Load original parser ticks
        TickDataConfig config;
        config.file_path = tick_file;
        config.format = TickDataFormat::MT5_CSV;
        config.load_all_into_memory = false;
        TickDataManager manager(config);
        std::vector<Tick> orig_ticks;
        Tick tick;
        while (manager.GetNextTick(tick) && orig_ticks.size() < VERIFY_COUNT) {
            orig_ticks.push_back(tick);
        }

        if (fast_ticks.size() != orig_ticks.size()) {
            std::cout << "   MISMATCH COUNT: fast=" << fast_ticks.size() << " orig=" << orig_ticks.size() << std::endl;
        } else {
            bool all_match = true;
            size_t first_mismatch = 0;
            for (size_t i = 0; i < fast_ticks.size(); ++i) {
                bool match = (fast_ticks[i].timestamp == orig_ticks[i].timestamp &&
                             std::abs(fast_ticks[i].bid - orig_ticks[i].bid) < 1e-10 &&
                             std::abs(fast_ticks[i].ask - orig_ticks[i].ask) < 1e-10);
                if (!match) {
                    all_match = false;
                    first_mismatch = i;
                    break;
                }
            }

            if (all_match) {
                std::cout << "   All " << fast_ticks.size() << " ticks match perfectly!" << std::endl;
            } else {
                std::cout << "   MISMATCH at index " << first_mismatch << ":" << std::endl;
                std::cout << "     Fast: ts=" << fast_ticks[first_mismatch].timestamp
                          << " bid=" << fast_ticks[first_mismatch].bid
                          << " ask=" << fast_ticks[first_mismatch].ask << std::endl;
                std::cout << "     Orig: ts=" << orig_ticks[first_mismatch].timestamp
                          << " bid=" << orig_ticks[first_mismatch].bid
                          << " ask=" << orig_ticks[first_mismatch].ask << std::endl;
            }
        }
    }

    std::cout << "\nBenchmark complete." << std::endl;
    return 0;
}
