# Final Review Fix Report

## Changes

- `Application::dispatch()` replaces every informational handler or middleware response (100-199, including unsupported
  101 upgrades) with a deterministic 500. The server then closes because it treats 5xx responses as non-persistent.
- Server keep-alive policy now scans all response `Connection` fields and comma-separated tokens case-insensitively. Any
  `close` token closes the connection, even when it appears after a duplicate field or another token.
- README now describes raw request-target validation accurately: disallowed URI ASCII characters such as quotes and
  backslashes, spaces, non-ASCII bytes, and malformed percent escapes are rejected.
- Multi-candidate socket fallback remains a documented test gap. A deterministic test would need resolver injection or
  another seam into the private socket implementation; resolver-order/IPv6-dependent integration coverage was previously
  removed for portability. No production API was broadened for this test.

## Regression And Verification

The new server tests were run before implementation with
`cmake --build build && ctest --test-dir build -R server --output-on-failure`. Both failed as expected: informational
responses were not 500, and a duplicate `Connection` field containing `CLOSE` did not close the socket.

After implementation:

- `cmake --build build` passed.
- `ctest --test-dir build -R server --output-on-failure` passed.
- `ctest --test-dir build -R 'http_parser|http_serializer' --output-on-failure` passed (2/2).
- `ctest --test-dir build --output-on-failure` passed (9/9).
- `git diff --check` passed.

Verification was performed on the available Clang/macOS environment. Cross-platform GCC/Linux and MSVC/Windows CI was
not executed in this environment.
