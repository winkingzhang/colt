# Colt JSON Parsing And Serialization Design

## Goal

Add dependency-free JSON parsing and serialization to Colt, with explicit
request-body parsing and convenient JSON responses. Add an application-defined
codec plugin interface so third-party JSON libraries can use the same HTTP
helpers without becoming Colt dependencies. Built-in JSON remains the default.

It supersedes the JSON deferral in
`2026-11-11-colt-http-framework-design.md`. All other
framework constraints and protocol exclusions remain unchanged.

## Constraints

- Require C++23 and use only the standard library within Colt; applications
  supply and manage any third-party JSON dependencies.
- Keep JSON support header-only, usable independently of the socket layer.
- Preserve the existing `Colt::colt` consumer target and dependency requirements.
- Preserve raw request/response bodies, HTTP framing, middleware, and routing.
- Reject malformed or unsupported values explicitly rather than silently
  substituting null or losing integer precision.

## Public Value API

`../../include/colt/json.hpp` defines `colt::Json` and `colt::JsonParseError`.
`include/colt/http.hpp` includes this header, making JSON available through
`<colt/application.hpp>` as well.

`Json` is an owning, copyable value backed by `std::variant`:

| JSON value | Stored C++ type |
|------------|-----------------|
| Null | `std::nullptr_t` |
| Boolean | `bool` |
| Signed integer | `std::int64_t` |
| Unsigned integer | `std::uint64_t` |
| Fractional/exponent number | `double` |
| String | `std::string` |
| Array | `Json::array`, an alias for `std::vector<Json>` |
| Object | `Json::object`, an alias for `std::map<std::string, Json, std::less<>>` |

Default construction and construction from `nullptr` produce JSON null.
Integral constructors exclude `bool` and accept types no wider than 64 bits,
preserving signedness. Strings can be constructed from `const char*`,
`std::string`, or `std::string_view`; string literals must not become booleans.
A null `const char*` throws `std::invalid_argument`.

The public operations are:

```cpp
static Json parse(std::string_view input,
                  std::size_t max_depth = default_max_depth);
std::string dump(std::size_t max_depth = default_max_depth) const;

template<class T> bool is() const noexcept;
bool is_null() const noexcept;
bool is_number() const noexcept;

template<class T> const T& get() const;
template<class T> T& get();

const Json& at(std::string_view key) const;
Json& at(std::string_view key);
const Json& at(std::size_t index) const;
Json& at(std::size_t index);
```

`get<T>()` and `is<T>()` use the exact stored alternative types. There are no
implicit conversions between number alternatives. Wrong-type access throws
`std::bad_variant_access`; missing object keys and out-of-range array indices
throw `std::out_of_range`. Mutation uses the mutable `get<T>()` and `at()`
overloads, including standard map/vector operations.

```cpp
auto value = colt::Json::parse(R"({"name":"Colt","enabled":true})");
const auto& name = value.at("name").get<std::string>();
value.get<colt::Json::object>()["tags"] = colt::Json::array{"http", "json"};
auto encoded = value.dump();
```

## Parsing Contract

The parser is a recursive-descent implementation operating on
`std::string_view`. The returned value owns its contents and does not depend
on the input lifetime.

Parsing accepts any JSON root value, including scalars. It consumes the whole
input except JSON whitespace: space, horizontal tab, line feed, and carriage
return. Empty input, trailing data, comments, trailing commas, malformed
literals, and invalid delimiters are errors. A UTF-8 byte-order mark is not
accepted.

Object keys must be strings and must be unique after decoding escapes. For
example, `"a"` and `"\u0061"` are duplicate keys. Duplicate objects are rejected,
not interpreted using first-key-wins or last-key-wins behavior.

### Numbers

Number syntax follows JSON grammar: no leading plus, no leading zeros except
zero itself, and at least one digit after a decimal point or exponent marker.
Conversion uses locale-independent `std::from_chars`.

Integer tokens without a fraction or exponent are stored exactly. Negative
integers must fit `std::int64_t`. Nonnegative integers use `std::int64_t` when
they fit and otherwise `std::uint64_t`. Integers outside those ranges are
rejected, not converted to double.

Tokens with a fraction or exponent use finite `double`. Normal binary
floating-point rounding applies; conversion range errors, including reported
underflow, are rejected. Infinity and NaN are not valid JSON tokens.
Integer `-0` becomes integer zero; floating-point negative zero is preserved.

### Strings And Unicode

Strings accept valid UTF-8 Unicode scalars and all JSON escapes:
`\"`, `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t`, and `\uXXXX`.
Escaped UTF-16 surrogate pairs are decoded to UTF-8.

