#pragma once

#include <colt/context.hpp>
#include <colt/detail/http_parser.hpp>
#include <colt/detail/platform_socket.hpp>
#include <colt/http.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace colt {

using Handler = std::function<Response(const Request&, RequestContext&)>;
using Next = std::function<Response()>;
using Middleware = std::function<Response(const Request&, RequestContext&, Next)>;

struct ServerSettings {
    std::size_t max_connections{256};
    std::chrono::seconds keep_alive_timeout{30};
    HttpLimits http_limits{};
};

class Application {
public:
    Application() = default;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    ~Application() { stop(); }

    template <class T, class Factory>
    void add_singleton(Factory&& factory) {
        registry_.template add_singleton<T>(std::forward<Factory>(factory));
    }

    template <class T, class Factory>
    void add_request(Factory&& factory) {
        registry_.template add_request<T>(std::forward<Factory>(factory));
    }

    void get(std::string pattern, Handler handler) {
        add_route("GET", std::move(pattern), std::move(handler));
    }

    void post(std::string pattern, Handler handler) {
        add_route("POST", std::move(pattern), std::move(handler));
    }

    void put(std::string pattern, Handler handler) {
        add_route("PUT", std::move(pattern), std::move(handler));
    }

    void patch(std::string pattern, Handler handler) {
        add_route("PATCH", std::move(pattern), std::move(handler));
    }

    void delete_(std::string pattern, Handler handler) {
        add_route("DELETE", std::move(pattern), std::move(handler));
    }

    void use(Middleware middleware) {
        middleware_.push_back(std::move(middleware));
    }

    [[nodiscard]] Response dispatch(Request request) {
        RequestContext context{registry_};
        return dispatch(std::move(request), context);
    }

    [[nodiscard]] Response dispatch(Request request, RequestContext& context) {
        try {
            std::vector<std::string> path_segments;
            if (!split_and_decode_path(request.path, path_segments)) {
                return Response::text("Bad Request", 400);
            }

            const Handler* matched_handler = nullptr;
            for (const auto& route : routes_) {
                const auto& candidate = *route;
                if (candidate.method != request.method) continue;

                std::vector<std::string> pattern_segments;
                if (!split_path(candidate.pattern, pattern_segments) ||
                    pattern_segments.size() != path_segments.size()) {
                    continue;
                }

                auto params = request.path_params;
                bool matches = true;
                for (std::size_t i = 0; i < pattern_segments.size(); ++i) {
                    const auto& pattern_segment = pattern_segments[i];
                    if (!pattern_segment.empty() && pattern_segment.front() == ':') {
                        params[pattern_segment.substr(1)] = path_segments[i];
                    } else if (pattern_segment != path_segments[i]) {
                        matches = false;
                        break;
                    }
                }
                if (!matches) continue;

                request.path_params = std::move(params);
                matched_handler = &candidate.handler;
                break;
            }

            auto route = [&]() {
                if (matched_handler) return (*matched_handler)(request, context);
                return Response::text("Not Found", 404);
            };

            std::function<Response(std::size_t)> run_middleware;
            run_middleware = [&](std::size_t index) -> Response {
                if (index == middleware_.size()) return route();

                bool called = false;
                bool repeated = false;
                Next next = [&]() -> Response {
                    if (called) {
                        repeated = true;
                        return Response::text("Internal Server Error", 500);
                    }
                    called = true;
                    return run_middleware(index + 1);
                };
                auto response = middleware_[index](request, context, std::move(next));
                if (repeated) return Response::text("Internal Server Error", 500);
                return response;
            };

            return run_middleware(0);
        } catch (...) {
            return Response::text("Internal Server Error", 500);
        }
    }

    void listen(std::string address = "127.0.0.1", std::uint16_t port = 8080,
                ServerSettings settings = {}) {
        std::lock_guard lock(lifecycle_mutex_);
        if (listener_) throw std::logic_error("application is already listening");

        auto listener = std::make_unique<detail::platform::Socket>(
            detail::platform::listen_tcp(address, port, 128));
        const auto bound_port = detail::platform::local_port(*listener);
        settings_ = settings;
        listen_address_ = address;
        bound_port_ = bound_port;
        stopping_.store(false);
        listener_ = std::move(listener);
        try {
            accept_thread_ = std::thread([this] { accept_connections(); });
        } catch (...) {
            stopping_.store(true);
            listener_.reset();
            bound_port_ = 0;
            throw;
        }
    }

