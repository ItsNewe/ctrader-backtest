#include <iostream>
#include <fstream>

int main() {
    std::cout << "Step 1: Starting" << std::endl;
    std::cout.flush();

    std::cout << "Step 2: Testing file open" << std::endl;
    std::cout.flush();

    std::ifstream file("C:/Users/user/Documents/ctrader-backtest/validation/Grid/XAUUSD_TICKS_2025.csv");

    std::cout << "Step 3: File open attempted" << std::endl;
    std::cout.flush();

    if (file.is_open()) {
        std::cout << "Step 4: File opened successfully!" << std::endl;
        std::string line;
        std::getline(file, line);
        std::cout << "First line: " << line.substr(0, 50) << std::endl;
        file.close();
    } else {
        std::cout << "Step 4: File FAILED to open!" << std::endl;
    }

    std::cout.flush();
    std::cout << "Step 5: Complete" << std::endl;
    return 0;
}