Unescaped control bytes below `0x20`, incomplete/invalid escapes, unpaired
surrogates, invalid continuation bytes, overlong UTF-8, surrogate scalars, and
scalars above `U+10FFFF` are rejected. Escaped null bytes are retained inside
the owning string, including object keys.

### Errors And Depth

`JsonParseError` derives from `std::invalid_argument`. It exposes a zero-based
byte `offset()` and a `what()` diagnostic of the form
`JSON at byte <offset>: <reason>`. The offset describes the parser position
when the failure is detected.

`Json::default_max_depth` is 128. Each array or object counts as one level,
including an empty root container; scalar values do not add levels. Thus
`max_depth = 0` accepts scalars but rejects containers, and `max_depth = 1`
accepts `[0]` but rejects `[[]]`. Parsing rejects the next container before
descending beyond the configured limit.

## Serialization Contract

`dump()` produces compact JSON without indentation or optional whitespace.
Objects are emitted in the map's lexicographic key order, not insertion order.
This is deterministic ordering, not a general canonical-JSON guarantee.

Strings and object keys escape quotes, backslashes, and control bytes.
Standard short escapes are used where available; other controls use
`\u00xx`. Valid non-ASCII UTF-8 is retained rather than escaped. Invalid UTF-8
throws `std::invalid_argument`.

Integer output uses exact decimal formatting. Finite doubles use
locale-independent `std::to_chars` with round-trip precision. If the formatted
double has neither a decimal point nor an exponent, `.0` is appended so that
parsing the result retains the double alternative. Floating-point negative
zero is preserved.

NaN and infinity throw `std::invalid_argument`; they are not emitted as
nonstandard tokens or replaced with null. A number-formatting failure throws
`std::runtime_error`.

Serialization enforces the same container-counting depth contract as parsing,
using `std::invalid_argument` for depth violations. Constructors and mutable
access may hold invalid UTF-8, non-finite doubles, or deeper containers;
serialization is the validation boundary for those programmatically built
values. No partial output is returned after a failure.

## HTTP Integration

### Codec Plugins

`colt::JsonCodec` is a compile-time concept defined in `<colt/json.hpp>`.
An application adapter exposes its library's native value type and two static
functions:

```cpp
struct MyJsonCodec {
    using value_type = /* third-party library's owning JSON type */;
    static value_type parse(std::string_view input, std::size_t max_depth);
    static std::string dump(const value_type& value, std::size_t max_depth);
};
```

`parse()` must return exactly `value_type`, and `dump()` exactly `std::string`.
The concept checks those signatures, not behavioral correctness.
`BuiltinJsonCodec` implements this contract using `colt::Json`.

Plugins are selected explicitly at the call site; no runtime registry, global
backend switch, virtual interface, or automatic native-type inference is added.
Different codecs may coexist in one application, including in middleware.
Native values are not converted through `colt::Json`.

```cpp
auto native = request.json<MyJsonCodec>(64);
auto response = colt::Response::json<MyJsonCodec>(native, 201, 64);
```

The request overload returns `Codec::value_type`; the response overload takes
`const Codec::value_type&`, status (default 200), and maximum depth (default
128). Existing non-template helpers retain their signatures and delegate to
`BuiltinJsonCodec`, avoiding source and function-pointer compatibility changes.

Adapter authors must consume complete input, return values independent of the
request-body lifetime, enforce the supplied container depth limit on both
operations, and emit valid UTF-8 JSON. A root container counts as one level.
Library-specific numeric representation, duplicate-key handling, key ordering,
and formatting remain the plugin's policy; the built-in parsing/serialization
contracts above describe only `colt::Json`, not every plugin.

Adapters translate only library parse/validation failures to
`colt::JsonParseError`, with a zero-based detection offset and diagnostic.
`Request::json<Codec>()` wraps that error as `JsonBodyError`, preserving HTTP 400
handling and diagnostics. Unrelated exceptions (including allocation failures
and implementation bugs) must propagate, not be relabeled as client errors.
Serialization errors propagate to the existing HTTP 500 handler. Colt neither
reparses plugin output nor silently falls back to its built-in codec.

Applications include their adapter and library headers and link/manage that
library themselves. Colt ships no third-party-specific adapter or dependency.

### Request Bodies

`Request::json(max_depth = Json::default_max_depth) const` explicitly parses the
current body each time it is called. It does not cache results, alter the raw
body, automatically parse all requests, or require/validate `Content-Type`.
Existing HTTP body-byte limits still apply before dispatch; standalone JSON
parsing has no separate input-byte or element-count limit.

The helper catches `JsonParseError` and rethrows `JsonBodyError`, defined in
`http.hpp` as a subclass of `JsonParseError`. The original diagnostic and offset
are preserved. This distinguishes client request-body errors from parsing
failures in application-owned data.

