#include "test.hpp"

#include <colt/application.hpp>

#include <stdexcept>
#include <string>
#include <vector>

TEST_CASE(


    "middleware runs in registration order and unwinds in reverse"
) {
    colt::Application app;
    std::vector<std::string> events;
    app.use([&](const colt::Request &, colt::RequestContext &, colt::Next next) {
        events.push_back("first request");
        auto response = next();
        events.push_back("first response");
        response.body += " first";
        return response;
    });
    app.use([&](const colt::Request &, colt::RequestContext &, colt::Next next) {
        events.push_back("second request");
        auto response = next();
        events.push_back("second response");
        response.body += " second";
        return response;
    });
    app.get("/", [](const colt::Request &, colt::RequestContext &) {
        return colt::Response::text("handler");
    });

    colt::Request request;
    request.method = "GET";
    request.path = "/";
    const auto response = app.dispatch(request);
    REQUIRE(response.body == "handler second first");
    REQUIRE((events == std::vector<std::string>{"first request", "second request",
        "second response", "first response"}));
}

TEST_CASE(


    "middleware can short circuit route dispatch"
) {
    colt::Application app;
    app.use([](const colt::Request &, colt::RequestContext &, colt::Next) {
        return colt::Response::text("stopped", 401);
    });
    app.get("/", [](const colt::Request &, colt::RequestContext &) {
        return colt::Response::text("handler");
    });

    colt::Request request;
    request.method = "GET";
    request.path = "/";
    const auto response = app.dispatch(request);
    REQUIRE(response.status == 401);
    REQUIRE(response.body == "stopped");
}

TEST_CASE(


    "middleware can read matched path parameters before next"
) {
    colt::Application app;
    app.use([](const colt::Request &request, colt::RequestContext &, colt::Next next) {
        auto response = colt::Response::text(request.path_params.at("id"));
        auto downstream = next();
        response.body += ":" + downstream.body;
        return response;
    });
    app.get("/items/:id", [](const colt::Request &, colt::RequestContext &) {
        return colt::Response::text("handler");
    });

    colt::Request request;
    request.method = "GET";
    request.path = "/items/42";
    REQUIRE(app.dispatch(request).body == "42:handler");
}

TEST_CASE(


    "middleware route registration does not invalidate the matched handler"
) {
    colt::Application app;
    app.use([&](const colt::Request &, colt::RequestContext &, colt::Next next) {
        for (int i = 0; i < 1024; ++i) {
            app.get("/added/" + std::to_string(i),
                    [](const colt::Request &, colt::RequestContext &) {
                        return colt::Response::text("added");
                    });
        }
        return next();
    });
    app.get("/original/:id", [](const colt::Request &request, colt::RequestContext &) {
        return colt::Response::text(request.path_params.at("id"));
    });

    colt::Request request;
    request.method = "GET";
    request.path = "/original/42";
    REQUIRE(app.dispatch(request).body == "42");
}

TEST_CASE(


    "middleware and handler exceptions become 500 responses"
) {
    colt::Application handler_app;
    handler_app.get("/", [](const colt::Request &, colt::RequestContext &) -> colt::Response {
        throw std::runtime_error("handler failure");
    });

    colt::Request request;
    request.method = "GET";
    request.path = "/";
    REQUIRE(handler_app.dispatch(request).status == 500);

    colt::Application middleware_app;
    middleware_app.use([](const colt::Request &, colt::RequestContext &, colt::Next) -> colt::Response {
        throw std::runtime_error("middleware failure");
    });
    REQUIRE(middleware_app.dispatch(request).status == 500);
}

TEST_CASE(


    "middleware repeated next returns 500 without dispatching twice"
) {
    colt::Application app;
    int calls = 0;
    app.use([](const colt::Request &, colt::RequestContext &, colt::Next next) {
        (void) next();
        return next();
    });
    app.get("/", [&](const colt::Request &, colt::RequestContext &) {
        ++calls;
        return colt::Response::text("handler");
    });

    colt::Request request;
    request.method = "GET";
    request.path = "/";
    REQUIRE(app.dispatch(request).status == 500);
    REQUIRE(calls == 1);
}
