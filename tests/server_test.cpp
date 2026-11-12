#include "test.hpp"

#include <colt/application.hpp>
#include <platform/socket.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {

using colt::detail::platform::Socket;

void set_test_timeout(Socket& socket) {
    colt::detail::platform::set_receive_timeout(socket, std::chrono::seconds(3));
}

std::string read_response(Socket& socket, std::string& buffered) {
    while (true) {
        const auto header_end = buffered.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            const auto length_start = buffered.find("Content-Length: ");
            std::size_t body_length = 0;
            if (length_start != std::string::npos) {
                const auto value_start = length_start + std::string_view("Content-Length: ").size();
                const auto value_end = buffered.find("\r\n", value_start);
                body_length = static_cast<std::size_t>(std::stoull(
                    buffered.substr(value_start, value_end - value_start)));
            }
            const auto response_length = header_end + 4 + body_length;
            if (buffered.size() >= response_length) {
                auto response = buffered.substr(0, response_length);
                buffered.erase(0, response_length);
                return response;
            }
        }

        std::array<char, 1024> bytes{};
        const auto count = colt::detail::platform::receive(socket, bytes);
        if (count == 0) throw std::runtime_error("connection closed before response completed");
        buffered.append(bytes.data(), count);
    }
}

void send_request(Socket& socket, std::string_view request) {
    colt::detail::platform::send_all(socket, request);
}

Socket connect_to(colt::Application& app) {
    auto socket = colt::detail::platform::connect_tcp("127.0.0.1", app.bound_port());
    set_test_timeout(socket);
    return socket;
}

bool wait_for_close(Socket& socket) {
    std::array<char, 16> bytes{};
    try {
        return colt::detail::platform::receive(socket, bytes) == 0;
    } catch (const std::system_error&) {
        return false;
    }
}

} // namespace

TEST_CASE("server settings expose required defaults") {
    const colt::ServerSettings settings;
    REQUIRE(settings.max_connections == 256);
    REQUIRE(settings.keep_alive_timeout == std::chrono::seconds(30));
    REQUIRE(settings.http_limits.request_line_bytes == 8 * 1024);
    REQUIRE(settings.http_limits.header_bytes == 32 * 1024);
    REQUIRE(settings.http_limits.body_bytes == 1024 * 1024);
}

