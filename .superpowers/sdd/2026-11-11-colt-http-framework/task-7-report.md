# Task 7 Implementation And Verification Report

## Scope

Implemented Task 7 only: public example, consumer README, CMake example option,
cross-compiler GitHub Actions matrix, and a standard-library loopback smoke
test. No runtime framework code or third-party dependencies were added.

The requested brief path at the repository root was unavailable. The matching
brief at
`.worktrees/colt-http-framework/.superpowers/sdd/2026-11-11-colt-http-framework/task-7-brief.md`
was used as the authority.

Changes:

- Added `examples/hello.cpp`, which links through the public `Colt::colt`
  target and demonstrates a GET route, path parameter, middleware, singleton
  and request-scoped DI, `listen()`, and `stop()`.
- Added `COLT_BUILD_EXAMPLES`, defaulting to `ON`, and a `colt_hello` target.
  `COLT_BUILD_TESTS` already existed and defaults to `ON`.
- Expanded README build/consumer instructions, C++23/compiler-platform
  requirements, public API descriptions, server and HTTP limits, and deferred
  protocol features.
- Added GitHub Actions jobs for GCC/Linux, Clang/macOS, and MSVC/Windows. Each
  configures, builds, runs CTest, and executes the Python-standard-library
  loopback smoke script against `colt_hello`.
- Added `tests/example_smoke.py`, which checks HTTP 200 and exact response body
  `Hello, Colt!`, then stops the example through stdin.
- Documented that request-target checking is not complete URI-character
  validation, preserving the parked parser concern from the SDD ledger.

The pre-existing untracked `docs/` directory was not modified or staged.

## Verification

Commands ran in
`/Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework`
using Clang 23.1.5 on macOS.

Enabled development configuration, build, full CTest, loopback example,
consumer-only configuration/build, and whitespace check:

```text
cmake -S . -B build -DCOLT_BUILD_TESTS=ON -DCOLT_BUILD_EXAMPLES=ON && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure && python3 tests/example_smoke.py build/colt_hello && cmake -S . -B build-consumer -DCOLT_BUILD_TESTS=OFF -DCOLT_BUILD_EXAMPLES=OFF && cmake --build build-consumer --config Release && git diff --check
```

Exact output:

```text
-- Configuring done (0.1s)
-- Generating done (0.1s)
-- Build files have been written to: /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
[ 13%] Built target colt
[ 73%] Built target colt_tests
[ 86%] Built target colt_consumer_smoke
[100%] Built target colt_hello
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 1: colt_tests
1/9 Test #1: colt_tests .......................   Passed    1.03 sec
    Start 2: consumer_smoke
2/9 Test #2: consumer_smoke ...................   Passed    0.02 sec
    Start 3: socket
3/9 Test #3: socket ...........................   Passed    0.03 sec
    Start 4: http_parser
4/9 Test #4: http_parser ......................   Passed    0.01 sec
    Start 5: http_serializer
5/9 Test #5: http_serializer ..................   Passed    0.01 sec
    Start 6: context
6/9 Test #6: context ..........................   Passed    0.01 sec
    Start 7: routing
7/9 Test #7: routing ..........................   Passed    0.01 sec
    Start 8: middleware
8/9 Test #8: middleware .......................   Passed    0.01 sec
    Start 9: server
9/9 Test #9: server ...........................   Passed    1.02 sec

100% tests passed, 0 tests failed out of 9

Total Test time (real) =   2.16 sec
Listening on http://127.0.0.1:8080/hello/Colt
Press Enter to stop.
-- Configuring done (0.2s)
-- Generating done (0.0s)
-- Build files have been written to: /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build-consumer
[100%] Built target colt
```

The loopback script exited 0 after receiving status 200 and body `Hello,
Colt!`; it sent Enter and waited for clean server shutdown. The chained command
continued through consumer-only configuration/build and `git diff --check` and
exited 0. `git diff --check` emitted no output.

The consumer-only build used both options off. It configured the `colt` library
and compiled/linked the socket layer without tests or examples.

## Review And Limitations

- Reviewed the README API references against `include/colt/application.hpp`,
  `include/colt/context.hpp`, and `include/colt/http.hpp`; reviewed the final
  change scope and `git diff --check`.
- The example smoke test uses only Python's standard library. Python is already
  available on the GitHub-hosted runners and is not a Colt build/runtime
  dependency.
- Local verification was performed only on Clang/macOS. GCC/Linux and
  MSVC/Windows builds and the workflow itself require hosted CI execution and
  were not run locally.
- Request-target validation has the parked URI-character validation gap
  recorded in `progress.md`. README explicitly states that target validation is
  not complete URI-character validation; this task did not change parser code.
- No subagents were used.

## Commit

Task 7 implementation commit: recorded after commit creation.
