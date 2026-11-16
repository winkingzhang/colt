# Colt

Colt is a lightweight HTTP/1.1 server framework for C++23. Its public API is
header-only, with a small compiled socket layer. It has no third-party build or
runtime dependencies.

## Requirements

- CMake 3.20 or newer
- A C++23 compiler: GCC on Linux, Clang on macOS, or MSVC on Windows

These are the supported CI compiler/platform combinations. Other recent GCC,
Clang, and MSVC versions may work if they implement the required C++23 features.

## Build And Run

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/colt_hello
```

On Windows, run `build\Release\colt_hello.exe` for a multi-configuration
generator. The example listens on `127.0.0.1:8080`; visit
`http://127.0.0.1:8080/hello/Colt` to receive `Hello, Colt!`. Press Enter in
the server terminal to call `Application::stop()` and shut it down.

The example shows the public `Colt::colt` link target, a basic `GET /` route,
the `/hello/:name` path-parameter route, middleware wrapping `next()`, singleton and
request-scoped dependency registration/resolution, JSON echoing with `POST /echo`,
and `listen()`/`stop()`.

## Consumer CMake

Consumers can disable repository tests and examples. Link only the exported
build-tree target `Colt::colt` in a project that adds Colt as a subdirectory:

```cmake
set(COLT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(COLT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/colt)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE Colt::colt)
```

`COLT_BUILD_TESTS` and `COLT_BUILD_EXAMPLES` both default to `ON` for repository
development. `Colt::colt` propagates the C++23 and thread requirements and
builds the platform socket implementation.

## Minimal API

```cpp
#include <colt/application.hpp>

colt::Application app;
app.get("/users/:id", [](const colt::Request& request, colt::RequestContext&) {
    return colt::Response::text("User " + request.path_params.at("id"));
});
app.listen("127.0.0.1", 8080);
// Call app.stop() when the server should shut down.
```

Routes are registered with `get`, `post`, `put`, `patch`, and `delete_`.
Handlers receive a `const Request&` and `RequestContext&` and return a
`Response`. Requests expose method, target, path, headers, body, path
parameters, and query parameters; responses contain a status, headers, and
string body. Middleware registered with `use` receives the request, context,
and a `Next` continuation, and may modify the returned response.

Register dependencies before serving with `add_singleton<T>(factory)` or
`add_request<T>(factory)`, then resolve them in middleware or a handler using
`context.get<T>()`. A singleton is shared across requests; a request-scoped
service is cached for one HTTP request.

`listen(address, port, settings)` starts the server and returns once listening;
`bound_port()` reports the actual port (useful when listening on port `0`), and
`stop()` stops acceptance, interrupts active connections, and joins workers.
`ServerSettings` defaults to 256 active connections and a 30-second keep-alive
idle timeout. `HttpLimits` defaults to an 8 KiB request line, 32 KiB header
section, and 1 MiB request body. These limits can be configured through
`ServerSettings::http_limits`.

## JSON

JSON support is header-only and dependency-free. Include `<colt/json.hpp>` for
standalone use; `<colt/application.hpp>` also includes it.

```cpp
auto value = colt::Json::parse(R"({"name":"Colt","enabled":true})");
const auto &name = value.at("name").get<std::string>();
value.get<colt::Json::object>()["tags"] = colt::Json::array{"http", "json"};
auto encoded = value.dump();

app.post("/echo", [](const colt::Request& request, colt::RequestContext&) {
    return colt::Response::json(request.json(), 200);
});

auto created = colt::Response::json(colt::Json::object{{"id", 42}}, 201);
```

`Json` stores null (also the default), `bool`, `std::int64_t`, `std::uint64_t`,
`double`, `std::string`, `Json::array` (a vector), or `Json::object` (a map).
Use `is<T>()`, `is_null()`, and `is_number()` to inspect types, `get<T>()` for
typed access, and `at(key)` / `at(index)` for checked object/array access.
Wrong-type access throws `std::bad_variant_access`; missing keys and out-of-range
indices throw `std::out_of_range`. There is no implicit conversion to application
structs or between stored number types.

