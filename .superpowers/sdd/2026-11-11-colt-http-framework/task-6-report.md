# Task 6 Implementation And Verification Report

## Scope

Implemented Task 6 only: server lifecycle, bounded per-connection workers, HTTP request processing, configured limits
and keep-alive timeout, loopback coverage, and the directly relevant parked request-target validation fix.

Changes:

- Added `colt::ServerSettings` with `max_connections{256}`, `keep_alive_timeout{30s}`, and default `HttpLimits`.
- Added `Application::listen`, `bound_port`, and idempotent `stop`; application owns the accept thread, worker threads,
  listener, and tracked active sockets. No thread is detached.
- Added a context-taking dispatch overload; the existing `dispatch(Request)` still creates a fresh context. Network
  processing creates exactly one `RequestContext` per parsed request.
- Added sequential request processing on each connection. Unconsumed/pipelined bytes remain buffered and are parsed
  before reading more socket data.
- Added `include/colt/detail/platform_socket.hpp` as the OS-neutral declaration needed by the public header-only
  application API; `src/platform/socket.hpp` now forwards to it. OS calls remain in the compiled socket implementation.
- Added loopback tests for defaults, routes/path parameters, 404, malformed request 400, invalid request-target 400,
  handler exception 500, request-scope freshness, singleton sharing, keep-alive, response-directed close, timeout,
  request limits/413, zero and positive connection caps, new admission after worker exit, and stop while a worker is
  blocked reading.
- Preserved the unrelated untracked root `docs/` directory; it was not modified or staged.

The requested brief path at the repository root did not exist. The matching brief in the active worktree,
`.worktrees/colt-http-framework/.superpowers/sdd/2026-11-11-colt-http-framework/task-6-brief.md`, was used as the
authority.

## TDD Evidence

Added server tests and CTest registration before server implementation. Initial focused build failed on the missing
`Application::bound_port`, `listen`, `stop`, and `ServerSettings` declarations, as expected.

The first implementation exposed a blocked accept thread during shutdown: focused CTest exceeded the 120-second command
timeout, and the individually isolated request tests also exceeded 12 seconds. The blocked-read stop test passed. The
root cause was that shutting down the listening socket alone did not reliably wake `accept()` on the local platform.
`stop()` now attempts a local wake-up connection as well as shutting down the listener and active connections;
subsequent shutdown tests passed repeatedly.

Added the invalid-target integration regression before parser changes. The focused run failed specifically because a
quoted target did not return 400:

```text
[pass] server routes requests and returns 404 for unmatched paths
[fail] server returns 400 for malformed requests and 500 for handler exceptions: ... REQUIRE(read_response(invalid_target, buffered).find("HTTP/1.1 400 ") == 0) failed
[pass] server creates request contexts per keep-alive connection and shares singletons
[pass] server closes idle keep-alive connections after configured timeout
[pass] server closes when a response requests connection close
[pass] server rejects connections when maximum is zero
[pass] server enforces the active connection limit
[pass] server admits a new connection after a worker exits
[pass] server stop interrupts a worker blocked reading a request
```

Added HTTP request-target character validation and parser-level tests for quote, backslash, non-ASCII, and square
brackets. The Task 2 parked request-target issue is thereby carried into Task 6 and resolved only to the extent needed
for malformed request behavior; no adjacent parser work was included.

## Final Verification

All commands ran in `/Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework` using the existing
Clang/macOS build directory.

Command:

```text
cmake --build build && ctest --test-dir build -R server --output-on-failure && ctest --test-dir build -R server --repeat until-fail:20 --output-on-failure && ctest --test-dir build --output-on-failure && git diff --check
```

Exact output:

