# Task 4 Implementation And Verification Report

## Scope

Implemented Task 4 only:

- Added header-only `colt::Application`, route helpers, middleware, dispatch, and application-level DI registration
  forwarding in `include/colt/application.hpp`.
- Added route and middleware tests in `tests/routing_test.cpp` and `tests/middleware_test.cpp`.
- Registered both test sources and the focused `routing` and `middleware` CTest entries in `CMakeLists.txt`.
- Preserved the existing parser and its tests. The parked Task 2 raw request-target-character finding was not changed.

`dispatch` creates one `RequestContext` per request; matches exact method and segment count; splits raw paths before
percent-decoding segments; places decoded matched parameters in `Request::path_params`; and returns 400 for malformed
path escapes. Middleware is invoked in registration order and unwinds naturally as continuations return.
Handler/middleware exceptions and repeated calls to a middleware's own `next` become 500 responses. A repeated
continuation does not dispatch the downstream chain again.

## TDD And Verification

Commands below ran from `/Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework`.

### Initial Expected Red Phase

After adding the tests and CMake registration, before creating `include/colt/application.hpp`, ran:

`cmake --build build && ctest --test-dir build -R 'routing|middleware' --output-on-failure`

Relevant output:

```text
[ 30%] Building CXX object CMakeFiles/colt_tests.dir/tests/routing_test.cpp.o
.../tests/routing_test.cpp:3:10: fatal error: 'colt/application.hpp' file not found
    3 | #include <colt/application.hpp>
      |          ^~~~~~~~~~~~~~~~~~~~~~
1 error generated.
make[2]: *** [CMakeFiles/colt_tests.dir/tests/routing_test.cpp.o] Error 1
make[1]: *** [CMakeFiles/colt_tests.dir/all] Error 2
make: *** [all] Error 2
```

Result: expected nonzero build exit because the application interface had not been implemented.

### DI Forwarding Red/Green

To verify the explicit DI forwarding requirement independently, temporarily removed both forwarding methods while
retaining the new test, then ran:

`cmake --build build && ctest --test-dir build -R routing --output-on-failure`

It failed at compile time as expected:

```text
tests/routing_test.cpp:105:9: error: no member named 'add_request' in 'colt::Application'
tests/routing_test.cpp:106:9: error: no member named 'add_singleton' in 'colt::Application'
4 errors generated.
make[2]: *** [CMakeFiles/colt_tests.dir/tests/routing_test.cpp.o] Error 1
make[1]: *** [CMakeFiles/colt_tests.dir/all] Error 2
make: *** [Makefile:101: all] Error 2
```

Restored the forwarding methods and ran the focused and full verification command:

`cmake --build build && ctest --test-dir build -R 'routing|middleware' --output-on-failure && ctest --test-dir build --output-on-failure && git diff --check`

Output:

```text
 [20%] Built target colt
 [100%] Built target colt_tests
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 5: routing
1/2 Test #5: routing ..........................   Passed    0.01 sec
    Start 6: middleware
2/2 Test #6: middleware .......................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 2
Total Test time (real) =   0.04 sec
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 1: colt_tests
1/6 Test #1: colt_tests .......................   Passed    0.01 sec
    Start 2: http_parser
2/6 Test #2: http_parser ......................   Passed    0.01 sec
    Start 3: http_serializer
3/6 Test #3: http_serializer ..................   Passed    0.01 sec
    Start 4: context
4/6 Test #4: context ..........................   Passed    0.01 sec
    Start 5: routing
5/6 Test #5: routing ..........................   Passed    0.01 sec
    Start 6: middleware
 6/6 Test #6: middleware .......................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 6
Total Test time (real) =   0.06 sec
```

Result: build succeeded, focused tests passed 2/2, full CTest passed 6/6, and `git diff --check` produced no output
(exit 0).

### Review

- Reviewed the new route segmentation/decoding, route matching, middleware continuation state, and CMake registration
  against the Task 4 brief.
- Confirmed tests cover exact method/path, multi-parameter extraction, decoded values, encoded slash boundary
  preservation, wrong method and segment count 404, unmatched route 404, malformed escape 400, middleware
  request/response order, short circuit, handler and middleware exceptions, repeated `next()` returning 500 without
  duplicate handler invocation, and singleton/request DI forwarding.
- No subagents were used, as instructed. Review was performed inline by the implementer.
- `git diff --check` passed. The unrelated pre-existing untracked `docs/` directory was not staged.
- Verification was run with the available Clang toolchain on macOS. GCC/Linux and MSVC/Windows were not available
  locally and were not tested.

## Commit

Implementation commit: `ce11e16` (`feat: add route dispatch and synchronous middleware`).

## Middleware Path-Parameter Review Follow-Up

Reviewer found middleware could not read matched path parameters because route matching and parameter assignment
occurred only when the continuation reached the handler. Added a regression test that reads
`request.path_params.at("id")` before calling `next()`. It failed before the fix with the expected failed body
assertion. Dispatch now resolves the first matching method/path and assigns its decoded parameters before entering
middleware; the selected handler is still invoked only when the continuation chain reaches its terminal step. Unmatched
valid paths still return 404 from that terminal step.

Implementation commit: `ff2e9c1` (`fix: populate route params before middleware`).

