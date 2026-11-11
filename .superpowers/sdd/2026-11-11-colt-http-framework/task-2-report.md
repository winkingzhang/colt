# Task 2 Implementation, Test, And Self-Review Report

## Scope

Implemented only Task 2, HTTP values, parsing, and response serialization:

- Completed the existing `colt::Response` in place and added `Headers`, `Request`, and the default `HttpLimits` values.
- Added a header-only HTTP/1.1 request parser and response serializer in `include/colt/detail/http_parser.hpp`.
- Added focused parser and serializer tests, along with CTest filters to run them separately.
- Updated the small test harness with an optional test-name filter so the focused CTest entries run only relevant tests.
- Did not alter the socket layer or implement later task features.

## TDD And Verification Results

All commands below ran from `/Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework`.

### Expected Red Phase

`cmake --build build && ctest --test-dir build -R 'http_parser|http_serializer' --output-on-failure`

```text
[ 28%] Built target colt
[ 42%] Building CXX object CMakeFiles/colt_tests.dir/tests/test_main.cpp.o
[ 57%] Building CXX object CMakeFiles/colt_tests.dir/tests/http_parser_test.cpp.o
/Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/tests/http_parser_test.cpp:3:10: fatal error: 'colt/detail/http_parser.hpp' file not found
    3 | #include <colt/detail/http_parser.hpp>
      |          ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~
1 error generated.
```

Result: expected nonzero build exit because the parser/types did not exist.

### Focused HTTP Tests

`cmake -S . -B build -DCOLT_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build -R 'http_parser|http_serializer' --output-on-failure`

```text
-- Configuring done (0.1s)
-- Generating done (0.0s)
-- Build files have been written to: /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
[ 28%] Built target colt
[ 42%] Building CXX object CMakeFiles/colt_tests.dir/tests/http_parser_test.cpp.o
[ 57%] Building CXX object CMakeFiles/colt_tests.dir/tests/http_serializer_test.cpp.o
[ 71%] Linking CXX executable colt_tests
[100%] Built target colt_tests
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 2: http_parser
1/2 Test #2: http_parser ......................   Passed    0.53 sec
    Start 3: http_serializer
2/2 Test #3: http_serializer ..................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 2
```

Result: exit 0.

### Full Clang Build And Test Suite

`cmake -S . -B build -DCOLT_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build --output-on-failure`

```text
-- Configuring done (0.1s)
-- Generating done (0.0s)
-- Build files have been written to: /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
[ 28%] Built target colt
[ 42%] Building CXX object CMakeFiles/colt_tests.dir/tests/http_parser_test.cpp.o
[ 57%] Building CXX object CMakeFiles/colt_tests.dir/tests/http_serializer_test.cpp.o
[ 71%] Linking CXX executable colt_tests
[100%] Built target colt_tests
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 1: colt_tests
1/3 Test #1: colt_tests .......................   Passed    0.01 sec
    Start 2: http_parser
2/3 Test #2: http_parser ......................   Passed    0.01 sec
    Start 3: http_serializer
3/3 Test #3: http_serializer ..................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 3
```

Result: exit 0. The configured compiler in `build/CMakeCache.txt` is `/opt/homebrew/opt/llvm/bin/clang++`, Clang 23.1.5 on macOS.

### GCC-Identified Compiler Build And Tests

`cmake -S . -B build-gcc -DCOLT_BUILD_TESTS=ON -DCMAKE_CXX_COMPILER=/usr/bin/g++ && cmake --build build-gcc && ctest --test-dir build-gcc --output-on-failure`

```text
-- The CXX compiler identification is AppleClang 21.0.0.21000401
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/bin/g++ - skipped
-- Detecting CXX compile features - done
-- Configuring done (0.1s)
-- Generating done (0.0s)
-- Build files have been written to: /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build-gcc
[ 28%] Built target colt
[ 42%] Building CXX object CMakeFiles/colt_tests.dir/tests/test_main.cpp.o
[ 57%] Building CXX object CMakeFiles/colt_tests.dir/tests/build_test.cpp.o
[ 71%] Building CXX object CMakeFiles/colt_tests.dir/tests/http_parser_test.cpp.o
[ 85%] Building CXX object CMakeFiles/colt_tests.dir/tests/http_serializer_test.cpp.o
[100%] Linking CXX executable colt_tests
[100%] Built target colt_tests
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build-gcc
    Start 1: colt_tests
1/3 Test #1: colt_tests .......................   Passed    0.45 sec
    Start 2: http_parser
2/3 Test #2: http_parser ......................   Passed    0.01 sec
    Start 3: http_serializer
3/3 Test #3: http_serializer ..................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 3
```

Result: exit 0. `/usr/bin/g++ --version` reports Apple clang masquerading as the system `g++` driver, not GNU GCC; this validates the alternate driver invocation only, not a GNU GCC implementation. The temporary `build-gcc/` directory was removed with `cmake -E rm -rf build-gcc` after verification.

### Clang Warning-Enabled Direct Build