```text
[ 18%] Built target colt
[100%] Built target colt_tests
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 8: server
1/1 Test #8: server ...........................   Passed    1.03 sec

100% tests passed, 0 tests failed out of 1
Total Test time (real) =   1.04 sec
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 8: server
    Test #8: server ...........................   Passed    1.03 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.04 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.04 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.04 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.04 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.05 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.04 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.04 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.04 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.04 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.04 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.05 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.04 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.04 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.03 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.04 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.04 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.04 sec
    Start 8: server
    Test #8: server ...........................   Passed    1.04 sec
    Start 8: server
1/1 Test #8: server ...........................   Passed    1.04 sec

100% tests passed, 0 tests failed out of 1
Total Test time (real) =  20.84 sec
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 1: colt_tests
1/8 Test #1: colt_tests .......................   Passed    1.05 sec
    Start 2: socket
2/8 Test #2: socket ...........................   Passed    0.03 sec
    Start 3: http_parser
3/8 Test #3: http_parser ......................   Passed    0.02 sec
    Start 4: http_serializer
4/8 Test #4: http_serializer ..................   Passed    0.01 sec
    Start 5: context
5/8 Test #5: context ..........................   Passed    0.02 sec
    Start 6: routing
6/8 Test #6: routing ..........................   Passed    0.01 sec
    Start 7: middleware
7/8 Test #7: middleware .......................   Passed    0.02 sec
    Start 8: server
8/8 Test #8: server ...........................   Passed    1.03 sec

100% tests passed, 0 tests failed out of 8
Total Test time (real) =   2.20 sec
```

`git diff --check` emitted no output and exited 0.

## Review And Limitations

- Manually reviewed the complete Task 6 diff and the ownership/shutdown flow. No subagents were used, as instructed;
  this is author self-review rather than independent review.
- Server-focused CTest passed 20 consecutive repetitions and the final full CTest run passed 8/8.
- Only macOS with Clang was available for native verification. GCC/Linux and MSVC/Windows were not built in this task;
  the Windows socket implementation remains locally unverified.
- No report commit is recorded here; the implementation commit is listed after it is created.

## Commit

Implementation commit: `a477f86` (`feat: add HTTP server lifecycle and workers`).

## Review Follow-Up: Public Thread Linkage

The review finding was confirmed: `Threads::Threads` was discovered only inside the test block, and only the test
executable linked it directly. Consequently, consumers linking `Colt::colt` did not inherit the thread usage requirement
even though the public application header exposes `std::thread`.

Moved `find_package(Threads REQUIRED)` to the library configuration and linked `Threads::Threads` PUBLIC to `colt`.
Added a configure-time assertion that the exported interface contains `Threads::Threads` and `tests/consumer_smoke.cpp`,
which includes `colt/application.hpp`, uses `std::thread`, and links only `Colt::colt`.

### Red Phase

Before adding the public link dependency, ran:

```text
cmake -S . -B build -DCOLT_BUILD_TESTS=ON
```

Exact result:

```text
CMake Error at CMakeLists.txt:34 (message):
  Colt::colt must publish Threads::Threads

-- Configuring incomplete, errors occurred!
```

### Verification

Ran:

```text
cmake -S . -B build -DCOLT_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build --output-on-failure && git diff --check
```

Exact output:

```text
-- Configuring done (0.2s)
-- Generating done (0.0s)
-- Build files have been written to: /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
[ 15%] Built target colt
[ 84%] Built target colt_tests
[ 92%] Building CXX object CMakeFiles/colt_consumer_smoke.dir/tests/consumer_smoke.cpp.o
[100%] Linking CXX executable colt_consumer_smoke
[100%] Built target colt_consumer_smoke
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 1: colt_tests
1/9 Test #1: colt_tests .......................   Passed    1.04 sec
    Start 2: consumer_smoke
2/9 Test #2: consumer_smoke ...................   Passed    1.09 sec
    Start 3: socket
3/9 Test #3: socket ...........................   Passed    0.02 sec
    Start 4: http_parser
4/9 Test #4: http_parser ......................   Passed    0.02 sec
    Start 5: http_serializer
5/9 Test #5: http_serializer ..................   Passed    0.01 sec
    Start 6: context
6/9 Test #6: context ..........................   Passed    0.01 sec
    Start 7: routing
7/9 Test #7: routing ..........................   Passed    0.01 sec
    Start 8: middleware
8/9 Test #8: middleware .......................   Passed    0.01 sec
    Start 9: server
9/9 Test #9: server ...........................   Passed    1.03 sec

100% tests passed, 0 tests failed out of 9
Total Test time (real) =   3.26 sec
```

`git diff --check` emitted no output and exited 0. The active worktree contained only the unrelated untracked `docs/`
directory after commit; it was not staged or modified.

Follow-up commit: `e3cbe66d9cf956fe22466f2d270849e1619391ed` (`fix: propagate thread linkage to consumers`).
