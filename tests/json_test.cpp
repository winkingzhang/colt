#include "test.hpp"

#include <colt/application.hpp>

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <variant>

namespace {
    template<class Exception, class Action>
    bool throws(Action action) {
        try {
            action();
        } catch (const Exception &) {
            return true;
        }
        return false;
    }

    void require_invalid(std::string_view input) {
        REQUIRE(throws<colt::JsonParseError>([&] { (void) colt::Json::parse(input); }));
    }

    struct PluginValue {
        bool value;
        bool array{false};
        bool serializable{true};
        std::size_t parsed_depth{0};
        std::string input;
    };

    // A narrow test codec with its own native value and no Colt JSON conversion.
    struct TestJsonCodec {
        using value_type = PluginValue;
        inline static std::size_t dumped_depth{0};

        static PluginValue parse(std::string_view input, std::size_t max_depth) {
            if (input == "bug") throw std::runtime_error("plugin implementation failure");
            const bool array = input == "[true]" || input == "[false]";
            if (array && max_depth == 0) throw colt::JsonParseError(0, "depth exceeded");
            if (!array && input != "true" && input != "false") {
                throw colt::JsonParseError(0, "expected boolean");
            }
            return {input == "true" || input == "[true]", array, true,
                    max_depth, std::string(input)};
        }

        static std::string dump(const PluginValue &value, std::size_t max_depth) {
            dumped_depth = max_depth;
            if (!value.serializable) throw std::runtime_error("plugin serialization failure");
            if (value.array && max_depth == 0) throw std::invalid_argument("depth exceeded");
            const std::string boolean = value.value ? "true" : "false";
            return value.array ? "[" + boolean + "]" : boolean;
        }
    };

    struct WrongParseCodec : TestJsonCodec {
        static int parse(std::string_view, std::size_t) { return 0; }
    };

    struct WrongDumpCodec : TestJsonCodec {
        static std::string_view dump(const PluginValue &, std::size_t) { return "true"; }
    };

    struct MissingDumpCodec {
        using value_type = PluginValue;
        static PluginValue parse(std::string_view, std::size_t);
    };

    struct NonStaticCodec {
        using value_type = PluginValue;
        PluginValue parse(std::string_view, std::size_t);
        std::string dump(const PluginValue &, std::size_t);
    };

    struct ParseErrorDumpCodec : TestJsonCodec {
        static std::string dump(const PluginValue &, std::size_t) {
            throw colt::JsonParseError(2, "plugin output failure");
        }
    };

    static_assert(colt::JsonCodec<colt::BuiltinJsonCodec>);
    static_assert(colt::JsonCodec<TestJsonCodec>);
    static_assert(!colt::JsonCodec<WrongParseCodec>);
    static_assert(!colt::JsonCodec<WrongDumpCodec>);
    static_assert(!colt::JsonCodec<MissingDumpCodec>);
    static_assert(!colt::JsonCodec<NonStaticCodec>);
    static_assert(!colt::JsonCodec<int>);
    static_assert(!std::is_convertible_v<PluginValue, colt::Json>);
} // namespace

TEST_CASE("json scalar conversions preserve null and boolean alternatives") {
    const colt::Json null = nullptr;
    const colt::Json yes = true;
    const colt::Json no = false;
    REQUIRE(null.is_null());
    REQUIRE(yes.is<bool>());
    REQUIRE(no.is<bool>());
    REQUIRE(yes.get<bool>());
    REQUIRE(!no.get<bool>());
    REQUIRE(colt::Json::parse("null").is_null());
    REQUIRE(colt::Json::parse("true").is<bool>());
    REQUIRE(colt::Json::parse("false").is<bool>());
    REQUIRE(colt::Json::parse("[null,true,false]").dump() == "[null,true,false]");
    REQUIRE(colt::Response::json(nullptr).body == "null");
    REQUIRE(colt::Response::json(true).body == "true");
    REQUIRE(colt::Response::json(false).body == "false");
    colt::Json assigned = "string";
    assigned = nullptr;
    REQUIRE(assigned.is_null());
    assigned = false;
    REQUIRE(assigned.is<bool>());
    REQUIRE(!assigned.get<bool>());
    const char *null_string = nullptr;
    REQUIRE(throws<std::invalid_argument>([&] { (void) colt::Json(null_string); }));
}

