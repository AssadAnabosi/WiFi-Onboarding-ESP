"""Preview the web interface on your PC: against a fake device or a real one.

Mock:  python utils/ui_preview.py --mock
       Fake data and a Demo states panel to switch the fake device's state;
       no device needed.
Live:  python utils/ui_preview.py [--device config.local]
       The real pages talking to a real device.

Open http://localhost:8766 (--port to change it). Pages come straight from
web-interface/, served without ".html" (/home, /scan, /settings) as on the
device, with {{VERSION}} filled in from VERSION. Edit and reload; there is no
build step.

Mock mode swaps each page's js/api.js and page scripts for utils/ui_mock.js.
Live mode forwards /api/* to the device: it only accepts POSTs whose Host and
Origin are its own name and sends no CORS headers, so a page on localhost
could not call it directly. Uses the standard library only.
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

ROOT = Path(__file__).resolve().parent.parent
WEB_DIR = ROOT / "web-interface"
MOCK_JS = Path(__file__).resolve().parent / "ui_mock.js"
MOCK_URL = "/ui_mock.js"
VERSION_FILE = ROOT / "VERSION"
API_TIMEOUT_S = 15
UPLOAD_TIMEOUT_S = 120

# Scripts that talk to the device; the mock stands in for all of them
DEVICE_SCRIPT = re.compile(r'[ \t]*<script src="js/(?:api|home|scan|settings|firmware)\.js" defer></script>\r?\n?')


def fill_version(html, version):
    return html.replace("{{VERSION}}", version)


def mock_page(html):
    """Swap a page's device scripts for the mock."""
    if not DEVICE_SCRIPT.search(html):
        raise ValueError("page loads none of js/api.js, home.js, scan.js, settings.js, firmware.js")
    first = True

    def swap(match):
        nonlocal first
        if first:
            first = False
            indent = re.match(r"[ \t]*", match.group(0)).group(0)
            return f'{indent}<script src="{MOCK_URL}" defer></script>\n'
        return ""

    return DEVICE_SCRIPT.sub(swap, html)


def find_file(url_path, mock):
    """Map a URL to a file: "/" is index.html and "/home" is home.html, as on the device."""
    rel = url_path.split("?", 1)[0].split("#", 1)[0].lstrip("/")
    if mock and "/" + rel == MOCK_URL:
        return MOCK_JS
    rel = rel or "index.html"
    if "." not in Path(rel).name:
        rel += ".html"
    path = (WEB_DIR / rel).resolve()
    if WEB_DIR.resolve() in path.parents and path.is_file():
        return path
    return None


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
    version = ""

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

    def serve_file(self):
        mock = self.device is None
        path = find_file(self.path, mock)
        if not path:
            self.send_error(404)
            return
        data = path.read_bytes()
        if path.suffix == ".html":
            html = fill_version(data.decode("utf-8"), self.version)
            data = (mock_page(html) if mock else html).encode("utf-8")
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
    parser.add_argument("--mock", action="store_true", help="use a fake device instead of a real one")
    parser.add_argument("--device", default="config.local", help="device host name or IP (default: config.local)")
    parser.add_argument("--port", type=int, default=8766, help="local port (default: 8766)")
    args = parser.parse_args()

    Handler.version = VERSION_FILE.read_text(encoding="utf-8").strip()
    if args.mock:
        print("Mock mode: fake device; the Demo states button switches its state")
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
