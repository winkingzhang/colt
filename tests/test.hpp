#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace colt::test {

struct TestCase {
    std::string_view name;
    void (*run)();
};

inline std::vector<TestCase>& test_cases() {
    static std::vector<TestCase> cases;
    return cases;
}

struct RegisterTest {
    RegisterTest(std::string_view name, void (*run)()) {
        test_cases().push_back({name, run});
    }
};

inline void require(bool condition, std::string_view expression,
                    std::string_view file, int line) {
    if (!condition) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) +
                                 ": REQUIRE(" + std::string(expression) + ") failed");
    }
}

int run_all_tests();

} // namespace colt::test

#define COLT_TEST_CONCAT_IMPL(left, right) left##right
#define COLT_TEST_CONCAT(left, right) COLT_TEST_CONCAT_IMPL(left, right)
#define TEST_CASE(name)                                                        \
    static void COLT_TEST_CONCAT(colt_test_, __LINE__)();                      \
    static const ::colt::test::RegisterTest COLT_TEST_CONCAT(                  \
        colt_test_registration_, __LINE__){name,                               \
                                            COLT_TEST_CONCAT(colt_test_,       \
                                                             __LINE__)};       \
    static void COLT_TEST_CONCAT(colt_test_, __LINE__)()
#define REQUIRE(expression)                                                    \
    ::colt::test::require(static_cast<bool>(expression), #expression,          \
                          __FILE__, __LINE__)
