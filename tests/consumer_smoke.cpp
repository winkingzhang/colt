#include <colt/application.hpp>

#include <thread>

struct BooleanJsonCodec {
    using value_type = bool;

    static bool parse(std::string_view input, std::size_t) {
        if (input == "true") return true;
        if (input == "false") return false;
        throw colt::JsonParseError(0, "expected boolean");
    }

    static std::string dump(bool value, std::size_t) {
        return value ? "true" : "false";
    }
};

int main() {
    colt::Application app;
    std::thread worker([] {
    });
    worker.join();
    const auto value = colt::Json::parse(R"({"framework":"Colt"})");
    const auto response = colt::Response::json(value);
    colt::Request request;
    request.body = "true";
    const auto plugin_response = colt::Response::json<BooleanJsonCodec>(
        request.json<BooleanJsonCodec>());
    return response.body == R"({"framework":"Colt"})" &&
           plugin_response.body == "true" ? 0 : 1;
}
