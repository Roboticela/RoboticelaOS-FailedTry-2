#!/usr/bin/env python3
"""Host-side echo + HTTP server for Roboticela OS QEMU networking tests."""
import socket
import threading
import sys

ECHO_PORT = 18019
HTTP_PORT = 18080
BODY = b"ok\n"


def handle_echo(c, _a):
    try:
        data = b""
        while True:
            x = c.recv(4096)
            if not x:
                break
            data += x
            if b"\n" in data or len(data) >= 32:
                break
        c.sendall(b"Hello from host\n")
    except Exception:
        pass
    finally:
        try:
            c.close()
        except Exception:
            pass


def handle_http(c, _a):
    try:
        buf = b""
        while b"\r\n\r\n" not in buf and len(buf) < 4096:
            x = c.recv(1024)
            if not x:
                break
            buf += x
        payload = BODY
        hdr = (
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n" % len(payload)
        ).encode("ascii")
        c.sendall(hdr + payload)
    except Exception:
        pass
    finally:
        try:
            c.close()
        except Exception:
            pass


def serve(port, handler):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", port))
    s.listen(16)
    while True:
        c, a = s.accept()
        t = threading.Thread(target=handler, args=(c, a), daemon=True)
        t.start()


def main():
    t = threading.Thread(target=serve, args=(HTTP_PORT, handle_http), daemon=True)
    t.start()
    print("net-echo-server 127.0.0.1:%d echo  :%d http" % (ECHO_PORT, HTTP_PORT), flush=True)
    serve(ECHO_PORT, handle_echo)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
