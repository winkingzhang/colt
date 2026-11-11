#include "test.hpp"

#include <exception>
#include <iostream>

int colt::test::run_all_tests() {
    int failures = 0;
    for (const auto& test : test_cases()) {
        try {
            test.run();
            std::cout << "[pass] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[fail] " << test.name << ": " << error.what() << '\n';
        }
    }
    return failures;
}

int main() {
    return colt::test::run_all_tests() == 0 ? 0 : 1;
}
