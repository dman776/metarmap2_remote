#!/usr/bin/env python3
"""Open the Copilot's screens over HTTP and save a screenshot of each.

    COPILOT_PASSWORD=... python3 tools/shoot.py metarmap-copilot.local splash picker card:KHOU airports:2

Each item opens something, then saves the screen as <item>.png (card:KHOU is card-KHOU.png), or as the file after
an @ (card:KHOU@card.png):

    chart airports splash update picker brightness volume close   a screen (volume: the splash's volume slider)
    airports:N                                                     the Airports panel's page N (1 is the first)
    card:KHOU                                                      an airport's card, by its code
    card:N  card:P:N                                               the Nth airport on the page shown, or on page P
    button:locate  button:awos  button:atis  button:stop           press a button on the open card
    visualizer:wind                                                switch the Chart to a visualizer (id or name)

A card, button:locate and visualizer: change what the Chart shows; button:awos and button:atis play on the
Copilot's speaker. Needs "Remote control (for screenshots)" on in the Copilot's web page's Settings (this doesn't turn
it on), and the web page's login: user metar, the password from COPILOT_PASSWORD or --password. Python 3, nothing to
install.
"""

import argparse
import base64
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

SCREENS = ("chart", "airports", "splash", "update", "picker", "brightness", "volume", "close")
BUTTONS = ("locate", "awos", "atis", "stop")


class Failed(Exception):
    pass


class Copilot:
    def __init__(self, host, password, timeout=15):
        host = host.strip().rstrip("/")
        if not re.match(r"^https?://", host):
            host = "http://" + host
        self.base = host
        token = base64.b64encode(f"metar:{password}".encode()).decode()
        self.headers = {"Authorization": "Basic " + token}
        self.timeout = timeout

    def request(self, path, data=None):
        body = urllib.parse.urlencode(data).encode() if data is not None else None
        req = urllib.request.Request(self.base + path, data=body, headers=self.headers)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            text = e.read().decode(errors="replace").strip()
            if e.code == 401:
                raise Failed("The login didn't work: user metar, and the Copilot's web password.") from None
            raise Failed(f"{e.code}: {text or e.reason}") from None
        except (urllib.error.URLError, OSError) as e:
            raise Failed(f"Can't reach {self.base}: {getattr(e, 'reason', e)}") from None

    def state(self):
        return json.loads(self.request("/api/ui/state"))

    def post(self, path, **data):
        return json.loads(self.request(path, data))

    def screenshot(self):
        return self.request("/screen.png")


def parse_item(item):
    """An item into (the endpoint, its parameters, the PNG's name, a warning or None)."""
    item, _, out = item.partition("@")
    kind, _, rest = item.partition(":")
    kind = kind.lower()
    name = out or re.sub(r"[^A-Za-z0-9_.-]+", "-", item).strip("-") + ".png"
    if kind in SCREENS:
        if not rest:
            return "/api/ui/show", {"screen": kind}, name, None
        if kind == "airports" and rest.isdigit():
            return "/api/ui/show", {"screen": kind, "page": rest}, name, None
    elif kind == "card" and rest:
        parts = rest.split(":")
        if len(parts) == 2 and all(p.isdigit() for p in parts):
            params = {"page": parts[0], "position": parts[1]}
        elif len(parts) == 1 and rest.isdigit():
            params = {"position": rest}
        elif len(parts) == 1:
            params = {"airport": rest}
        else:
            params = None
        if params:
            return "/api/ui/card", params, name, "the Chart pulses the airport's pixel"
    elif kind == "button" and rest.lower() in BUTTONS:
        b = rest.lower()
        warning = {"locate": "the Chart pulses the airport's pixel", "stop": None}.get(b, "the speaker plays")
        return "/api/ui/card", {"button": b}, name, warning
    elif kind == "visualizer" and rest:
        return "/api/ui/visualizer", {"id": rest}, name, "the Chart switches visualizer"
    raise Failed(f"Don't know {item!r}: see python3 tools/shoot.py --help")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("host", help="the Copilot: metarmap-copilot.local, its IP, or http://...")
    p.add_argument("items", nargs="+", help="what to open before each screenshot (see above)")
    p.add_argument("--password", help="the web password (default: $COPILOT_PASSWORD)")
    p.add_argument("--out", default=".", help="the folder to save the PNGs in (default: here)")
    p.add_argument("--delay", type=float, default=0.2, help="seconds to wait before each screenshot (default 0.2)")
    args = p.parse_args(argv)

    password = args.password if args.password is not None else os.environ.get("COPILOT_PASSWORD")
    if not password:
        p.error("the web password: set COPILOT_PASSWORD or give --password")
    try:
        plan = [parse_item(i) for i in args.items]
    except Failed as e:
        p.error(str(e))

    copilot = Copilot(args.host, password)
    try:
        state = copilot.state()
    except Failed as e:
        print(e, file=sys.stderr)
        return 2
    if not state.get("remoteControl"):
        print(
            'Remote control is off on the Copilot. Turn on "Remote control (for screenshots)" in its web page\'s '
            f"Settings ({copilot.base}/), then run this again.",
            file=sys.stderr,
        )
        return 2

    os.makedirs(args.out, exist_ok=True)
    failed = 0
    for item, (path, params, name, warning) in zip(args.items, plan):
        try:
            if warning:
                print(f"{item}: {warning}")
            copilot.post(path, **params)
            time.sleep(args.delay)
            png = copilot.screenshot()
        except Failed as e:
            print(f"{item}: {e}", file=sys.stderr)
            failed += 1
            continue
        out = os.path.join(args.out, name)
        with open(out, "wb") as f:
            f.write(png)
        print(f"{item}: {out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