`/opt/homebrew/opt/llvm/bin/clang++ -std=c++23 -Wall -Wextra -Wpedantic -Iinclude tests/test_main.cpp tests/build_test.cpp tests/http_parser_test.cpp tests/http_serializer_test.cpp src/platform/socket.cpp -o /var/folders/rk/3m41sv2x38nd5tyx7712v57c0000gp/T/opencode/colt_tests_clang && /var/folders/rk/3m41sv2x38nd5tyx7712v57c0000gp/T/opencode/colt_tests_clang`

```text
[pass] default response has status 200 and empty body
[pass] parser limits expose the planned request line header and body defaults
[pass] parser extracts a complete GET request and query parameters
[pass] parser accepts a content-length POST body
[pass] parser reports incomplete request lines headers and bodies
[pass] parser accepts the exact configured request line header and body limits
[pass] parser marks over-limit declared bodies for close
[pass] parser rejects malformed request lines
[pass] parser rejects duplicate and conflicting content lengths and closes
[pass] parser rejects transfer encoding including combined framing
[pass] parser rejects invalid percent escapes in query parameters
[pass] parser splits query pairs before decoding and preserves encoded path delimiters
[pass] parser honors an HTTP/1.1 connection close request
[pass] parser preserves pipelined bytes after one request
[pass] headers lookup is case insensitive while preserving values
[pass] serializer emits a default 200 response
[pass] serializer emits custom status headers and byte-count content length
[pass] serializer replaces supplied content length
[pass] serializer enforces keep alive or close connection policy
```

Result: exit 0, no compiler warnings printed, 19 tests passed.

`git diff --check`

```text
(no output)
```

Result: exit 0.

## Self-Review

- The pre-existing `Response` was completed in place; only one definition exists.
- Header name comparisons are ASCII case-insensitive while original field names and values are stored; lookup returns the first value, `set` replaces matching values, and `add` preserves duplicates.
- Parser defaults match the brief: request line 8 KiB, aggregate headers 32 KiB, body 1 MiB. Equality at the configured body/request-line/header limits and over-limit results are covered.
- Parsing accepts HTTP/1.1 origin-form targets, enforces request/header syntax, accounts incrementally for partial CRLFs, rejects duplicate `Content-Length` (equal or conflicting), all `Transfer-Encoding`, unsupported combined framing, invalid numeric lengths, and malformed query/path escapes. `bytes_consumed` stops after exactly one declared request body, preserving pipelined bytes.
- Query parsing splits at `&` and the first `=` before percent decoding, converts `+` to space, preserves encoded delimiters, and rejects malformed `%` sequences.
- The serializer computes byte length from the response body and replaces provided content-length/connection policy headers. Common status reasons are mapped; unknown status values serialize with `Unknown`.
- Focused CTest filters are implemented in the existing tiny harness rather than registering multiple names that run the full suite.
- Platform verification was limited to macOS. Clang 23.1.5 passed; the system `g++` command is AppleClang, so GNU GCC, MSVC, Linux, and Windows were not verified in this environment.
- Existing untracked `docs/` files were present before Task 2 and were left untouched.

## Fix Follow-Up: Response Header Injection

The serializer originally emitted caller-provided field names and values verbatim. It now validates every header name against HTTP token characters and rejects field values containing NUL, CR, LF, DEL, or other controls except horizontal tab. Invalid fields throw `std::invalid_argument` before any serialized string is returned. Regression tests cover CR/LF-bearing field names, CR/LF values, NUL, and another control byte.

TDD red result, from `cmake --build build && ctest --test-dir build -R 'http_serializer' --output-on-failure` before the serializer validation was implemented:

```text
[pass] serializer emits a default 200 response
[pass] serializer emits custom status headers and byte-count content length
[pass] serializer replaces supplied content length
[pass] serializer enforces keep alive or close connection policy
[fail] serializer rejects invalid response header names: .../tests/http_serializer_test.cpp:50: REQUIRE(rejected) failed
[fail] serializer rejects response header values containing controls: .../tests/http_serializer_test.cpp:64: REQUIRE(rejected) failed
0% tests passed, 1 tests failed out of 1
```

After the fix, the focused serializer command was rerun:

```text
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 3: http_serializer
1/1 Test #3: http_serializer ..................   Passed    0.50 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   0.50 sec
```

The full suite was rerun with `cmake --build build && ctest --test-dir build --output-on-failure`:

```text
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 1: colt_tests
1/3 Test #1: colt_tests .......................   Passed    0.01 sec
    Start 2: http_parser
2/3 Test #2: http_parser ......................   Passed    0.01 sec
    Start 3: http_serializer
3/3 Test #3: http_serializer ..................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 3

Total Test time (real) =   0.03 sec
```

Command sequence: `cmake --build build && ctest --test-dir build -R 'http_serializer' --output-on-failure && ctest --test-dir build --output-on-failure && git diff --check`. Exit 0. `cmake --build build` output was `[ 28%] Built target colt` and `[100%] Built target colt_tests`; `git diff --check` produced no output.

`git diff --cached --check` also completed with exit 0 and no output after staging the intended Task 2 files and this report. Existing untracked `docs/` was not staged.

