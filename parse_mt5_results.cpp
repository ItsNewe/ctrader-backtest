#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <codecvt>
#include <locale>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: parse_mt5_results <csv_file>" << std::endl;
        return 1;
    }
    
    std::wifstream file(argv[1]);
    file.imbue(std::locale(file.getloc(), new std::codecvt_utf16<wchar_t, 0x10ffff, std::little_endian>));
    
    std::wstring line;
    double max_equity = 10000.0;
    double max_dd_pct = 0.0;
    double final_balance = 10000.0;
    double final_equity = 10000.0;
    int line_count = 0;
    
    while (std::getline(file, line)) {
        line_count++;
        if (line_count == 1) continue; // Skip header
        
        // Parse tab-separated values
        std::wistringstream iss(line);
        std::wstring date, balance_str, equity_str, deposit_str;
        
        std::getline(iss, date, L'\t');
        std::getline(iss, balance_str, L'\t');
        std::getline(iss, equity_str, L'\t');
        std::getline(iss, deposit_str, L'\t');
        
        try {
            double balance = std::stod(std::string(balance_str.begin(), balance_str.end()));
            double equity = std::stod(std::string(equity_str.begin(), equity_str.end()));
            
            final_balance = balance;
            final_equity = equity;
            
            if (equity > max_equity) {
                max_equity = equity;
            }
            
            double dd_pct = (max_equity - equity) / max_equity * 100.0;
            if (dd_pct > max_dd_pct) {
                max_dd_pct = dd_pct;
            }
        } catch (...) {
            // Skip invalid lines
        }
    }
    
    std::cout << "Lines processed: " << line_count << std::endl;
    std::cout << "Final Balance: $" << final_balance << " (" << (final_balance/10000.0) << "x)" << std::endl;
    std::cout << "Final Equity: $" << final_equity << std::endl;
    std::cout << "Peak Equity: $" << max_equity << std::endl;
    std::cout << "Max Drawdown: " << max_dd_pct << "%" << std::endl;
    
    return 0;
}
