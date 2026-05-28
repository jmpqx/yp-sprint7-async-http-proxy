#!/usr/bin/env python3

import os
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

WORKSPACE = Path(__file__).resolve().parent.parent
PROXY_BIN = WORKSPACE / "build" / "AsyncHttpProxy"
PROXY_PORT = 6767
SERVER_PORT = 8000


def make_response(body: bytes, content_type: str = "text/html") -> bytes:
    return (
        f"HTTP/1.1 200 OK\r\n"
        f"Content-Type: {content_type}\r\n"
        f"Content-Length: {len(body)}\r\n"
        f"\r\n"
    ).encode() + body


def serve_once(port: int, response: bytes) -> threading.Thread:
    ready = threading.Event()

    def _serve() -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind(("127.0.0.1", port))
            s.listen(1)
            ready.set()
            conn, _ = s.accept()
            with conn:
                buf = b""
                while b"\r\n\r\n" not in buf:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    buf += chunk
                conn.sendall(response)

    t = threading.Thread(target=_serve, daemon=True)
    t.start()
    ready.wait(timeout=2.0)
    return t


def run_test(name: str, body: bytes) -> bool:
    print(f"  {name} ... ", end="", flush=True)

    serve_thread = serve_once(SERVER_PORT, make_response(body))

    with tempfile.NamedTemporaryFile(delete=False, suffix=".out") as f:
        out_path = f.name

    try:
        subprocess.run(
            [
                "wget", "-q",
                "-e", "use_proxy=yes",
                "-e", f"http_proxy=127.0.0.1:{PROXY_PORT}",
                f"127.0.0.1:{SERVER_PORT}",
                "-O", out_path,
            ],
            timeout=10,
            check=False,
        )

        received = Path(out_path).read_bytes()

        if received == body:
            print("PASS")
            return True
        else:
            print(f"FAIL — expected {len(body)} bytes, got {len(received)} bytes")
            return False

    except subprocess.TimeoutExpired:
        print("FAIL — timeout")
        return False
    finally:
        os.unlink(out_path)
        serve_thread.join(timeout=2.0)


def main() -> int:
    if not PROXY_BIN.exists():
        print(f"Error: proxy binary not found at {PROXY_BIN}")
        print("Build first:  cd build && cmake --build .")
        return 2

    proxy = subprocess.Popen(
        [str(PROXY_BIN), str(PROXY_PORT)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.3)

    tests = [
        ("4096 bytes of 'A'  (Content-Length)", b"A" * 4096),
        ("small body (10 bytes)",               b"0123456789"),
        ("1 MB body",                           b"B" * (1024 * 1024)),
    ]

    passed = 0
    failed = 0

    print("Running integration tests...")
    try:
        for name, body in tests:
            if run_test(name, body):
                passed += 1
            else:
                failed += 1
    finally:
        proxy.terminate()
        proxy.wait()

    total = passed + failed
    print(f"\nResults: {passed}/{total} passed")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
