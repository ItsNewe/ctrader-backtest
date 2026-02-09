#include <iostream>
#include <vector>
#include <cmath>

int main() {
    std::cout << "Testing basic C++ functionality..." << std::endl;

    // Test vector
    std::vector<double> values;
    for (int i = 0; i < 10; ++i) {
        values.push_back(i * 1.5);
    }
    std::cout << "Vector size: " << values.size() << std::endl;

    // Test math
    double result = std::sqrt(16.0);
    std::cout << "sqrt(16) = " << result << std::endl;

    // Test accumulate (like in our code)
    double sum = 0.0;
    for (auto v : values) {
        sum += v;
    }
    std::cout << "Sum: " << sum << std::endl;

    std::cout << "All tests passed!" << std::endl;
    return 0;
}