TEST_CASE("server routes requests and returns 404 for unmatched paths") {
    colt::Application app;
    app.get("/hello/:name", [](const colt::Request& request, colt::RequestContext&) {
        return colt::Response::text("hello " + request.path_params.at("name"));
    });
    app.listen("127.0.0.1", 0);

    auto socket = connect_to(app);
    std::string buffered;
    send_request(socket, "GET /hello/Colt HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    const auto routed = read_response(socket, buffered);
    REQUIRE(routed.find("HTTP/1.1 200 OK\r\n") == 0);
    REQUIRE(routed.ends_with("hello Colt"));

    socket.close();
    auto missing = connect_to(app);
    send_request(missing, "GET /missing HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    const auto not_found = read_response(missing, buffered);
    REQUIRE(not_found.find("HTTP/1.1 404 ") == 0);
    app.stop();
}

TEST_CASE("server returns 400 for malformed requests and 500 for handler exceptions") {
    colt::Application app;
    app.get("/error", [](const colt::Request&, colt::RequestContext&) -> colt::Response {
        throw std::runtime_error("handler failure");
    });
    app.listen("127.0.0.1", 0);

    auto malformed = connect_to(app);
    std::string buffered;
    send_request(malformed, "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n");
    REQUIRE(read_response(malformed, buffered).find("HTTP/1.1 400 ") == 0);
    REQUIRE(wait_for_close(malformed));

    auto invalid_target = connect_to(app);
    send_request(invalid_target,
                 "GET /bad\"target HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(read_response(invalid_target, buffered).find("HTTP/1.1 400 ") == 0);
    REQUIRE(wait_for_close(invalid_target));

    auto failing = connect_to(app);
    send_request(failing, "GET /error HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    REQUIRE(read_response(failing, buffered).find("HTTP/1.1 500 ") == 0);
    REQUIRE(wait_for_close(failing));
    app.stop();
}

TEST_CASE("server creates request contexts per keep-alive request and shares singletons") {
    struct RequestValue { int id; };
    struct SharedValue { int id; };

    colt::Application app;
    std::atomic<int> request_id{};
    app.add_request<RequestValue>([&] {
        return std::make_shared<RequestValue>(RequestValue{++request_id});
    });
    app.add_singleton<SharedValue>([] {
        return std::make_shared<SharedValue>(SharedValue{7});
    });
    app.get("/scope", [](const colt::Request&, colt::RequestContext& context) {
        return colt::Response::text(std::to_string(context.get<RequestValue>().id) + ":" +
                                   std::to_string(context.get<SharedValue>().id));
    });
    app.listen("127.0.0.1", 0);

    auto socket = connect_to(app);
    std::string buffered;
    send_request(socket,
                 "GET /scope HTTP/1.1\r\nHost: localhost\r\n\r\n"
                 "GET /scope HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    REQUIRE(read_response(socket, buffered).ends_with("1:7"));
    REQUIRE(read_response(socket, buffered).ends_with("2:7"));
    app.stop();
}

TEST_CASE("server closes idle keep-alive connections after configured timeout") {
    colt::Application app;
    colt::ServerSettings settings;
    settings.keep_alive_timeout = std::chrono::seconds(1);
    app.get("/", [](const colt::Request&, colt::RequestContext&) {
        return colt::Response::text("ok");
    });
    app.listen("127.0.0.1", 0, settings);

    auto socket = connect_to(app);
    std::string buffered;
    send_request(socket, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(read_response(socket, buffered).ends_with("ok"));
    REQUIRE(wait_for_close(socket));
    app.stop();
}

TEST_CASE("server closes when a response requests connection close") {
    colt::Application app;
    app.get("/", [](const colt::Request&, colt::RequestContext&) {
        auto response = colt::Response::text("done");
        response.headers.set("Connection", "keep-alive, Close");
        return response;
    });
    app.listen("127.0.0.1", 0);

    auto socket = connect_to(app);
    std::string buffered;
    send_request(socket, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(read_response(socket, buffered).ends_with("done"));
    REQUIRE(wait_for_close(socket));
    app.stop();
}

TEST_CASE("server replaces informational handler responses with a final 500") {
    colt::Application app;
    for (const int status : {100, 101, 199}) {
        app.get("/" + std::to_string(status), [status](const colt::Request&,
                                                       colt::RequestContext&) {
            return colt::Response::text("unsupported", status);
        });
    }
    app.listen("127.0.0.1", 0);

    for (const int status : {100, 101, 199}) {
        auto socket = connect_to(app);
        std::string buffered;
        send_request(socket, "GET /" + std::to_string(status) +
                                 " HTTP/1.1\r\nHost: localhost\r\n\r\n");
        const auto response = read_response(socket, buffered);
        REQUIRE(response.find("HTTP/1.1 500 Internal Server Error\r\n") == 0);
        REQUIRE(response.ends_with("Internal Server Error"));
        REQUIRE(wait_for_close(socket));
    }
    app.stop();
}

TEST_CASE("server honors close in any duplicate response Connection field") {
    colt::Application app;
    app.get("/", [](const colt::Request&, colt::RequestContext&) {
        auto response = colt::Response::text("done");
        response.headers.add("Connection", "keep-alive");
        response.headers.add("connection", "upgrade, CLOSE");
        return response;
    });
    app.listen("127.0.0.1", 0);

    auto socket = connect_to(app);
    std::string buffered;
    send_request(socket, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(read_response(socket, buffered).ends_with("done"));
    REQUIRE(wait_for_close(socket));
    app.stop();
}

TEST_CASE("server applies configured request limits") {
    colt::Application app;
    colt::ServerSettings settings;
    settings.http_limits.request_line_bytes = 12;
    app.listen("127.0.0.1", 0, settings);

    auto socket = connect_to(app);
    std::string buffered;
    send_request(socket, "GET /long HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(read_response(socket, buffered).find("HTTP/1.1 413 ") == 0);
    REQUIRE(wait_for_close(socket));
    app.stop();
}

TEST_CASE("server rejects connections when maximum is zero") {
    colt::Application app;
    colt::ServerSettings settings;
    settings.max_connections = 0;
    app.listen("127.0.0.1", 0, settings);

    auto socket = connect_to(app);
    REQUIRE(wait_for_close(socket));
    app.stop();
}

TEST_CASE("server enforces the active connection limit") {
    colt::Application app;
    colt::ServerSettings settings;
    settings.max_connections = 1;
    app.listen("127.0.0.1", 0, settings);

    auto first = connect_to(app);
    auto second = connect_to(app);
    REQUIRE(wait_for_close(second));
    app.stop();
    REQUIRE(wait_for_close(first));
}

TEST_CASE("server admits a new connection after a worker exits") {
    colt::Application app;
    colt::ServerSettings settings;
    settings.max_connections = 1;
    app.get("/", [](const colt::Request&, colt::RequestContext&) {
        return colt::Response::text("ok");
    });
    app.listen("127.0.0.1", 0, settings);

    auto first = connect_to(app);
    std::string buffered;
    send_request(first, "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    REQUIRE(read_response(first, buffered).ends_with("ok"));
    REQUIRE(wait_for_close(first));

    auto second = connect_to(app);
    send_request(second, "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    REQUIRE(read_response(second, buffered).ends_with("ok"));
    app.stop();
}

TEST_CASE("server stop interrupts a worker blocked reading a request") {
    colt::Application app;
    app.listen("127.0.0.1", 0);
    auto socket = connect_to(app);
    app.stop();
    app.stop();
    REQUIRE(wait_for_close(socket));
}