    [[nodiscard]] std::uint16_t bound_port() const {
        std::lock_guard lock(lifecycle_mutex_);
        return bound_port_;
    }

    void stop() noexcept {
        std::lock_guard stop_lock(stop_mutex_);
        {
            std::lock_guard lock(lifecycle_mutex_);
            if (!listener_ && !accept_thread_.joinable()) return;
            stopping_.store(true);
            if (listener_ && *listener_) {
                try { detail::platform::shutdown(*listener_); } catch (...) {}
                try {
                    auto address = listen_address_;
                    if (address == "0.0.0.0") address = "127.0.0.1";
                    else if (address == "::") address = "::1";
                    auto wake = detail::platform::connect_tcp(address, bound_port_);
                } catch (...) {}
            }
            for (const auto& socket : active_sockets_) {
                try { detail::platform::shutdown(*socket); } catch (...) {}
            }
        }

        if (accept_thread_.joinable()) accept_thread_.join();
        for (auto& worker : workers_) {
            if (worker.thread.joinable()) worker.thread.join();
        }

        std::lock_guard lock(lifecycle_mutex_);
        workers_.clear();
        active_sockets_.clear();
        listener_.reset();
        bound_port_ = 0;
    }

private:
    using Socket = detail::platform::Socket;

    struct Route {
        std::string method;
        std::string pattern;
        Handler handler;
    };

    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> finished;
    };

    void add_route(std::string method, std::string pattern, Handler handler) {
        routes_.push_back(std::make_unique<Route>(
            Route{std::move(method), std::move(pattern), std::move(handler)}));
    }

    void accept_connections() noexcept {
        while (!stopping_.load()) {
            Socket accepted;
            try {
                accepted = detail::platform::accept(*listener_);
            } catch (...) {
                if (stopping_.load()) return;
                stopping_.store(true);
                return;
            }

            try {
                std::lock_guard lock(lifecycle_mutex_);
                if (stopping_.load()) continue;
                for (auto current = workers_.begin(); current != workers_.end();) {
                    if (current->finished->load()) {
                        if (current->thread.joinable()) current->thread.join();
                        current = workers_.erase(current);
                    } else {
                        ++current;
                    }
                }
                if (active_sockets_.size() >= settings_.max_connections) continue;

                auto socket = std::make_shared<Socket>(std::move(accepted));
                active_sockets_.push_back(socket);
                auto finished = std::make_shared<std::atomic<bool>>(false);
                try {
                    workers_.push_back({{}, finished});
                } catch (...) {
                    active_sockets_.pop_back();
                    continue;
                }
                try {
                    workers_.back().thread = std::thread([this, socket, finished] {
                        serve_connection(socket);
                        finished->store(true);
                    });
                } catch (...) {
                    workers_.pop_back();
                    active_sockets_.erase(std::remove(active_sockets_.begin(),
                                                      active_sockets_.end(), socket),
                                          active_sockets_.end());
                }
            } catch (...) {
            }
        }
    }

    void serve_connection(const std::shared_ptr<Socket>& socket) noexcept {
        try {
            const auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
                settings_.keep_alive_timeout);
            detail::platform::set_receive_timeout(*socket, std::max(timeout,
                std::chrono::milliseconds(1)));

            std::string buffered;
            std::array<char, 8192> input{};
            while (!stopping_.load()) {
                auto parsed = detail::parse_request(buffered, settings_.http_limits);
                if (parsed.status == detail::ParseStatus::need_more) {
                    const auto received = detail::platform::receive(*socket, input);
                    if (received == 0) break;
                    buffered.append(input.data(), received);
                    continue;
                }

                if (parsed.status != detail::ParseStatus::complete) {
                    const int status = parsed.status == detail::ParseStatus::payload_too_large
                                           ? 413 : 400;
                    const auto bytes = detail::serialize_response(
                        Response::text(status == 413 ? "Payload Too Large" : "Bad Request", status),
                        false);
                    detail::platform::send_all(*socket, std::span<const char>(bytes));
                    break;
                }

                buffered.erase(0, parsed.bytes_consumed);
                const bool request_close = parsed.close_connection;
                RequestContext context{registry_};
                auto response = dispatch(std::move(parsed.request), context);
                bool response_close = false;
                if (const auto connection = response.headers.get("Connection")) {
                    std::string value(*connection);
                    std::transform(value.begin(), value.end(), value.begin(), [](char ch) {
                        if (ch >= 'A' && ch <= 'Z') return static_cast<char>(ch + ('a' - 'A'));
                        return ch;
                    });
                    std::size_t start = 0;
                    while (start < value.size()) {
                        const auto comma = value.find(',', start);
                        const auto token = std::string_view(value).substr(
                            start, comma == std::string::npos ? std::string::npos : comma - start);
                        const auto first = token.find_first_not_of(" \t");
                        const auto last = token.find_last_not_of(" \t");
                        if (first != std::string_view::npos &&
                            token.substr(first, last - first + 1) == "close") {
                            response_close = true;
                            break;
                        }
                        if (comma == std::string::npos) break;
                        start = comma + 1;
                    }
                }
                const bool error_requires_close = response.status == 400 || response.status == 413 ||
                                                  response.status >= 500;
                const bool keep_alive = !request_close && !response_close && !error_requires_close;
                const auto bytes = detail::serialize_response(response, keep_alive);
                detail::platform::send_all(*socket, std::span<const char>(bytes));
                if (!keep_alive) break;
            }
        } catch (...) {
        }

        std::lock_guard lock(lifecycle_mutex_);
        active_sockets_.erase(std::remove(active_sockets_.begin(), active_sockets_.end(), socket),
                              active_sockets_.end());
    }

    static bool split_path(std::string_view path, std::vector<std::string>& segments) {
        if (path.empty() || path.front() != '/') return false;
        if (path == "/") return true;

        std::size_t start = 1;
        while (true) {
            const auto delimiter = path.find('/', start);
            segments.emplace_back(path.substr(start, delimiter == std::string_view::npos
                                                          ? path.size() - start
                                                          : delimiter - start));
            if (delimiter == std::string_view::npos) return true;
            start = delimiter + 1;
        }
    }

    static int hex_value(char ch) {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        return -1;
    }

    static bool decode_segment(std::string_view encoded, std::string& decoded) {
        decoded.clear();
        for (std::size_t i = 0; i < encoded.size(); ++i) {
            if (encoded[i] != '%') {
                decoded.push_back(encoded[i]);
                continue;
            }
            if (i + 2 >= encoded.size()) return false;
            const int high = hex_value(encoded[i + 1]);
            const int low = hex_value(encoded[i + 2]);
            if (high < 0 || low < 0) return false;
            decoded.push_back(static_cast<char>((high << 4) | low));
            i += 2;
        }
        return true;
    }

    static bool split_and_decode_path(std::string_view path,
                                     std::vector<std::string>& segments) {
        std::vector<std::string> raw_segments;
        if (!split_path(path, raw_segments)) return false;
        segments.reserve(raw_segments.size());
        for (const auto& segment : raw_segments) {
            std::string decoded;
            if (!decode_segment(segment, decoded)) return false;
            segments.push_back(std::move(decoded));
        }
        return true;
    }

    ServiceRegistry registry_;
    std::vector<std::unique_ptr<Route>> routes_;
    std::vector<Middleware> middleware_;
    mutable std::mutex lifecycle_mutex_;
    std::mutex stop_mutex_;
    std::atomic<bool> stopping_{true};
    std::unique_ptr<Socket> listener_;
    std::uint16_t bound_port_{};
    ServerSettings settings_{};
    std::string listen_address_;
    std::vector<std::shared_ptr<Socket>> active_sockets_;
    std::thread accept_thread_;
    std::vector<Worker> workers_;
};

} // namespace colt
