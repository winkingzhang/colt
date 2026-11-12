cmake_minimum_required(VERSION 3.20)
project(colt LANGUAGES CXX)

option(COLT_BUILD_TESTS "Build Colt tests" ON)
option(COLT_BUILD_EXAMPLES "Build Colt examples" ON)

find_package(Threads REQUIRED)

add_library(colt src/platform/socket.cpp)
add_library(Colt::colt ALIAS colt)
target_compile_features(colt PUBLIC cxx_std_23)
target_include_directories(colt PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(colt PRIVATE src)
target_link_libraries(colt PUBLIC Threads::Threads)
if (WIN32)
    target_link_libraries(colt PRIVATE ws2_32)
endif ()

if (COLT_BUILD_TESTS)
    enable_testing()
    add_executable(colt_tests
            tests/test_main.cpp
            tests/build_test.cpp
            tests/http_parser_test.cpp
            tests/http_serializer_test.cpp
            tests/context_test.cpp
            tests/routing_test.cpp
            tests/middleware_test.cpp
            tests/server_test.cpp
    )
    target_include_directories(colt_tests PRIVATE src)
    target_link_libraries(colt_tests PRIVATE Colt::colt Threads::Threads)
    get_target_property(colt_interface_links colt INTERFACE_LINK_LIBRARIES)
    if (NOT "${colt_interface_links}" MATCHES "Threads::Threads")
        message(FATAL_ERROR "Colt::colt must publish Threads::Threads")
    endif ()
    add_executable(colt_consumer_smoke tests/consumer_smoke.cpp)
    target_link_libraries(colt_consumer_smoke PRIVATE Colt::colt)
    add_test(NAME colt_tests COMMAND colt_tests)
    add_test(NAME consumer_smoke COMMAND colt_consumer_smoke)
    add_test(NAME socket COMMAND colt_tests socket)
    add_test(NAME http_parser COMMAND colt_tests parser)
    add_test(NAME http_serializer COMMAND colt_tests serializer)
    add_test(NAME context COMMAND colt_tests context)
    add_test(NAME routing COMMAND colt_tests routing)
    add_test(NAME middleware COMMAND colt_tests middleware)
    add_test(NAME server COMMAND colt_tests server)
endif ()

if (COLT_BUILD_EXAMPLES)
    add_executable(colt_hello examples/hello.cpp)
    target_link_libraries(colt_hello PRIVATE Colt::colt)
endif ()