TEST_CASE("json parses every value type and nested containers") {
    const auto value = colt::Json::parse(
        R"( {"array":[null,true,false,-42,42,1.25,{"name":"Colt"}],"empty":{}} )");
    const auto &items = value.at("array");
    REQUIRE(items.is<colt::Json::array>());
    REQUIRE(items.at(0).is_null());
    REQUIRE(items.at(1).get<bool>());
    REQUIRE(!items.at(2).get<bool>());
    REQUIRE(items.at(3).get<std::int64_t>() == -42);
    REQUIRE(items.at(4).get<std::int64_t>() == 42);
    REQUIRE(items.at(5).get<double>() == 1.25);
    REQUIRE(items.at(6).at("name").get<std::string>() == "Colt");
    REQUIRE(value.at("empty").get<colt::Json::object>().empty());
    REQUIRE(colt::Json::parse("[]").get<colt::Json::array>().empty());
    REQUIRE(colt::Json::parse(" \t\r\nnull \t\r\n").is_null());
}

TEST_CASE("json constructs and mutates values without confusing strings and booleans") {
    colt::Json value = colt::Json::object{
        {"name", "Colt"}, {"active", true}, {"items", colt::Json::array{1, 2, nullptr}}
    };
    REQUIRE(value.at("name").is<std::string>());
    value.at("name") = std::string_view("Updated");
    value.at("items").at(0) = -2;
    value.get<colt::Json::object>()["new"] = false;
    REQUIRE(value.dump() == R"({"active":true,"items":[-2,2,null],"name":"Updated","new":false})");
    REQUIRE(throws<std::out_of_range>([&] { (void) value.at("missing"); }));
    REQUIRE(throws<std::out_of_range>([&] { (void) value.at("items").at(3); }));
    REQUIRE(throws<std::bad_variant_access>([&] { (void) value.get<bool>(); }));
    const char *null_string = nullptr;
    REQUIRE(throws<std::invalid_argument>([&] { (void) colt::Json(null_string); }));
    REQUIRE(colt::Json{}.is_null());
    REQUIRE(colt::Json(nullptr).is_null());
}

TEST_CASE("json values own parsed data and copies remain independent") {
    auto parsed = [] {
        const std::string input = R"({"key":["value"]})";
        return colt::Json::parse(input);
    }();
    auto copy = parsed;
    parsed.at("key").at(0).get<std::string>() = "changed";
    REQUIRE(copy.at("key").at(0).get<std::string>() == "value");
    REQUIRE(parsed.at("key").at(0).get<std::string>() == "changed");
    const colt::Json signed_value = -1;
    const colt::Json unsigned_value = 1u;
    REQUIRE(signed_value.is<std::int64_t>());
    REQUIRE(unsigned_value.is<std::uint64_t>());
    REQUIRE(throws<std::bad_variant_access>([&] { (void) unsigned_value.get<std::int64_t>(); }));
    REQUIRE(colt::Json::parse("-0").get<std::int64_t>() == 0);
}

TEST_CASE("json preserves integer boundaries without rounding") {
    constexpr auto signed_min = std::numeric_limits<std::int64_t>::min();
    constexpr auto signed_max = std::numeric_limits<std::int64_t>::max();
    constexpr auto unsigned_max = std::numeric_limits<std::uint64_t>::max();
    REQUIRE(colt::Json::parse("-9223372036854775808").get<std::int64_t>() == signed_min);
    REQUIRE(colt::Json::parse("9223372036854775807").get<std::int64_t>() == signed_max);
    REQUIRE(colt::Json::parse("9223372036854775808").get<std::uint64_t>() ==
            static_cast<std::uint64_t>(signed_max) + 1);
    REQUIRE(colt::Json::parse("18446744073709551615").get<std::uint64_t>() == unsigned_max);
    REQUIRE(colt::Json(signed_min).dump() == "-9223372036854775808");
    REQUIRE(colt::Json(signed_max).dump() == "9223372036854775807");
    REQUIRE(colt::Json(unsigned_max).dump() == "18446744073709551615");
    REQUIRE(colt::Json::parse("9007199254740993").dump() == "9007199254740993");
    require_invalid("-9223372036854775809");
    require_invalid("18446744073709551616");
}

