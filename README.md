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
request-scoped dependency registration/resolution, and `listen()`/`stop()`.

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

## Protocol Scope

Colt currently supports HTTP/1.1 over plain TCP, including sequential
keep-alive requests and bounded request bodies framed with `Content-Length`.
It does not support TLS/HTTPS, chunked request bodies, protocol upgrades,
WebSockets, JSON parsing, asynchronous handlers, static-file hosting, or
multipart parsing. Unsupported transfer encodings are rejected. Raw request-target
characters must be permitted URI ASCII characters; quotes, backslashes, spaces,
and non-ASCII bytes are rejected, and percent escapes must be well-formed.
