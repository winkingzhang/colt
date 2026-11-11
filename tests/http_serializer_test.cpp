#include "test.hpp"

#include <colt/detail/http_parser.hpp>

#include <array>
#include <stdexcept>

TEST_CASE("serializer emits a default 200 response") {
    REQUIRE(colt::detail::serialize_response(colt::Response{}, true) ==
            "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: keep-alive\r\n\r\n");
}

TEST_CASE("serializer emits custom status headers and byte-count content length") {
    auto response = colt::Response::text("caf\xc3\xa9", 201);
    response.headers.set("X-Result", "created");

    REQUIRE(colt::detail::serialize_response(response, true) ==
            "HTTP/1.1 201 Created\r\nX-Result: created\r\nContent-Length: 5\r\nConnection: keep-alive\r\n\r\ncaf\xc3\xa9");
}

TEST_CASE("serializer replaces supplied content length") {
    auto response = colt::Response::text("hello");
    response.headers.set("content-length", "999");

    const auto serialized = colt::detail::serialize_response(response, false);
    REQUIRE(serialized.find("Content-Length: 5\r\n") != std::string::npos);
    REQUIRE(serialized.find("999") == std::string::npos);
}

TEST_CASE("serializer enforces keep alive or close connection policy") {
    colt::Response response;
    response.headers.set("Connection", "upgrade");
    const auto keep_alive = colt::detail::serialize_response(response, true);
    const auto close = colt::detail::serialize_response(response, false);

    REQUIRE(keep_alive.find("Connection: keep-alive\r\n") != std::string::npos);
    REQUIRE(close.find("Connection: close\r\n") != std::string::npos);
    REQUIRE(keep_alive.find("upgrade") == std::string::npos);
}

TEST_CASE("serializer rejects invalid response header names") {
    colt::Response response;
    response.headers.add("X-Safe\r\nInjected", "value");

    bool rejected = false;
    try {
        (void)colt::detail::serialize_response(response, true);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    REQUIRE(rejected);
}

TEST_CASE("serializer rejects response header values containing controls") {
    const std::array<std::string, 3> values{
        "first\r\nInjected: yes", std::string("nul\0value", 9), "control\x01value"};
    for (const auto& value : values) {
        colt::Response response;
        response.headers.set("X-Safe", value);

        bool rejected = false;
        try {
            (void)colt::detail::serialize_response(response, true);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        REQUIRE(rejected);
    }
}