TEST_CASE("json round trips finite doubles including negative zero and subnormals") {
    const std::array values{
        0.0, -0.0, 1.0, -1.5, 1.2345678901234567, 1e100,
        std::numeric_limits<double>::max(), std::numeric_limits<double>::min(),
        std::numeric_limits<double>::denorm_min()
    };
    for (const auto value: values) {
        const auto parsed = colt::Json::parse(colt::Json(value).dump());
        REQUIRE(parsed.is<double>());
        REQUIRE(parsed.get<double>() == value);
        REQUIRE(std::signbit(parsed.get<double>()) == std::signbit(value));
    }
    REQUIRE(colt::Json::parse("1e2").get<double>() == 100.0);
    REQUIRE(colt::Json::parse("-2E-2").get<double>() == -0.02);
    REQUIRE(colt::Json::parse("1e+2").get<double>() == 100.0);
    REQUIRE(colt::Json(1).is_number());
    REQUIRE(colt::Json(1u).is_number());
    REQUIRE(colt::Json(1.0).is_number());
    REQUIRE(!colt::Json(true).is_number());
    for (const auto value: {std::numeric_limits<double>::infinity(),
                            -std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()}) {
        REQUIRE(throws<std::invalid_argument>([&] { (void) colt::Json(value).dump(); }));
    }
    require_invalid("1e309");
    require_invalid("1e-9999");
}

TEST_CASE("json handles escapes surrogate pairs and embedded nulls") {
    const auto escaped = colt::Json::parse(R"("\"\\\/\b\f\n\r\t\u0000\u001f")");
    std::string expected = "\"\\/\b\f\n\r\t";
    expected += '\0';
    expected += '\x1f';
    REQUIRE(escaped.get<std::string>() == expected);
    REQUIRE(escaped.dump() == R"("\"\\/\b\f\n\r\t\u0000\u001f")");
    const auto unicode = colt::Json::parse(R"("\u0041\u00e9\u20Ac\uD83D\uDE00")");
    REQUIRE(unicode.get<std::string>() == "A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
    REQUIRE(colt::Json::parse(unicode.dump()).get<std::string>() == unicode.get<std::string>());
    const auto key = colt::Json::parse(R"({"\u0000":"value"})");
    REQUIRE(key.at(std::string_view("\0", 1)).get<std::string>() == "value");
    REQUIRE(key.dump() == R"({"\u0000":"value"})");
}

TEST_CASE("json round trips generated doubles and all ASCII string bytes") {
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    for (int i = 0; i < 2048; ++i) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        const auto value = std::bit_cast<double>(state);
        if (!std::isfinite(value)) continue;
        const auto parsed = colt::Json::parse(colt::Json(value).dump()).get<double>();
        REQUIRE(std::bit_cast<std::uint64_t>(parsed) == state);
    }
    std::string ascii;
    for (int i = 0; i < 128; ++i) ascii += static_cast<char>(i);
    const colt::Json value = colt::Json::object{
        {ascii, colt::Json::array{ascii, true, nullptr, colt::Json::object{{"empty", ""}}}}
    };
    const auto encoded = value.dump();
    const auto parsed = colt::Json::parse(encoded);
    REQUIRE(parsed.at(ascii).at(0).get<std::string>() == ascii);
    REQUIRE(parsed.dump() == encoded);
    for (std::size_t length = 0; length < encoded.size(); ++length) {
        require_invalid(std::string_view(encoded).substr(0, length));
    }
}

