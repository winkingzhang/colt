#include "test.hpp"

#include <colt/http.hpp>
#include <platform/socket.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <system_error>
#include <string_view>
#include <type_traits>

TEST_CASE("default response has status 200 and empty body") {
    const colt::Response response;
    REQUIRE(response.status == 200);
    REQUIRE(response.body.empty());
}

TEST_CASE("socket loopback exchanges data on an ephemeral port") {
    using colt::detail::platform::Socket;

    static_assert(!std::is_copy_constructible_v<Socket>);
    static_assert(std::is_move_constructible_v<Socket>);

    using namespace colt::detail::platform;
    initialize_sockets();
    auto listener = listen_tcp("127.0.0.1", 0, 1);
    REQUIRE(local_port(listener) != 0);

    auto client = connect_tcp("127.0.0.1", local_port(listener));
    auto server = accept(listener);
    set_receive_timeout(server, std::chrono::seconds(5));
    set_receive_timeout(client, std::chrono::seconds(5));

    const auto receive_payload = [](Socket& socket, std::size_t expected_size) {
        std::array<char, 32> buffer{};
        std::string payload;
        payload.reserve(expected_size);
        std::size_t received = 0;
        while (received < expected_size) {
            const auto chunk_size = std::min<std::size_t>(3, expected_size - received);
            const auto count = receive(
                socket, std::span<char>(buffer).subspan(received, chunk_size));
            REQUIRE(count != 0);
            payload.append(buffer.data() + received, count);
            received += count;
        }
        return payload;
    };

    constexpr std::string_view request = "ping from client";
    send_all(client, std::span<const char>(request.data(), request.size()));
    REQUIRE(receive_payload(server, request.size()) == request);

    constexpr std::string_view response = "pong from server";
    send_all(server, std::span<const char>(response.data(), response.size()));
    REQUIRE(receive_payload(client, response.size()) == response);

    shutdown(client);
    shutdown(server);
    client.close();
    server.close();
    listener.close();
}

TEST_CASE("socket listen reports single-address bind failure") {
    using namespace colt::detail::platform;
    initialize_sockets();

    try {
        (void)listen_tcp("192.0.2.1", 0, 1);
        REQUIRE(false);
    } catch (const std::system_error&) {
    }
}

TEST_CASE("socket connect reports single-address connection failure") {
    using namespace colt::detail::platform;
    initialize_sockets();

    try {
        (void)connect_tcp("127.0.0.1", 0);
        REQUIRE(false);
    } catch (const std::system_error&) {
    }
}

TEST_CASE("socket resolver errors use a resolver error category") {
    using namespace colt::detail::platform;
    initialize_sockets();

    try {
        (void)connect_tcp("invalid host name colt-test.invalid", 80);
        REQUIRE(false);
    } catch (const std::system_error& error) {
        REQUIRE(std::string_view(error.code().category().name()) == "getaddrinfo");
    }
}
