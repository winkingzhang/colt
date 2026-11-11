#include "test.hpp"

#include <colt/detail/http_parser.hpp>

#include <array>
#include <string>
#include <string_view>

using colt::detail::ParseStatus;
using colt::detail::parse_request;

TEST_CASE("parser limits expose the planned request line header and body defaults") {
    const colt::HttpLimits limits;
    REQUIRE(limits.request_line_bytes == 8 * 1024);
    REQUIRE(limits.header_bytes == 32 * 1024);
    REQUIRE(limits.body_bytes == 1024 * 1024);
}

TEST_CASE("parser extracts a complete GET request and query parameters") {
    const auto result = parse_request(
        "GET /items?name=two+words&x=a%26b HTTP/1.1\r\nHost: example\r\n\r\n");

    REQUIRE(result.status == ParseStatus::complete);
    REQUIRE(result.request.method == "GET");
    REQUIRE(result.request.target == "/items?name=two+words&x=a%26b");
    REQUIRE(result.request.path == "/items");
    REQUIRE(result.request.query_params.at("name") == "two words");
    REQUIRE(result.request.query_params.at("x") == "a&b");
    REQUIRE(result.bytes_consumed == 61);
    REQUIRE(!result.close_connection);
}

TEST_CASE("parser accepts a content-length POST body") {
    const auto result = parse_request(
        "POST /submit HTTP/1.1\r\nHost: x\r\nContent-Length: 4\r\n\r\ndata");

    REQUIRE(result.status == ParseStatus::complete);
    REQUIRE(result.request.body == "data");
    REQUIRE(result.bytes_consumed == 57);
}

TEST_CASE("parser reports incomplete request lines headers and bodies") {
    REQUIRE(parse_request("GET / HTTP/1.1\r").status == ParseStatus::need_more);
    REQUIRE(parse_request("GET / HTTP/1.1\r\nHost: x\r\n").status ==
            ParseStatus::need_more);
    REQUIRE(parse_request("GET / HTTP/1.1\r\nHost: x\r\n\r\n").status ==
            ParseStatus::complete);
    REQUIRE(parse_request("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 2\r\n\r\nx")
                .status == ParseStatus::need_more);
}

TEST_CASE("parser rejects requests without exactly one nonempty Host header") {
    const auto missing = parse_request("GET / HTTP/1.1\r\n\r\n");
    REQUIRE(missing.status == ParseStatus::bad_request);
    REQUIRE(missing.close_connection);

    const auto duplicate = parse_request(
        "GET / HTTP/1.1\r\nHost: first\r\nhOST: second\r\n\r\n");
    REQUIRE(duplicate.status == ParseStatus::bad_request);
    REQUIRE(duplicate.close_connection);

    const auto empty = parse_request("GET / HTTP/1.1\r\nHost: \t \r\n\r\n");
    REQUIRE(empty.status == ParseStatus::bad_request);
    REQUIRE(empty.close_connection);
}

TEST_CASE("parser accepts valid Host authorities") {
    constexpr std::array authorities{
             "example.com", "127.0.0.1:8080", "example-host:65535", "[2001:db8::1]",
             "[2001:db8::1]:443", "[::ffff:192.0.2.1]:80", "[v1.foo]",
             "[Vf.a:b!$&'()*+,;=_~-]"};
    for (const std::string_view authority : authorities) {
        const std::string request = "GET / HTTP/1.1\r\nHost: " + std::string(authority) +
                                    "\r\n\r\n";
        REQUIRE(parse_request(request).status == ParseStatus::complete);
    }
}

TEST_CASE("parser rejects malformed Host authorities and closes") {
    constexpr std::array authorities{
             "", ":80", "x y", "example.com:", "example.com:abc", "example.com:65536",
             "example.com:999999999999999999999", "2001:db8::1", "[]", "[example.com]",
             "[2001:db8::1", "2001:db8::1]", "[2001:db8::1]]", "user@example.com",
             "example.com/path", "example.com?query", "example.com\tbad", "[v1.foo#bar]",
             "[v1.foo^bar]", "[v1.foo%bar]"};
    for (const std::string_view authority : authorities) {
        const std::string request = "GET / HTTP/1.1\r\nHost: " + std::string(authority) +
                                    "\r\n\r\n";
        const auto result = parse_request(request);
        REQUIRE(result.status == ParseStatus::bad_request);
        REQUIRE(result.close_connection);
    }
}