TEST_CASE("json accepts valid UTF-8 scalar boundaries") {
    const std::array<std::string, 8> values{
        "\xc2\x80", "\xdf\xbf", "\xe0\xa0\x80", "\xed\x9f\xbf",
        "\xee\x80\x80", "\xef\xbf\xbf", "\xf0\x90\x80\x80", "\xf4\x8f\xbf\xbf"
    };
    for (const auto &value: values) {
        const auto encoded = "\"" + value + "\"";
        REQUIRE(colt::Json::parse(encoded).get<std::string>() == value);
        REQUIRE(colt::Json(value).dump() == encoded);
    }
}

TEST_CASE("json rejects invalid UTF-8 in parsed strings and serialized keys and values") {
    const std::array<std::string, 13> invalid{
        "\x80", "\xc0\xaf", "\xc1\xbf", "\xc2", "\xc2\x20", "\xe0\x80\x80",
        "\xed\xa0\x80", "\xe2\x82", "\xf0\x80\x80\x80", "\xf4\x90\x80\x80",
        "\xf5\x80\x80\x80", "\xff", "\xf0\x9f\x98"
    };
    for (const auto &text: invalid) {
        require_invalid("\"" + text + "\"");
        REQUIRE(throws<std::invalid_argument>([&] { (void) colt::Json(text).dump(); }));
        const colt::Json object = colt::Json::object{{text, nullptr}};
        REQUIRE(throws<std::invalid_argument>([&] { (void) object.dump(); }));
    }
}

TEST_CASE("json rejects malformed grammar escapes and duplicate keys") {
    for (const auto input: {
        "", " ", "undefined", "NULL", "True", "NaN", "Infinity",
        "true false", "nullx", "{}[]", "[", "{", "[1", R"({"a":1)",
        "[1,]", "[,1]", "[1 2]", R"({"a":1,})", R"({a:1})",
        R"({"a" 1})", R"({"a":})", R"({"a":1 "b":2})",
        R"({"a":1,"a":2})", R"({"a":1,"\u0061":2})",
        "+1", "01", "-01", "-", ".1", "1.", "1e", "1e+", "1e-", "--1", "0x1",
        R"(")", R"("\)", R"("\x20")", R"("\u12")", R"("\uGGGG")",
        R"("\ud800")", R"("\udc00")", R"("\ud800\u0041")",
        R"("\ud800\ud800")", R"("\ud800x")", "'string'"
    }) {
        require_invalid(input);
    }
    for (int control = 0; control < 32; ++control) {
        require_invalid("\"" + std::string(1, static_cast<char>(control)) + "\"");
        if (control != '\t' && control != '\n' && control != '\r') {
            require_invalid(std::string(1, static_cast<char>(control)) + "null");
        }
    }
    require_invalid(std::string("null\0", 5));
    require_invalid("\xef\xbb\xbfnull");
}

TEST_CASE("json reports the byte offset of parse failures") {
    try {
        (void) colt::Json::parse("[1,]");
        REQUIRE(false);
    } catch (const colt::JsonParseError &error) {
        REQUIRE(error.offset() == 3);
        REQUIRE(std::string_view(error.what()).find("byte 3") != std::string_view::npos);
    }
}

TEST_CASE("json enforces exact container depth limits on parse and dump") {
    REQUIRE(colt::Json::parse("0", 0).dump(0) == "0");
    REQUIRE(colt::Json::parse("[]", 1).dump(1) == "[]");
    REQUIRE(colt::Json::parse(R"({"a":[0]})", 2).dump(2) == R"({"a":[0]})");
    REQUIRE(throws<colt::JsonParseError>([] { (void) colt::Json::parse("[]", 0); }));
    REQUIRE(throws<colt::JsonParseError>([] { (void) colt::Json::parse("[[]]", 1); }));
    REQUIRE(throws<colt::JsonParseError>([] { (void) colt::Json::parse(R"({"a":[]})", 1); }));
    const auto nested = colt::Json::parse("[[]]");
    REQUIRE(throws<std::invalid_argument>([&] { (void) nested.dump(1); }));
    const std::string deepest = std::string(128, '[') + "0" + std::string(128, ']');
    REQUIRE(colt::Json::parse(deepest).dump() == deepest);
    require_invalid("[" + deepest + "]");
    const auto deeper = colt::Json::parse("[" + deepest + "]", 129);
    REQUIRE(throws<std::invalid_argument>([&] { (void) deeper.dump(); }));
    REQUIRE(deeper.dump(129) == "[" + deepest + "]");
}

