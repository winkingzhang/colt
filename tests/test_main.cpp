#include "test.hpp"

#include <exception>
#include <iostream>

int colt::test::run_all_tests(std::string_view filter) {
    int failures = 0;
    for (const auto& test : test_cases()) {
        if (!filter.empty() && test.name.find(filter) == std::string_view::npos) continue;
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

int main(int argc, char** argv) {
    const std::string_view filter = argc > 1 ? argv[1] : "";
    return colt::test::run_all_tests(filter) == 0 ? 0 : 1;
}
