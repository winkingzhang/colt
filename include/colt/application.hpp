#pragma once

#include <colt/context.hpp>
#include <colt/http.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace colt {

using Handler = std::function<Response(const Request&, RequestContext&)>;
using Next = std::function<Response()>;
using Middleware = std::function<Response(const Request&, RequestContext&, Next)>;

class Application {
public:
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
        try {
            std::vector<std::string> path_segments;
            if (!split_and_decode_path(request.path, path_segments)) {
                return Response::text("Bad Request", 400);
            }

            Handler matched_handler;
            for (const auto& candidate : routes_) {
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
                matched_handler = candidate.handler;
                break;
            }

            auto route = [&]() {
                if (matched_handler) return matched_handler(request, context);
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

private:
    struct Route {
        std::string method;
        std::string pattern;
        Handler handler;
    };

    void add_route(std::string method, std::string pattern, Handler handler) {
        routes_.push_back({std::move(method), std::move(pattern), std::move(handler)});
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
    std::vector<Route> routes_;
    std::vector<Middleware> middleware_;
};

} // namespace colt