`Application::dispatch()` catches uncaught `JsonBodyError` before its existing
general exception handler and returns `Response::text("Bad Request", 400)`.
Middleware and handlers may catch it to provide a custom error response.

### JSON Responses

`Response::json(const Json& value, int status = 200)` returns a response with
the requested status, `Content-Type: application/json`, and `value.dump()` as
its body.

```cpp
app.post("/echo", [](const colt::Request& request, colt::RequestContext&) {
    return colt::Response::json(request.json());
});

auto created = colt::Response::json(colt::Json::object{{"id", 42}}, 201);
```

Response serialization uses the default JSON depth limit. The existing HTTP
serializer remains responsible for byte-count `Content-Length`, connection
headers, status validation, and suppressing bodies for bodyless statuses.
Applications needing a different output depth can call `dump(max_depth)` and
construct a response manually.

### Failure And Connection Behavior

| Uncaught failure during dispatch | HTTP result |
|---------------------------------|-------------|
| Invalid body through `Request::json()` or `json<Codec>()`, reported as `JsonParseError` | 400 Bad Request |
| Application-side `Json::parse()` failure | 500 Internal Server Error |
| Unnormalized plugin exception or application-side codec parse failure | 500 Internal Server Error |
| Wrong JSON type, missing key, or invalid index | 500 Internal Server Error |
| Invalid JSON during response serialization | 500 Internal Server Error |

The existing server connection policy is unchanged: responses with status 400,
413, or 500 and above close the connection. Valid JSON responses use ordinary
keep-alive unless the request or response asks to close.

## Changed Surfaces

| File | Change |
|------|--------|
| `../../include/colt/json.hpp` | Owning JSON value, parser, serializer, parse error, codec concept, and built-in adapter |
| `../../include/colt/http.hpp` | Body error type, default helpers, and explicit codec helper overloads |
| `../../include/colt/application.hpp` | Request JSON parse failures become HTTP 400 |
| `../../tests/json_test.cpp` | New JSON and dispatch coverage |
| `../../tests/server_test.cpp` | Loopback JSON responses and connection-policy coverage |
| `../../tests/consumer_smoke.cpp` | JSON usage through the public consumer target |
| `../../CMakeLists.txt` | Include JSON tests and register the `json` CTest selector |
| `../../examples/hello.cpp` | Add `POST /echo` JSON route |
| `../../README.md` | Document JSON API, limits, failure behavior, and example |

No new third-party package, platform-specific JSON source, or consumer link
requirement is introduced.

## Acceptance Coverage

The repository test harness covers every value alternative, nested and empty
containers, construction, mutation, checked access, and complete-input parsing.
Numeric cases include signed/unsigned boundaries, integers above double's exact
integer range, exponent syntax, overflow/underflow, negative zero, subnormals,
and generated finite-double bitwise round trips.

String cases include all ASCII bytes through serialization, JSON escapes,
embedded nulls in values and keys, Unicode scalar boundaries, surrogate pairs,
invalid UTF-8, and malformed/truncated input. Grammar coverage includes
duplicate decoded keys, invalid numbers, literals, delimiters, and trailing
data. Depth cases verify exact limits for both parsing and serialization.

HTTP coverage checks explicit body parsing, JSON content type, status and
byte-count framing, request-only 400 mapping, application-side 500 behavior,
custom middleware errors, loopback echoing, valid-response keep-alive, and
connection closure after malformed JSON.

Plugin acceptance coverage must verify concept rejection of invalid adapters,
native values unrelated to `colt::Json`, exact input/depth forwarding, unchanged
default calls, normalized parse diagnostics and HTTP 400, unexpected parse and
serialization exceptions remaining HTTP 500, middleware usage, response framing,
and loopback operation. A dependency-free test codec is sufficient to validate
the generic contract; library-specific behavior belongs to application adapters.
The consumer smoke test must exercise a custom codec through `Colt::colt`.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure -R '^(json|consumer_smoke)$'
./build/colt_tests
```

Implementation validation also exercised standalone public-header compilation
with strict Clang warnings and JSON tests under address and undefined-behavior
sanitizers. These local checks do not replace cross-platform CI.

## Non-Goals

This change does not add automatic conversion to application structs,
reflection, schema validation, implicit numeric coercion, arbitrary-precision
numbers, streaming/incremental JSON, pretty printing, configurable duplicate-key
policies, automatic content negotiation, or request JSON caching.
It also does not add bundled third-party adapters, runtime plugin loading, or
global codec configuration. Third-party plugins may expose library capabilities
through their native values without adding those capabilities to `colt::Json`.

TLS, WebSockets, asynchronous handlers, chunked request bodies, static-file
hosting, and multipart parsing remain outside the existing framework scope.
