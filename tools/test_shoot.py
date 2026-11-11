"""Tests for tools/shoot.py (screenshots over HTTP): python3 -m unittest discover -s tools -p 'test_*.py'"""

import base64
import contextlib
import io
import json
import pathlib
import sys
import tempfile
import threading
import unittest
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import shoot  # noqa: E402

PNG = b"\x89PNG\r\n\x1a\n fake"


class ParseItem(unittest.TestCase):
    def test_screens(self):
        self.assertEqual(shoot.parse_item("picker"), ("/api/ui/show", {"screen": "picker"}, "picker.png", None))
        self.assertEqual(
            shoot.parse_item("airports:2")[:3], ("/api/ui/show", {"screen": "airports", "page": "2"}, "airports-2.png")
        )

    def test_cards(self):
        self.assertEqual(shoot.parse_item("card:KHOU")[:3], ("/api/ui/card", {"airport": "KHOU"}, "card-KHOU.png"))
        self.assertEqual(shoot.parse_item("card:3")[1], {"position": "3"})
        self.assertEqual(shoot.parse_item("card:2:3")[1], {"page": "2", "position": "3"})
        self.assertIsNotNone(shoot.parse_item("card:KHOU")[3])  # the Chart pulses it: said

    def test_buttons_and_visualizer(self):
        self.assertEqual(shoot.parse_item("button:atis")[:2], ("/api/ui/card", {"button": "atis"}))
        self.assertIsNone(shoot.parse_item("button:stop")[3])
        self.assertEqual(shoot.parse_item("visualizer:wind")[:2], ("/api/ui/visualizer", {"id": "wind"}))

    def test_file_name_after_an_at(self):
        self.assertEqual(shoot.parse_item("card:KHOU@card.png")[2], "card.png")
        self.assertEqual(shoot.parse_item("chart@leds.png")[2], "leds.png")

    def test_unknown(self):
        for bad in ("cockpit", "chart:2", "card:", "card:1:2:3", "button:play", "visualizer:"):
            with self.assertRaises(shoot.Failed, msg=bad):
                shoot.parse_item(bad)


class FakeCopilot(BaseHTTPRequestHandler):
    """The Copilot's /api/ui/ endpoints and /screen.png, as remote_web.cpp answers them."""

    password = "secret"
    remote_control = True
    posts = []
    headers_seen = []

    def log_message(self, *args):
        pass

    def send(self, code, body, kind="text/plain"):
        self.send_response(code)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def authed(self):
        want = "Basic " + base64.b64encode(f"metar:{self.password}".encode()).decode()
        if self.headers.get("Authorization") == want:
            return True
        self.send(401, b"")
        return False

    def do_GET(self):
        if not self.authed():
            return
        if self.path == "/api/ui/state":
            self.send(200, json.dumps({"remoteControl": self.remote_control}).encode(), "application/json")
        elif self.path == "/screen.png":
            self.send(200, PNG, "image/png")
        else:
            self.send(404, b"not found")

    def do_POST(self):
        FakeCopilot.headers_seen.append(self.headers.get("X-Copilot"))
        if self.headers.get("X-Copilot") != "1":
            self.send(403, b"This request needs the header X-Copilot: 1.")
            return
        if not self.authed():
            return
        data = dict(urllib.parse.parse_qsl(self.rfile.read(int(self.headers["Content-Length"])).decode()))
        FakeCopilot.posts.append((self.path, data))
        if not self.remote_control:
            self.send(403, b"Remote control is off.")
        elif data.get("airport") == "KXYZ":
            self.send(404, b"KXYZ isn't one of the Chart's airports.")
        else:
            self.send(200, json.dumps({"remoteControl": True}).encode(), "application/json")


class Shoot(unittest.TestCase):
    def setUp(self):
        FakeCopilot.remote_control = True
        FakeCopilot.posts = []
        FakeCopilot.headers_seen = []
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), FakeCopilot)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.host = f"127.0.0.1:{self.server.server_address[1]}"
        self.dir = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.dir.cleanup()

    def run_shoot(self, *args, password="secret"):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = shoot.main([self.host, *args, "--password", password, "--out", self.dir.name, "--delay", "0"])
        return code, out.getvalue(), err.getvalue()

    def test_saves_a_png_for_each(self):
        code, out, _ = self.run_shoot("splash", "card:KHOU@card.png")
        self.assertEqual(code, 0)
        self.assertEqual(
            FakeCopilot.posts, [("/api/ui/show", {"screen": "splash"}), ("/api/ui/card", {"airport": "KHOU"})]
        )
        self.assertEqual((pathlib.Path(self.dir.name) / "splash.png").read_bytes(), PNG)
        self.assertTrue((pathlib.Path(self.dir.name) / "card.png").exists())
        self.assertIn("pulses", out)  # the card's locate, said

    def test_posts_send_the_x_copilot_header(self):
        code, _, _ = self.run_shoot("splash", "picker")
        self.assertEqual(code, 0)
        self.assertEqual(FakeCopilot.headers_seen, ["1", "1"])

    def test_the_header_goes_on_posts_only(self):
        copilot = shoot.Copilot(self.host, "secret")
        sent = []
        real = shoot.urllib.request.urlopen

        def spy(req, timeout):
            sent.append((req.get_method(), req.get_header("X-copilot")))
            return real(req, timeout=timeout)

        shoot.urllib.request.urlopen = spy
        try:
            copilot.state()
            copilot.post("/api/ui/show", screen="chart")
        finally:
            shoot.urllib.request.urlopen = real
        self.assertEqual(sent, [("GET", None), ("POST", "1")])

    def test_says_remote_control_is_off_and_sends_nothing(self):
        FakeCopilot.remote_control = False
        code, _, err = self.run_shoot("picker")
        self.assertEqual(code, 2)
        self.assertIn("Remote control (for screenshots)", err)
        self.assertEqual(FakeCopilot.posts, [])

    def test_a_wrong_password(self):
        code, _, err = self.run_shoot("picker", password="nope")
        self.assertEqual(code, 2)
        self.assertIn("login", err)

    def test_goes_on_after_one_that_fails(self):
        code, _, err = self.run_shoot("card:KXYZ", "picker")
        self.assertEqual(code, 1)
        self.assertIn("404: KXYZ isn't one of the Chart's airports.", err)
        self.assertTrue((pathlib.Path(self.dir.name) / "picker.png").exists())
        self.assertFalse((pathlib.Path(self.dir.name) / "card-KXYZ.png").exists())


if __name__ == "__main__":
    unittest.main()