TEST_CASE("parser accepts the exact configured request line header and body limits") {
    colt::HttpLimits limits{14, 11, 4};
    const auto request = parse_request("GET / HTTP/1.1\r\nHost: x\r\n\r\n", limits);
    REQUIRE(request.status == ParseStatus::complete);
    REQUIRE(parse_request("GET / HTTP/1.1\r", limits).status == ParseStatus::need_more);

    limits.request_line_bytes = 13;
    REQUIRE(parse_request("GET / HTTP/1.1\r\n\r\n", limits).status ==
            ParseStatus::payload_too_large);

    limits = {8192, 7, 4};
    REQUIRE(parse_request("GET / HTTP/1.1\r\nHost: x\r\n\r\n", limits).status ==
            ParseStatus::payload_too_large);
    REQUIRE(parse_request("GET / HTTP/1.1\r\nHost: xxxxx", limits).status ==
            ParseStatus::payload_too_large);

    limits = {8192, 32768, 4};
    REQUIRE(parse_request("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 4\r\n\r\ndata", limits)
                .status == ParseStatus::complete);
}

TEST_CASE("parser marks over-limit declared bodies for close") {
    colt::HttpLimits limits;
    limits.body_bytes = 3;
    const auto result = parse_request(
        "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 4\r\n\r\n", limits);

    REQUIRE(result.status == ParseStatus::payload_too_large);
    REQUIRE(result.close_connection);

    const auto overflow = parse_request(
        "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 999999999999999999999999999999\r\n\r\n");
    REQUIRE(overflow.status == ParseStatus::payload_too_large);
    REQUIRE(overflow.close_connection);
}

TEST_CASE("parser rejects malformed request lines") {
    REQUIRE(parse_request("GET / HTTP/1.0\r\nHost: x\r\n\r\n").status ==
            ParseStatus::bad_request);
    REQUIRE(parse_request("GET / HTTP/1.1\r\r").status == ParseStatus::bad_request);
    REQUIRE(parse_request("GET  HTTP/1.1\r\nHost: x\r\n\r\n").status ==
            ParseStatus::bad_request);
    REQUIRE(parse_request("GET / HTTP/1.1 extra\r\nHost: x\r\n\r\n").status ==
            ParseStatus::bad_request);
}

TEST_CASE("parser rejects duplicate and conflicting content lengths and closes") {
    const auto same = parse_request(
        "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\ncontent-length: 0\r\n\r\n");
    REQUIRE(same.status == ParseStatus::bad_request);
    REQUIRE(same.close_connection);

    const auto different = parse_request(
        "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\nContent-Length: 1\r\n\r\n");
    REQUIRE(different.status == ParseStatus::bad_request);
    REQUIRE(different.close_connection);
}

TEST_CASE("parser rejects transfer encoding including combined framing") {
    const auto transfer = parse_request(
        "POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n");
    REQUIRE(transfer.status == ParseStatus::bad_request);
    REQUIRE(transfer.close_connection);

    const auto combined = parse_request(
        "POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n");
    REQUIRE(combined.status == ParseStatus::bad_request);
    REQUIRE(combined.close_connection);
}

TEST_CASE("parser rejects invalid percent escapes in query parameters") {
    for (const std::string_view target : {"/?x=%", "/?x=%0", "/?x=%GG", "/bad%2"}) {
        const std::string request = "GET " + std::string(target) + " HTTP/1.1\r\nHost: x\r\n\r\n";
        const auto result = parse_request(request);
        REQUIRE(result.status == ParseStatus::bad_request);
        REQUIRE(result.close_connection);
    }
}

TEST_CASE("parser splits query pairs before decoding and preserves encoded path delimiters") {
    const auto result = parse_request("GET /a%2Fb?token=a=b&empty= HTTP/1.1\r\nHost: x\r\n\r\n");

    REQUIRE(result.status == ParseStatus::complete);
    REQUIRE(result.request.path == "/a%2Fb");
    REQUIRE(result.request.query_params.at("token") == "a=b");
    REQUIRE(result.request.query_params.at("empty").empty());
}

TEST_CASE("parser honors an HTTP/1.1 connection close request") {
    const auto result = parse_request(
        "GET / HTTP/1.1\r\nHost: x\r\nConnection: keep-alive, Close\r\n\r\n");

    REQUIRE(result.status == ParseStatus::complete);
    REQUIRE(result.close_connection);
}

TEST_CASE("parser preserves pipelined bytes after one request") {
    const std::string input =
        "GET / HTTP/1.1\r\nHost: x\r\n\r\nGET /next HTTP/1.1\r\nHost: x\r\n\r\n";
    const auto result = parse_request(input);

    REQUIRE(result.status == ParseStatus::complete);
    REQUIRE(result.bytes_consumed == 27);
    REQUIRE(input.substr(result.bytes_consumed) == "GET /next HTTP/1.1\r\nHost: x\r\n\r\n");
}

TEST_CASE("headers lookup is case insensitive while preserving values") {
    colt::Headers headers;
    headers.add("X-Trace", "first");
    headers.add("x-trace", "second");
    REQUIRE(headers.get("X-TRACE").value() == "first");
    headers.set("X-TRACE", "replacement");
    REQUIRE(headers.get("x-trace").value() == "replacement");
}
