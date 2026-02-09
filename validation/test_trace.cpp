#include <iostream>
#include <fstream>

int main() {
    std::ofstream log("trace.log");
    log << "Step 1: Starting" << std::endl;
    
    #include "../include/tick_data.h"
    log << "Step 2: tick_data.h included (compile time)" << std::endl;
    
    log << "Step 3: About to include tick_based_engine.h" << std::endl;
    log.flush();
    
    std::cout << "Hello from test_trace!" << std::endl;
    log << "Step 4: cout worked" << std::endl;
    
    log.close();
    return 0;
}
