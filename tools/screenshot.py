#!/usr/bin/env python3
"""Save the remote's screen as a PNG.

    python3 tools/screenshot.py /dev/cu.usbmodem143201 screen.png [tab]

tab, if given, opens that tab first: 1 Visualizer, 2 Locator.

Needs pyserial. The firmware answers an "S" on the serial port with
"SCREENSHOT <w> <h>", the pixels as little-endian RGB565, then "END".
"""

import struct
import sys
import time
import zlib

import serial


def read_screen(port, tab=None):
    # Leave DTR and RTS alone: dropping both resets the ESP32-S3.
    s = serial.Serial(port, 115200, timeout=5)
    s.reset_input_buffer()
    if tab:
        s.write(tab.encode())  # "1" Visualizer, "2" Locator
        time.sleep(0.5)
    s.write(b"S")
    deadline = time.time() + 10
    while True:
        line = s.readline()
        if line.startswith(b"SCREENSHOT "):
            break
        if time.time() > deadline:
            raise SystemExit("no answer from the remote")
    w, h = (int(v) for v in line.split()[1:3])
    data = s.read(w * h * 2)
    s.close()
    if len(data) != w * h * 2:
        raise SystemExit(f"short read: {len(data)} of {w * h * 2} bytes")
    return w, h, data


def to_png(w, h, data):
    rows = bytearray()
    for y in range(h):
        rows.append(0)  # no filter
        for (v,) in struct.iter_unpack("<H", data[y * w * 2 : (y + 1) * w * 2]):
            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
            rows += bytes(((r * 255 + 15) // 31, (g * 255 + 31) // 63, (b * 255 + 15) // 31))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    header = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows))) + chunk(b"IEND", b"")
    )


def main():
    if len(sys.argv) not in (3, 4):
        raise SystemExit(__doc__)
    w, h, data = read_screen(sys.argv[1], sys.argv[3] if len(sys.argv) == 4 else None)
    with open(sys.argv[2], "wb") as f:
        f.write(to_png(w, h, data))
    print(f"{sys.argv[2]}: {w}x{h}")


if __name__ == "__main__":
    main()
