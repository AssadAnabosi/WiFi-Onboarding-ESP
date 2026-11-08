"""Serve the live redesign pages and forward /api/* to a real device.

The device only accepts POSTs whose Host and Origin are its own name and sends
no CORS headers, so a page on localhost cannot call it directly. This server
serves the pages and passes API calls on with Host/Origin set to the device,
so the pages use the same relative /api URLs they would on the device.

Usage: python web-interface-next/serve.py [--device config.local] [--port 8766]
Then open http://localhost:8766
"""

import argparse
import http.client
import json
import mimetypes
import socket
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
# Live pages first, then the shared style.css / app.js next to the mock
ROOTS = [HERE / "live", HERE]
API_TIMEOUT_S = 15
UPLOAD_TIMEOUT_S = 120


class Device:
    """The device's name and its resolved address (mDNS lookups can be slow on Windows)."""

    def __init__(self, host):
        self.host = host
        self.ip = None

    def address(self, refresh=False):
        if refresh or not self.ip:
            self.ip = socket.gethostbyname(self.host)
        return self.ip


def find_file(url_path):
    """Map a URL to a file: "/" is index.html and "/home" is home.html, as on the device."""
    rel = url_path.split("?", 1)[0].lstrip("/") or "index.html"
    if "." not in Path(rel).name:
        rel += ".html"
    for root in ROOTS:
        path = (root / rel).resolve()
        if root in path.parents and path.is_file():
            return path
    return None


class Handler(BaseHTTPRequestHandler):
    device = None

    def do_GET(self):
        if self.path.startswith("/api/"):
            self.proxy()
        else:
            self.serve_file()

    def do_POST(self):
        if self.path.startswith("/api/"):
            self.proxy()
        else:
            self.send_error(405)

    def serve_file(self):
        path = find_file(self.path)
        if not path:
            self.send_error(404)
            return
        data = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", mimetypes.guess_type(path.name)[0] or "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(data)

    def proxy(self):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else None
        headers = {"Host": self.device.host}
        if self.headers.get("Content-Type"):
            headers["Content-Type"] = self.headers["Content-Type"]
        if self.command == "POST":
            # What a page served by the device itself would send
            headers["Origin"] = "http://" + self.device.host
        timeout = UPLOAD_TIMEOUT_S if self.path.startswith("/api/update?") or self.path == "/api/update" else API_TIMEOUT_S

        for attempt in range(2):
            try:
                conn = http.client.HTTPConnection(self.device.address(refresh=attempt > 0), 80, timeout=timeout)
                conn.request(self.command, self.path, body=body, headers=headers)
                res = conn.getresponse()
                data = res.read()
                conn.close()
                break
            except (OSError, http.client.HTTPException) as e:
                error = e
        else:
            self.send_json(502, {"success": False, "message": f"Device not reachable at {self.device.host} ({error})"})
            return

        self.send_response(res.status)
        self.send_header("Content-Type", res.getheader("Content-Type") or "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def send_json(self, code, obj):
        data = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, fmt, *args):
        if self.path.startswith("/api/"):
            sys.stderr.write("%s %s\n" % (self.command, fmt % args))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--device", default="config.local", help="device host name or IP (default: config.local)")
    parser.add_argument("--port", type=int, default=8766, help="local port (default: 8766)")
    args = parser.parse_args()

    Handler.device = Device(args.device)
    try:
        print(f"Device {args.device} is at {Handler.device.address()}")
    except OSError as e:
        print(f"Warning: cannot resolve {args.device} yet ({e}); will retry on each request")
    print(f"Open http://localhost:{args.port}")
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