TEST_CASE("json request parsing is explicit and response sets JSON content type") {
    colt::Request request;
    request.body = R"({"message":"caf\u00e9"})";
    const auto response = colt::Response::json(request.json(), 201);
    REQUIRE(response.status == 201);
    REQUIRE(response.headers.get("content-type") == "application/json");
    REQUIRE(response.body == "{\"message\":\"caf\xc3\xa9\"}");
    const auto wire = colt::detail::serialize_response(response, true);
    REQUIRE(wire.starts_with("HTTP/1.1 201 Created\r\n"));
    REQUIRE(wire.find("Content-Length: " + std::to_string(response.body.size()) + "\r\n") !=
            std::string::npos);
    REQUIRE(wire.ends_with(response.body));
    request.body = "[[]]";
    REQUIRE(throws<colt::JsonBodyError>([&] { (void) request.json(1); }));
    REQUIRE(colt::Response::json(nullptr).body == "null");
}

TEST_CASE("json requests reparse current bodies without enforcing content type") {
    colt::Request request;
    request.headers.set("Content-Type", "text/plain");
    request.body = R"({"value":1})";
    auto first = request.json();
    first.at("value") = 99;
    REQUIRE(request.body == R"({"value":1})");
    REQUIRE(request.json().at("value").get<std::int64_t>() == 1);
    request.body = "false";
    REQUIRE(!request.json().get<bool>());
    REQUIRE(request.body == "false");
    request.body = "invalid";
    REQUIRE(throws<colt::JsonBodyError>([&] { (void) request.json(); }));
}

TEST_CASE("json dispatch maps only request JSON parse failures to 400") {
    colt::Application app;
    app.post("/echo", [](const colt::Request &request, colt::RequestContext &) {
        return colt::Response::json(request.json());
    });
    app.post("/raw", [](const colt::Request &request, colt::RequestContext &) {
        return colt::Response::text(request.body);
    });
    app.post("/internal", [](const colt::Request &, colt::RequestContext &) {
        return colt::Response::json(colt::Json::parse("invalid internal data"));
    });
    app.post("/invalid-response", [](const colt::Request &, colt::RequestContext &) {
        return colt::Response::json(std::numeric_limits<double>::infinity());
    });
    app.post("/wrong-type", [](const colt::Request &request, colt::RequestContext &) {
        return colt::Response::text(request.json().get<std::string>());
    });
    colt::Request request;
    request.method = "POST";
    request.path = "/echo";
    request.body = R"({"ok":true})";
    REQUIRE(app.dispatch(request).body == request.body);
    for (const auto input: {"", "[", R"({"x":1,"x":2})"}) {
        request.body = input;
        const auto response = app.dispatch(request);
        REQUIRE(response.status == 400);
        REQUIRE(response.body == "Bad Request");
    }
    request.path = "/raw";
    REQUIRE(app.dispatch(request).status == 200);
    for (const auto path: {"/internal", "/invalid-response"}) {
        request.path = path;
        REQUIRE(app.dispatch(request).status == 500);
    }
    request.path = "/wrong-type";
    request.body = "true";
    REQUIRE(app.dispatch(request).status == 500);
}

TEST_CASE("json middleware can parse bodies and handle parse failures") {
    colt::Application app;
    app.use([](const colt::Request &request, colt::RequestContext &, const colt::Next &next) {
        try {
            (void) request.json();
        } catch (const colt::JsonBodyError &) {
            return colt::Response::json(colt::Json::object{{"error", "Invalid JSON"}}, 400);
        }
        return next();
    });
    app.post("/", [](const colt::Request &request, colt::RequestContext &) {
        return colt::Response::json(request.json());
    });
    colt::Request request;
    request.method = "POST";
    request.path = "/";
    request.body = "not json";
    const auto response = app.dispatch(request);
    REQUIRE(response.status == 400);
    REQUIRE(response.headers.get("Content-Type") == "application/json");
    REQUIRE(response.body == R"({"error":"Invalid JSON"})");
    request.body = "null";
    REQUIRE(app.dispatch(request).body == "null");
}