Post-commit verification command:

`cmake --build build && ctest --test-dir build -R 'routing|middleware' --output-on-failure && ctest --test-dir build --output-on-failure && git diff --check && git rev-parse --short HEAD`

Exact output:

```text
[ 20%] Built target colt
[100%] Built target colt_tests
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 5: routing
1/2 Test #5: routing ..........................   Passed    0.01 sec
    Start 6: middleware
2/2 Test #6: middleware .......................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 2
Total Test time (real) =   0.02 sec
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 1: colt_tests
1/6 Test #1: colt_tests .......................   Passed    0.01 sec
    Start 2: http_parser
2/6 Test #2: http_parser ......................   Passed    0.01 sec
    Start 3: http_serializer
3/6 Test #3: http_serializer ..................   Passed    0.01 sec
    Start 4: context
4/6 Test #4: context ..........................   Passed    0.01 sec
    Start 5: routing
5/6 Test #5: routing ..........................   Passed    0.01 sec
    Start 6: middleware
6/6 Test #6: middleware .......................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 6
Total Test time (real) =   0.06 sec
ff2e9c1
```

`git diff --check` emitted no output and exited 0. Route/middleware ordering, short-circuit, 404, malformed escape, and
repeated-`next()` tests remain covered and pass.

## Matched Handler Lifetime Review Follow-Up

Re-review found that dispatch held a pointer to a `Handler` stored in `routes_` while middleware ran. A middleware
capturing the application could register routes and reallocate that vector before `next()` invoked the pointer. Added a
regression that registers 1,024 routes from middleware, then calls `next()` and asserts the originally matched
parameterized route still handles the request. The test failed before the fix. Dispatch now copies the selected
`Handler` into a local `std::function` before middleware; route registration remains available during middleware, and
the earlier path-parameter visibility behavior is preserved.

Implementation commit: `d273d47` (`fix: preserve matched handler during middleware`).

Post-commit verification command:

`cmake --build build && ctest --test-dir build -R 'routing|middleware' --output-on-failure && ctest --test-dir build --output-on-failure && git diff --check && git rev-parse --short HEAD`

Exact output:

```text
[ 20%] Built target colt
[100%] Built target colt_tests
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 5: routing
1/2 Test #5: routing ..........................   Passed    0.01 sec
    Start 6: middleware
2/2 Test #6: middleware .......................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 2
Total Test time (real) =   0.02 sec
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 1: colt_tests
1/6 Test #1: colt_tests .......................   Passed    0.01 sec
    Start 2: http_parser
2/6 Test #2: http_parser ......................   Passed    0.01 sec
    Start 3: http_serializer
3/6 Test #3: http_serializer ..................   Passed    0.01 sec
    Start 4: context
4/6 Test #4: context ..........................   Passed    0.01 sec
    Start 5: routing
5/6 Test #5: routing ..........................   Passed    0.01 sec
    Start 6: middleware
6/6 Test #6: middleware .......................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 6
Total Test time (real) =   0.07 sec
d273d47
```

`git diff --check` emitted no output and exited 0. The path-parameter middleware regression and existing middleware
order, short-circuit, exception, 404, malformed-escape, and repeated-`next()` coverage all passed.

## Stateful Handler Lifetime Review Follow-Up

Re-review found that copying the matched `std::function` per dispatch resets mutable by-value handler state on each
request. Added a regression test with a mutable handler that returns increasing values across two dispatches; before the
fix, the second result was not `2`. Route entries are now individually heap-owned by `std::unique_ptr<Route>` in the
route vector. Dispatch keeps a pointer to the selected handler inside its stable route entry, so vector reallocation
caused by middleware route registration does not invalidate it, while the original stateful callable remains shared
across dispatches. The middleware route-registration growth regression remains in place and passes.

Implementation commit: `4c5b035` (`fix: preserve stateful route handlers`).

Post-commit verification command:

`cmake --build build && ctest --test-dir build -R 'routing|middleware' --output-on-failure && ctest --test-dir build --output-on-failure && git diff --check && git rev-parse --short HEAD`

Exact output:

```text
[ 20%] Built target colt
[100%] Built target colt_tests
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 5: routing
1/2 Test #5: routing ..........................   Passed    0.01 sec
    Start 6: middleware
2/2 Test #6: middleware .......................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 2
Total Test time (real) =   0.02 sec
Test project /Users/alfiezhang/Works/winkingzhang/colt/.worktrees/colt-http-framework/build
    Start 1: colt_tests
1/6 Test #1: colt_tests .......................   Passed    0.01 sec
    Start 2: http_parser
2/6 Test #2: http_parser ......................   Passed    0.01 sec
    Start 3: http_serializer
3/6 Test #3: http_serializer ..................   Passed    0.01 sec
    Start 4: context
4/6 Test #4: context ..........................   Passed    0.01 sec
    Start 5: routing
5/6 Test #5: routing ..........................   Passed    0.01 sec
    Start 6: middleware
6/6 Test #6: middleware .......................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 6
Total Test time (real) =   0.06 sec
4c5b035
```

`git diff --check` emitted no output and exited 0. The stateful-handler regression, middleware
route-registration/reallocation regression, and prior middleware path-parameter visibility regression all passed.
