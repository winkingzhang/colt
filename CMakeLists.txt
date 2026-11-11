cmake_minimum_required(VERSION 3.20)
project(colt LANGUAGES CXX)

option(COLT_BUILD_TESTS "Build Colt tests" ON)

add_library(colt src/platform/socket.cpp)
add_library(Colt::colt ALIAS colt)
target_compile_features(colt PUBLIC cxx_std_23)
target_include_directories(colt PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(COLT_BUILD_TESTS)
    enable_testing()
    add_executable(colt_tests
        tests/test_main.cpp
        tests/build_test.cpp
        tests/http_parser_test.cpp
        tests/http_serializer_test.cpp
        tests/context_test.cpp
    )
    target_link_libraries(colt_tests PRIVATE Colt::colt)
    add_test(NAME colt_tests COMMAND colt_tests)
    add_test(NAME http_parser COMMAND colt_tests parser)
    add_test(NAME http_serializer COMMAND colt_tests serializer)
    add_test(NAME context COMMAND colt_tests context)
endif()