TEST_CASE("json plugins preserve native values and forward input and depth") {
    colt::Request request;
    request.body = "[true]";
    const auto native = request.json<TestJsonCodec>(7);
    REQUIRE(native.value);
    REQUIRE(native.array);
    REQUIRE(native.input == request.body);
    REQUIRE(native.parsed_depth == 7);
    REQUIRE(request.body == "[true]");
    const auto response = colt::Response::json<TestJsonCodec>(native, 201, 9);
    REQUIRE(response.status == 201);
    REQUIRE(response.body == "[true]");
    REQUIRE(response.headers.get("content-type") == "application/json");
    REQUIRE(TestJsonCodec::dumped_depth == 9);
    REQUIRE(colt::detail::serialize_response(response, false).find("Content-Length: 6\r\n") !=
            std::string::npos);
    REQUIRE(throws<colt::JsonBodyError>([&] { (void) request.json<TestJsonCodec>(0); }));
    REQUIRE(throws<std::invalid_argument>([&] {
        (void) colt::Response::json<TestJsonCodec>(native, 200, 0);
    }));
    const auto default_native = request.json<TestJsonCodec>();
    REQUIRE(default_native.parsed_depth == 128);
    REQUIRE(colt::Response::json<TestJsonCodec>(default_native).status == 200);
    REQUIRE(TestJsonCodec::dumped_depth == 128);
    request.body = "false";
    REQUIRE(!request.json<TestJsonCodec>(0).value);
    REQUIRE(colt::Response::json<TestJsonCodec>(request.json<TestJsonCodec>(), 204).body == "false");
}

TEST_CASE("json plugins retain built-in defaults and original helper signatures") {
    colt::Request request;
    request.body = R"({"name":"Colt"})";
    const auto explicit_value = request.json<colt::BuiltinJsonCodec>();
    REQUIRE(explicit_value.dump() == request.json().dump());
    REQUIRE(colt::Response::json<colt::BuiltinJsonCodec>(explicit_value).body ==
            colt::Response::json(explicit_value).body);
    colt::Json (colt::Request::*parse)(std::size_t) const = &colt::Request::json;
    colt::Response (*dump)(const colt::Json &, int) = &colt::Response::json;
    REQUIRE(dump((request.*parse)(128), 202).status == 202);
    const auto deeper = colt::Json::parse("[[]]");
    REQUIRE(throws<std::invalid_argument>([&] {
        (void) colt::Response::json<colt::BuiltinJsonCodec>(deeper, 200, 1);
    }));
    REQUIRE(colt::Response::json<colt::BuiltinJsonCodec>(deeper, 200, 2).body == "[[]]");
}

TEST_CASE("json plugins normalize only request parse failures") {
    colt::Request request;
    request.body = "invalid";
    try {
        (void) request.json<TestJsonCodec>();
        REQUIRE(false);
    } catch (const colt::JsonBodyError &error) {
        REQUIRE(error.offset() == 0);
        REQUIRE(std::string_view(error.what()) == "JSON at byte 0: expected boolean");
    }
    colt::Application app;
    app.post("/plugin", [](const colt::Request &request, colt::RequestContext &) {
        return colt::Response::json<TestJsonCodec>(request.json<TestJsonCodec>());
    });
    app.post("/internal", [](const colt::Request &, colt::RequestContext &) {
        return colt::Response::json<TestJsonCodec>(TestJsonCodec::parse("invalid", 128));
    });
    app.post("/bad-output", [](const colt::Request &, colt::RequestContext &) {
        return colt::Response::json<TestJsonCodec>(PluginValue{true, false, false, 0, {}});
    });
    app.post("/parse-error-output", [](const colt::Request &, colt::RequestContext &) {
        return colt::Response::json<ParseErrorDumpCodec>(TestJsonCodec::parse("true", 128));
    });
    request.method = "POST";
    request.path = "/plugin";
    REQUIRE(app.dispatch(request).status == 400);
    request.body = "bug";
    REQUIRE(throws<std::runtime_error>([&] { (void) request.json<TestJsonCodec>(); }));
    REQUIRE(app.dispatch(request).status == 500);
    for (const auto path: {"/internal", "/bad-output", "/parse-error-output"}) {
        request.path = path;
        REQUIRE(app.dispatch(request).status == 500);
    }
}

