# Colt HTTP Framework Design

## Goal

Build Colt as a lightweight C++ web framework with a Node.js/Express-inspired
programming interface, implemented using C++23 and no third-party runtime
dependencies. The first release is an HTTP framework, not a general application
framework. The API borrows familiar route and middleware registration patterns;
it does not attempt to reproduce Node.js's asynchronous runtime.

## Constraints

- Require C++23 or later.
- Use CMake and support GCC, Clang, and MSVC on Linux, macOS, and Windows.
- Keep the public Colt API header-only, backed by a small compiled socket layer.
- Do not require third-party libraries at build or runtime.
- Use portable C++23 for framework logic and isolate POSIX/WinSock differences
  in the compiled platform layer.

## Public API

Applications register routes using HTTP-method helpers and path templates, for
example `app.get("/users/:id", handler)`. Route handlers receive request data
and a request context, and produce or populate an HTTP response. The request
API exposes the method, path, path parameters, query parameters, headers, and
string body. Responses support status, headers, and a string body.

Applications add synchronous middleware through `app.use(...)`. Middleware
receives the request, request context, and a `next` continuation. It may
short-circuit by producing a response without calling `next`; otherwise it may
inspect or modify the response as the continuation returns. Middleware runs in
registration order on the request path and unwinds in reverse order on the
response path. Route handlers receive the request and context and return a
response. Handler and middleware exceptions use the same 500 error behavior.

The framework provides a request context with `context.get<T>()` for resolving
services. The initial DI lifetimes are:

- Singleton: one registered instance shared by requests and concurrent worker
  threads. Applications are responsible for making mutable singleton services
  thread-safe.
- Request: one instance per HTTP request, shared by resolutions of that type
  within that request and discarded afterward. Each request on a keep-alive
  connection receives a new request scope.

The initial design does not require C++ reflection. Service registration and
construction will use explicit C++ types/factories. A request-scoped factory
creates at most one instance per type per request. A singleton factory creates
one shared instance; concurrent resolution must not construct it more than
once.

## Server Runtime

The public server API provides explicit `listen()` and `stop()` lifecycle
operations. `listen()` starts the server and returns once the listening socket
is ready; the accept loop runs on a server-owned thread. `stop()` may be called
from an application thread, stops acceptance, closes or interrupts active
connections, and joins the accept and connection worker threads before
returning. It is not called from a request handler/worker thread.

The socket backend assigns each accepted connection to a worker thread. A
worker processes requests sequentially on its connection and supports HTTP/1.1
keep-alive. The worker exits when the peer closes the connection, a
socket/protocol error occurs, its idle keep-alive timeout expires, or the
server is stopped.

Defaults are a maximum of 256 active connections and a 30-second keep-alive
idle timeout. Both values are configurable through server settings. When the
active connection limit is reached, new connections are closed without creating
additional workers.

The compiled platform layer owns socket creation, binding, accepting,
connection I/O, timeout/shutdown behavior, and platform-specific startup and
cleanup. The public C++ layer owns HTTP parsing/serialization, routing,
middleware, request scopes, and handler dispatch.

## HTTP Scope And Failure Behavior

The initial protocol scope is HTTP/1.1 request/response over plain TCP:

- Parse request lines and headers, query strings, and route path parameters.
- Support bounded request bodies described by `Content-Length`.
- Serialize responses with a correct `Content-Length` and connection behavior.
- Bound request-line, header, and body sizes using documented configurable
  limits; reject malformed or over-limit requests with a client error and close
  the connection when safe recovery is not possible.
- Return 404 for unmatched routes.
- Convert uncaught handler exceptions to a 500 response without terminating
  the server process.

Chunked request bodies and protocol upgrades are not part of the initial
release. Unsupported transfer encodings are rejected rather than silently
misparsed. TLS, WebSockets, JSON, asynchronous handlers, static files, and
multipart parsing are explicitly deferred.

## Build, Tests, And Example

CMake exposes a consumer target named `Colt::colt`, propagates the C++23
requirement, and builds/links the small platform socket library. Consumers
should not need to configure third-party packages.

Tests use CTest and a small in-repository test harness so the project remains
dependency-free. Unit tests cover request/response parsing and serialization,
route matching, middleware order, and DI lifetimes. Loopback integration tests
cover server start/stop, keep-alive, connection limits, and error responses.
The repository includes a minimal getting-started HTTP example. CI/build
validation should cover GCC, Clang, and MSVC on their supported hosted
platforms.

## Non-Goals For The First Release

- TLS or HTTPS.
- WebSockets or other protocol upgrades.
- JSON parsing/serialization.
- Async/coroutine handlers or event-loop networking.
- Static-file hosting or multipart/form-data parsing.
- A general-purpose application container beyond singleton and request DI.
- External dependencies, including a third-party test framework.

## Acceptance Criteria

- A consumer can configure Colt through CMake and compile a C++23 application
  using the documented route, middleware, DI, and lifecycle APIs.
- The example server serves a basic route and a path-parameter route over
  loopback TCP.
- Request scopes are distinct between requests, including requests sharing a
  keep-alive connection; singleton instances are shared.
- The server enforces connection and request-size limits and shuts down without
  leaving worker threads blocked.
- CTest passes for unit and loopback integration coverage, and the project
  builds with GCC, Clang, and MSVC.
