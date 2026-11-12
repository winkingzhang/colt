#include <colt/application.hpp>

#include <iostream>
#include <memory>
#include <string>

struct Greeter {
    std::string prefix{"Hello"};
};

struct RequestMarker {
};

int main() {
    colt::Application app;
    app.add_singleton<Greeter>([] { return std::make_shared<Greeter>(); });
    app.add_request<RequestMarker>([] { return std::make_shared<RequestMarker>(); });

    app.use([](const colt::Request &, colt::RequestContext &, const colt::Next &next) {
        auto response = next();
        response.headers.set("X-Powered-By", "Colt");
        return response;
    });

    app.get("/", [](const colt::Request &, colt::RequestContext &) {
        return colt::Response::text("Hello from Colt!");
    });

    app.get("/hello/:name", [](const colt::Request &request, colt::RequestContext &context) {
        const auto &greeter = context.get<Greeter>();
        (void) context.get<RequestMarker>();
        return colt::Response::text(greeter.prefix + ", " + request.path_params.at("name") + "!");
    });

    try {
        app.listen("127.0.0.1", 8080);
        std::cout << "Listening on http://127.0.0.1:8080/hello/Colt\n"
                << "Press Enter to stop.\n";
        std::string line;
        std::getline(std::cin, line);
        app.stop();
    } catch (const std::exception &error) {
        std::cerr << "Failed to start Colt: " << error.what() << '\n';
        return 1;
    }
}