TEST_CASE("json plugin response framing suppresses bodyless status payloads") {
    const auto native = TestJsonCodec::parse("[true]", 1);
    for (const int status: {100, 204, 205, 304}) {
        const auto response = colt::Response::json<TestJsonCodec>(native, status);
        const auto wire = colt::detail::serialize_response(response, true);
        REQUIRE(response.body == "[true]");
        REQUIRE(wire.find("[true]") == std::string::npos);
        REQUIRE(wire.find("Content-Type: application/json\r\n") != std::string::npos);
        if (status == 205) {
            REQUIRE(wire.find("Content-Length: 0\r\n") != std::string::npos);
        } else {
            REQUIRE(wire.find("Content-Length:") == std::string::npos);
        }
    }
}

TEST_CASE("json plugins work in middleware alongside built-in JSON") {
    colt::Application app;
    app.use([](const colt::Request &request, colt::RequestContext &, const colt::Next &next) {
        try {
            (void) request.json<TestJsonCodec>();
        } catch (const colt::JsonBodyError &) {
            return colt::Response::json(colt::Json::object{{"error", "Invalid JSON"}}, 400);
        }
        auto response = next();
        response.headers.set("X-Plugin", "boolean");
        return response;
    });
    app.post("/", [](const colt::Request &request, colt::RequestContext &) {
        return colt::Response::json<TestJsonCodec>(request.json<TestJsonCodec>());
    });
    colt::Request request;
    request.method = "POST";
    request.path = "/";
    request.body = "false";
    const auto response = app.dispatch(request);
    REQUIRE(response.body == "false");
    REQUIRE(response.headers.get("X-Plugin") == "boolean");
    request.body = "invalid";
    REQUIRE(app.dispatch(request).body == R"({"error":"Invalid JSON"})");
}

TEST_CASE("json plugins serve native responses over sockets and close on parse errors") {
    colt::Application app;
    app.post("/plugin", [](const colt::Request &request, colt::RequestContext &) {
        return colt::Response::json<TestJsonCodec>(request.json<TestJsonCodec>(), 201);
    });
    app.listen("127.0.0.1", 0);
    for (const std::string body: {"[true]", "invalid"}) {
        auto socket = colt::detail::platform::connect_tcp("127.0.0.1", app.bound_port());
        colt::detail::platform::set_receive_timeout(socket, std::chrono::seconds(3));
        const std::string connection = body == "[true]" ? "Connection: close\r\n" : "";
        const auto bytes = "POST /plugin HTTP/1.1\r\nHost: localhost\r\n" + connection +
                           "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        colt::detail::platform::send_all(socket, bytes);
        std::string response;
        std::array<char, 1024> buffer{};
        while (const auto size = colt::detail::platform::receive(socket, buffer)) {
            response.append(buffer.data(), size);
        }
        if (body == "[true]") {
            REQUIRE(response.starts_with("HTTP/1.1 201 Created\r\n"));
            REQUIRE(response.find("Content-Type: application/json\r\n") != std::string::npos);
            REQUIRE(response.find("Content-Length: 6\r\n") != std::string::npos);
            REQUIRE(response.ends_with("\r\n\r\n[true]"));
        } else {
            REQUIRE(response.starts_with("HTTP/1.1 400 Bad Request\r\n"));
            REQUIRE(response.find("Connection: close\r\n") != std::string::npos);
            REQUIRE(response.ends_with("\r\n\r\nBad Request"));
        }
    }
    app.stop();
}