Parsing accepts any JSON root value and requires complete input. Integers are
preserved exactly within the signed/unsigned 64-bit range; nonnegative integers
use `std::int64_t` when they fit. Fractional/exponent numbers use finite `double`;
unrepresentable numbers are rejected rather than silently rounded to a different
integer or replaced with null. Strings support UTF-8, JSON escapes, surrogate
pairs, and embedded nulls. Invalid UTF-8, unpaired surrogates, duplicate object
keys, comments, and trailing commas are rejected. `JsonParseError` reports a
zero-based byte `offset()` and a diagnostic via `what()`.

`dump()` emits compact JSON with lexicographically sorted object keys, escapes
control characters, and rejects invalid UTF-8 or non-finite numbers with
`std::invalid_argument`. Finite doubles use round-trip precision and retain a
decimal point or exponent so they parse back as doubles. Parsing and serialization
default to at most 128 nested containers (a root container counts as one); pass a
`max_depth` argument to `parse()`, `dump()`, or `Request::json()` to change it.
Serialization depth violations also throw `std::invalid_argument`.

`Request::json()` explicitly parses the body on each call, without requiring or
validating `Content-Type`, caching, or changing the raw body. Malformed input
throws `JsonBodyError` (a `JsonParseError`); if uncaught in middleware or a
handler, dispatch returns **400 Bad Request**, closing the connection under the
existing HTTP error policy. Catch it to return a custom error response.
Application-side `Json::parse()` errors, type/access errors, and invalid
response JSON remain server errors (**500**) unless handled by the application.
`Response::json(value, status)` serializes the value and sets
`Content-Type: application/json`; HTTP framing still supplies the byte-count
`Content-Length` and suppresses bodies for bodyless statuses.

### Third-Party JSON Plugins

Applications can supply a header-only adapter for their preferred JSON library
without changing Colt or converting native values through `colt::Json`. Include
and link the external library in your application; Colt does not fetch or bundle
it. Built-in JSON remains the default, and different adapters can coexist.

An adapter must satisfy `colt::JsonCodec`: declare `value_type` and expose
static `parse(std::string_view, std::size_t)` returning exactly `value_type`,
and static `dump(const value_type&, std::size_t)` returning exactly
`std::string`. For example, the application defines the following interface,
where `ThirdPartyJson` is its library's native owning value type:

```cpp
struct MyJsonCodec {
    using value_type = ThirdPartyJson;
    static value_type parse(std::string_view input, std::size_t max_depth);
    static std::string dump(const value_type& value, std::size_t max_depth);
};

static_assert(colt::JsonCodec<MyJsonCodec>);

app.post("/native-json", [](const colt::Request& request, colt::RequestContext&) {
    auto native = request.json<MyJsonCodec>(64);
    return colt::Response::json<MyJsonCodec>(native, 201, 64);
});
```

Implement those functions using the chosen library. Consume complete input,
return values that outlive the input, enforce the supplied nesting limit (a
root container counts as one), and emit valid UTF-8 JSON. Translate only the
library's parse/validation errors into `colt::JsonParseError(offset, reason)`,
using a zero-based byte offset. The request helper wraps these as `JsonBodyError`
for the same HTTP 400 behavior as built-in JSON. Do not relabel unrelated
exceptions as parse errors: unexpected plugin failures and serialization errors
remain HTTP 500 unless your application handles them.

Plugin-specific numeric types, duplicate-key policies, key ordering, and
formatting are controlled by the adapter/library. Colt checks the interface,
not its behavior, and neither reparses output nor falls back to another codec.
JSON content type and HTTP framing remain identical across codecs.
`Request::json<Codec>()` and `Response::json<Codec>(value, status, max_depth)`
default to a depth of 128; response status defaults to 200.
`colt::BuiltinJsonCodec` exposes the same interface for Colt's own value.
Existing `request.json()` and `Response::json(value, status)` calls are unchanged.
There is no runtime registration or global backend switch.

## Protocol Scope

Colt currently supports HTTP/1.1 over plain TCP, including sequential
keep-alive requests and bounded request bodies framed with `Content-Length`.
It does not support TLS/HTTPS, chunked request bodies, protocol upgrades,
WebSockets, asynchronous handlers, static-file hosting, or
multipart parsing. Unsupported transfer encodings are rejected. Raw request-target
characters must be permitted URI ASCII characters; quotes, backslashes, spaces,
and non-ASCII bytes are rejected, and percent escapes must be well-formed.