Implementation commit: `1626120` (`feat: add bounded HTTP parsing and serialization`). It includes the report and all Task 2 implementation/tests. `git status --short --branch` after the commit showed only the pre-existing untracked `docs/` directory, which was not staged. `git diff HEAD^ HEAD --check` completed with exit 0 and no output.

## Review Follow-Up: Host And Response Framing Semantics

Addressed four reviewer findings with regression tests:

- HTTP/1.1 requests must contain exactly one nonempty, case-insensitive `Host`; missing, duplicate, or empty values return `bad_request` with connection close.
- Serialization rejects any caller-provided `Transfer-Encoding` with `std::invalid_argument` to avoid emitting it with a `Content-Length`.
- Serialization rejects status values outside the valid 100-599 range with `std::invalid_argument`.
- 1xx, 204, 205, and 304 responses omit both body bytes and `Content-Length`; status phrases were added for 100, 205, and 304.

The new tests were first run against the old implementation using `cmake --build build && ctest --test-dir build -R 'http_parser|http_serializer' --output-on-failure`. Both focused CTest entries failed as expected: parser failed because a missing Host was accepted; serializer failed for accepted Transfer-Encoding, invalid status values, and body-bearing no-content statuses.

After the fixes, the final verification command was `cmake --build build && ctest --test-dir build -R 'http_parser|http_serializer' --output-on-failure && ctest --test-dir build --output-on-failure && git diff --check`:

```text
[ 28%] Built target colt
[ 42%] Building CXX object CMakeFiles/colt_tests.dir/tests/http_parser_test.cpp.o
[ 57%] Building CXX object CMakeFiles/colt_tests.dir/tests/http_serializer_test.cpp.o
[ 71%] Linking CXX executable colt_tests
[100%] Built target colt_tests
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 2: http_parser
1/2 Test #2: http_parser ......................   Passed    0.41 sec
    Start 3: http_serializer
2/2 Test #3: http_serializer ..................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 2

Total Test time (real) =   0.42 sec
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 1: colt_tests
1/3 Test #1: colt_tests .......................   Passed    0.01 sec
    Start 2: http_parser
2/3 Test #2: http_parser ......................   Passed    0.01 sec
    Start 3: http_serializer
3/3 Test #3: http_serializer ..................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 3

Total Test time (real) =   0.04 sec
```

`git diff --check` exited 0 with no output. Review-fix commit: `82cdc69` (`fix: enforce HTTP host and response framing rules`). The staged files were limited to `include/colt/detail/http_parser.hpp`, `tests/http_parser_test.cpp`, `tests/http_serializer_test.cpp`, and this report; existing untracked `docs/` was left unstaged. `git diff HEAD^ HEAD --check` exited 0 with no output.

## Review Follow-Up: Host Authority Validation

Added focused validation for exactly one HTTP/1.1 Host authority. Unbracketed authorities accept a nonempty reg-name (including valid percent-encoded octets) with an optional numeric port from 0 through 65535. Bracketed hosts accept IPv6 or IPvFuture literals, optionally followed by a numeric port. Empty hosts/ports, malformed percent escapes, invalid port syntax or overflow, unbracketed IPv6, malformed brackets, userinfo, whitespace/control, and path/query suffixes return `bad_request` and close the connection. Regression tests cover accepted DNS/IPv4-like and bracketed IPv6 forms and the requested rejection cases.

The focused parser test was first run before implementing the validator with `cmake --build build && ctest --test-dir build -R 'http_parser' --output-on-failure` and failed as expected:

```text
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 2: http_parser
1/1 Test #2: http_parser ......................***Failed    0.41 sec
[pass] parser limits expose the planned request line header and body defaults
[pass] parser extracts a complete GET request and query parameters
[pass] parser accepts a content-length POST body
[pass] parser reports incomplete request lines headers and bodies
[pass] parser rejects requests without exactly one nonempty Host header
[pass] parser accepts valid Host authorities
[fail] parser rejects malformed Host authorities and closes: .../tests/http_parser_test.cpp:85: REQUIRE(result.status == ParseStatus::bad_request) failed
0% tests passed, 1 tests failed out of 1
```

The final command was `cmake --build build && ctest --test-dir build -R 'http_parser|http_serializer' --output-on-failure && ctest --test-dir build --output-on-failure && git diff --check`:

```text
[ 28%] Built target colt
[ 42%] Building CXX object CMakeFiles/colt_tests.dir/tests/http_parser_test.cpp.o
[ 57%] Linking CXX executable colt_tests
[100%] Built target colt_tests
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 2: http_parser
1/2 Test #2: http_parser ......................   Passed    0.41 sec
    Start 3: http_serializer
2/2 Test #3: http_serializer ..................   Passed    0.02 sec

100% tests passed, 0 tests failed out of 2

Total Test time (real) =   0.44 sec
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 1: colt_tests
1/3 Test #1: colt_tests .......................   Passed    0.01 sec
    Start 2: http_parser
2/3 Test #2: http_parser ......................   Passed    0.01 sec
    Start 3: http_serializer
3/3 Test #3: http_serializer ..................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 3

Total Test time (real) =   0.04 sec
```

The trailing `git diff --check` exited 0 with no output. Commit hash will be entered after this fix is committed.
