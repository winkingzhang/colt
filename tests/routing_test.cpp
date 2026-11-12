#include "test.hpp"

#include <colt/application.hpp>

#include <memory>
#include <string>

namespace {
    struct RouteService {
        std::string value{"service"};
    };

    struct SingletonRouteService {
        std::string value{"singleton"};
    };
} // namespace

TEST_CASE (

"routing matches exact HTTP method and literal path"
)
 {
    colt::Application app;
    app.get("/items", [](const colt::Request&, colt::RequestContext&) {
        return colt::Response::text("get");
    });
    app.post("/items", [](const colt::Request&, colt::RequestContext&) {
        return colt::Response::text("post");
    });

    colt::Request get;
    get.method = "GET";
    get.path = "/items";
    REQUIRE(app.dispatch(get).body == "get");

    colt::Request post;
    post.method = "POST";
    post.path = "/items";
    REQUIRE(app.dispatch(post).body == "post");
}

TEST_CASE (

"routing extracts percent-decoded path parameters"
)
 {
    colt::Application app;
    app.get("/users/:user_id/orders/:order", [](const colt::Request& request,
                                                  colt::RequestContext&) {
        return colt::Response::text(request.path_params.at("user_id") + ":" +
                                    request.path_params.at("order"));
    });

    colt::Request request;
    request.method = "GET";
    request.path = "/users/al%20ice/orders/42";
    REQUIRE(app.dispatch(request).body == "al ice:42");
}

TEST_CASE (

"routing keeps encoded slashes inside a parameter segment"
)
 {
    colt::Application app;
    app.get("/files/:name", [](const colt::Request& request, colt::RequestContext&) {
        return colt::Response::text(request.path_params.at("name"));
    });
    app.get("/files/a/b", [](const colt::Request&, colt::RequestContext&) {
        return colt::Response::text("three segments");
    });

    colt::Request request;
    request.method = "GET";
    request.path = "/files/a%2Fb";
    REQUIRE(app.dispatch(request).body == "a/b");
}

TEST_CASE (

"routing returns 404 for wrong method or segment count"
)
 {
    colt::Application app;
    app.get("/items/:id", [](const colt::Request&, colt::RequestContext&) {
        return colt::Response::text("found");
    });

    colt::Request wrong_method;
    wrong_method.method = "POST";
    wrong_method.path = "/items/1";
    REQUIRE(app.dispatch(wrong_method).status == 404);

    colt::Request wrong_segments;
    wrong_segments.method = "GET";
    wrong_segments.path = "/items/1/extra";
    REQUIRE(app.dispatch(wrong_segments).status == 404);

    colt::Request missing;
    missing.method = "GET";
    missing.path = "/missing";
    REQUIRE(app.dispatch(missing).status == 404);
}

TEST_CASE (

"routing returns 400 for malformed path parameter escapes"
)
 {
    colt::Application app;
    app.get("/items/:id", [](const colt::Request&, colt::RequestContext&) {
        return colt::Response::text("found");
    });

    colt::Request request;
    request.method = "GET";
    request.path = "/items/%Q0";
    REQUIRE(app.dispatch(request).status == 400);
}

TEST_CASE (

"application forwards request service registration to dispatch context"
)
 {
    colt::Application app;
    app.add_request<RouteService>([] { return std::make_shared<RouteService>(); });
    app.add_singleton<SingletonRouteService>([] {
        return std::make_shared<SingletonRouteService>();
    });
    app.get("/", [](const colt::Request&, colt::RequestContext& context) {
        return colt::Response::text(context.get<RouteService>().value + ":" +
                                    context.get<SingletonRouteService>().value);
    });

    colt::Request request;
    request.method = "GET";
    request.path = "/";
    REQUIRE(app.dispatch(request).body == "service:singleton");
}

TEST_CASE (

"routing preserves mutable handler state across dispatches"
)
 {
    colt::Application app;
    app.get("/count", [count = 0](const colt::Request&,
                                    colt::RequestContext&) mutable {
        return colt::Response::text(std::to_string(++count));
    });

    colt::Request request;
    request.method = "GET";
    request.path = "/count";
    REQUIRE(app.dispatch(request).body == "1");
    REQUIRE(app.dispatch(request).body == "2");
}
