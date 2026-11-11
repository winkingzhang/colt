#include "test.hpp"

#include <colt/http.hpp>
#include <platform/socket.hpp>

#include <array>
#include <chrono>
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

    constexpr std::string_view request = "ping from client";
    send_all(client, std::span<const char>(request.data(), request.size()));
    std::array<char, 32> received_request{};
    const auto request_size = receive(server, received_request);
    REQUIRE(std::string_view(received_request.data(), request_size) == request);

    constexpr std::string_view response = "pong from server";
    send_all(server, std::span<const char>(response.data(), response.size()));
    std::array<char, 32> received_response{};
    const auto response_size = receive(client, received_response);
    REQUIRE(std::string_view(received_response.data(), response_size) == response);

    shutdown(client);
    shutdown(server);
    client.close();
    server.close();
    listener.close();
}

TEST_CASE("socket connect tries later localhost address candidates") {
    using namespace colt::detail::platform;
    initialize_sockets();
    auto listener = listen_tcp("127.0.0.1", 0, 1);

    auto client = connect_tcp("localhost", local_port(listener));
    auto server = accept(listener);

    shutdown(client);
    shutdown(server);
}

TEST_CASE("socket listen tries later localhost address candidates") {
    using namespace colt::detail::platform;
    initialize_sockets();
    auto ipv6_listener = listen_tcp("::1", 0, 1);

    auto ipv4_listener = listen_tcp("localhost", local_port(ipv6_listener), 1);
    REQUIRE(local_port(ipv4_listener) == local_port(ipv6_listener));
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
