import subprocess
import sys
import time
import urllib.error
import urllib.request


def main():
    process = subprocess.Popen([sys.argv[1]], stdin=subprocess.PIPE)
    try:
        deadline = time.monotonic() + 10
        while True:
            if process.poll() is not None:
                raise RuntimeError(f"colt_hello exited with status {process.returncode}")
            try:
                for path, expected_body in (
                    ("/", "Hello from Colt!"),
                    ("/hello/Colt", "Hello, Colt!"),
                ):
                    with urllib.request.urlopen(
                        f"http://127.0.0.1:8080{path}", timeout=1
                    ) as response:
                        if response.status != 200:
                            raise AssertionError(
                                f"unexpected HTTP status for {path}: {response.status}"
                            )
                        body = response.read().decode("utf-8")
                    if body != expected_body:
                        raise AssertionError(
                            f"unexpected response body for {path}: {body!r}"
                        )
                return
            except urllib.error.HTTPError as error:
                raise AssertionError(f"unexpected HTTP status: {error.code}") from error
            except urllib.error.URLError:
                if time.monotonic() >= deadline:
                    raise TimeoutError("colt_hello did not accept a loopback request")
                time.sleep(0.1)
    finally:
        if process.poll() is None:
            process.stdin.write(b"\n")
            process.stdin.flush()
            process.wait(timeout=10)
        process.stdin.close()


if __name__ == "__main__":
    main()
