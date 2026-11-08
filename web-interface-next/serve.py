"""Local server for the UI redesign: the offline mock or the live pages.

Mock:  python web-interface-next/serve.py --mock
       Fake data and the Demo states panel; no device needed.
Live:  python web-interface-next/serve.py [--device config.local]
       The same pages with the scripts in live/, talking to a real device.

Both open on http://localhost:8766 (--port to change it). Pages are served
without ".html" (/home, /scan, /settings), as the device serves them.

Live mode builds each page from the mock's HTML, swapping mock.js for the
live/ scripts, so there is one copy of the markup. API calls are forwarded to
the device: it only accepts POSTs whose Host and Origin are its own name and
sends no CORS headers, so a page on localhost could not call it directly.
"""

import argparse
import http.client
import json
import mimetypes
import re
import socket
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
LIVE = HERE / "live"
API_TIMEOUT_S = 15
UPLOAD_TIMEOUT_S = 120

MOCK_SCRIPT = '<script src="mock.js" defer></script>'
# Scripts each live page loads instead of mock.js (app.js is already in every page)
LIVE_SCRIPTS = {
    "index": ["api.js"],
    "home": ["api.js", "home.js"],
    "scan": ["api.js", "scan.js"],
    "settings": ["api.js", "settings.js", "firmware.js"],
}


def live_page(name, html):
    """Turn a mock page into its live version."""
    if MOCK_SCRIPT not in html:
        raise ValueError(f"{name}.html: expected {MOCK_SCRIPT}")
    scripts = "\n    ".join(f'<script src="{s}" defer></script>' for s in LIVE_SCRIPTS[name])
    html = html.replace(MOCK_SCRIPT, scripts)
    # Links without ".html", as on the device; the version comes from /api/health
    html = re.sub(r'href="([a-z]+)\.html', r'href="\1', html)
    return re.sub(r'data-version="[^"]*"', 'data-version=""', html)


class Device:
    """The device's name and its resolved address (mDNS lookups can be slow on Windows)."""

    def __init__(self, host):
        self.host = host
        self.ip = None

    def address(self, refresh=False):
        if refresh or not self.ip:
            self.ip = socket.gethostbyname(self.host)
        return self.ip


class Handler(BaseHTTPRequestHandler):
    device = None  # None in mock mode

    def do_GET(self):
        if self.path.startswith("/api/"):
            self.api()
        else:
            self.serve_file()

    def do_POST(self):
        if self.path.startswith("/api/"):
            self.api()
        else:
            self.send_error(405)

    def find(self, url_path):
        """Return (path, bytes) for a URL: "/" is index.html and "/home" is home.html."""
        rel = url_path.split("?", 1)[0].lstrip("/") or "index.html"
        if "." not in Path(rel).name:
            rel += ".html"
        roots = [LIVE, HERE] if self.device else [HERE]
        for root in roots:
            path = (root / rel).resolve()
            if root not in path.parents or not path.is_file():
                continue
            # The fake API must never load next to the real one
            if self.device and path.name == "mock.js":
                continue
            data = path.read_bytes()
            name = path.stem
            if self.device and path.suffix == ".html" and name in LIVE_SCRIPTS:
                data = live_page(name, data.decode("utf-8")).encode("utf-8")
            return path, data
        return None, None

    def serve_file(self):
        path, data = self.find(self.path)
        if not path:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", mimetypes.guess_type(path.name)[0] or "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(data)

    def api(self):
        if not self.device:
            self.send_json(404, {"success": False, "message": "Mock mode: there is no device API"})
            return
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else None
        headers = {"Host": self.device.host}
        if self.headers.get("Content-Type"):
            headers["Content-Type"] = self.headers["Content-Type"]
        if self.command == "POST":
            # What a page served by the device itself would send
            headers["Origin"] = "http://" + self.device.host
        upload = self.path.split("?", 1)[0] == "/api/update"
        timeout = UPLOAD_TIMEOUT_S if upload else API_TIMEOUT_S

        error = None
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
    parser.add_argument("--mock", action="store_true", help="serve the offline mock instead of the live pages")
    parser.add_argument("--device", default="config.local", help="device host name or IP (default: config.local)")
    parser.add_argument("--port", type=int, default=8766, help="local port (default: 8766)")
    args = parser.parse_args()

    if args.mock:
        print("Mock mode: fake data, use the Demo states button to switch states")
    else:
        Handler.device = Device(args.device)
        try:
            print(f"Live mode: device {args.device} is at {Handler.device.address()}")
        except OSError as e:
            print(f"Live mode: cannot resolve {args.device} yet ({e}); retrying on each request")
    print(f"Open http://localhost:{args.port}")
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
