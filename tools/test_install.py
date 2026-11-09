"""Tests for install.py (the installer): python3 -m unittest discover -s tools -p 'test_*.py'"""

import contextlib
import hashlib
import io
import json
import os
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

import install  # noqa: E402


def release(tag, created, prerelease=False, assets=(install.ASSET,), digest=None):
    return {
        "tag_name": tag,
        "name": tag,
        "created_at": created,
        "prerelease": prerelease,
        "draft": False,
        "assets": [{"name": n, "browser_download_url": f"https://example/{tag}/{n}", "digest": digest} for n in assets],
    }


RELEASES = [
    release("build-main", "2026-11-09T03:00:00Z", prerelease=True),
    release("v26.11.08", "2026-11-08T12:00:00Z"),
    release("build-wifi", "2026-11-08T10:00:00Z", prerelease=True),
    release("v26.11.07", "2026-11-07T06:00:00Z", assets=("metarmap_remote-es3c28p.bin",)),  # before factory images
    release("v26.11.06", "2026-11-06T06:00:00Z"),
]


class PickRelease(unittest.TestCase):
    def test_the_newest_release_with_a_factory_image(self):
        chosen, asset = install.pick_release(RELEASES)
        self.assertEqual(chosen["tag_name"], "v26.11.08")
        self.assertEqual(asset["name"], install.ASSET)

    def test_skips_prereleases_and_releases_without_the_image(self):
        chosen, _ = install.pick_release(RELEASES[2:])
        self.assertEqual(chosen["tag_name"], "v26.11.06")  # not the build- one, not v26.11.07 without the image

    def test_the_order_of_the_list_does_not_matter(self):
        chosen, _ = install.pick_release(list(reversed(RELEASES)))
        self.assertEqual(chosen["tag_name"], "v26.11.08")

    def test_latest_is_the_main_build(self):
        chosen, _ = install.pick_release(RELEASES, latest=True)
        self.assertEqual(chosen["tag_name"], "build-main")

    def test_no_release_has_one_yet(self):
        old = [release("v26.11.07", "2026-11-07T06:00:00Z", assets=("metarmap_remote-es3c28p.bin",))]
        with self.assertRaises(install.Problem) as why:
            install.pick_release(old)
        self.assertIn("--latest", str(why.exception))

    def test_no_main_build(self):
        with self.assertRaises(install.Problem):
            install.pick_release(RELEASES[1:], latest=True)


class Digest(unittest.TestCase):
    def test_sha256_from_github(self):
        self.assertEqual(install.asset_digest({"digest": "sha256:ABCDEF"}), "abcdef")

    def test_missing_or_another_kind(self):
        self.assertEqual(install.asset_digest({}), "")
        self.assertEqual(install.asset_digest({"digest": None}), "")
        self.assertEqual(install.asset_digest({"digest": "md5:abc"}), "")


class Download(unittest.TestCase):
    def test_saves_the_file_and_returns_its_hash(self):
        with tempfile.TemporaryDirectory() as d:
            src, dst = os.path.join(d, "src.bin"), os.path.join(d, "dst.bin")
            data = os.urandom(300_000)
            pathlib.Path(src).write_bytes(data)
            seen = []
            got = install.download(pathlib.Path(src).as_uri(), dst, lambda done, total: seen.append((done, total)))
            self.assertEqual(got, hashlib.sha256(data).hexdigest())
            self.assertEqual(pathlib.Path(dst).read_bytes(), data)
            self.assertEqual(seen[-1], (len(data), len(data)))  # progress reaches the end

    def test_a_missing_file_is_a_problem_not_a_traceback(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(install.Problem):
                install.download(pathlib.Path(d, "nope.bin").as_uri(), os.path.join(d, "x.bin"))

    def test_a_wrong_checksum_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "src.bin")
            pathlib.Path(src).write_bytes(b"firmware")
            asset = {
                "name": install.ASSET,
                "browser_download_url": pathlib.Path(src).as_uri(),
                "digest": "sha256:" + "0" * 64,
            }
            original = install.fetch_json
            install.fetch_json = lambda url: [{**release("v1", "2026"), "assets": [asset]}]
            try:
                args = install.parse_args([])
                with self.assertRaises(install.Problem) as why:
                    install.get_image(args, d)
                self.assertIn("checksum", str(why.exception))
            finally:
                install.fetch_json = original


class Esptool(unittest.TestCase):
    def test_finds_an_installed_one(self):
        self.assertEqual(
            install.find_esptool(lambda n: "/usr/bin/esptool" if n == "esptool" else None), ["/usr/bin/esptool"]
        )
        self.assertEqual(
            install.find_esptool(lambda n: "/x/esptool.py" if n == "esptool.py" else None), ["/x/esptool.py"]
        )
        self.assertIsNone(install.find_esptool(lambda n: None))

    def test_the_command_is_spelled_as_the_version_wants(self):
        self.assertEqual(install.flash_verb("commands: write-flash  read-flash  erase-flash"), "write-flash")
        self.assertEqual(install.flash_verb("positional arguments: write_flash read_flash erase_flash"), "write_flash")


class Failures(unittest.TestCase):
    def test_boot_mode(self):
        for text in (
            "A fatal error occurred: Failed to connect to ESP32-S3: No serial data received.",
            "Wrong boot mode detected (0x2b)! The chip needs to be in download mode.",
            "A fatal error occurred: No serial ports found.",
        ):
            self.assertEqual(install.classify_failure(text), "boot", text)

    def test_port_in_use(self):
        for text in (
            "Could not open /dev/cu.usbmodem14301, the port is busy or doesn't exist.",
            "serial.serialutil.SerialException: [Errno 16] Resource busy",
            "PermissionError(13, 'Access is denied.')",
        ):
            self.assertEqual(install.classify_failure(text), "busy", text)

    def test_anything_else(self):
        self.assertEqual(install.classify_failure("A fatal error occurred: Invalid head of packet"), "other")


FAKE_ESPTOOL = """
import json, os, sys
here = os.path.dirname(os.path.abspath(__file__))
if "--help" in sys.argv:
    print("commands: write-flash read-flash")
    sys.exit(0)
log = os.path.join(here, "calls.json")
calls = json.load(open(log)) if os.path.exists(log) else []
calls.append(sys.argv[1:])
json.dump(calls, open(log, "w"))
script = json.load(open(os.path.join(here, "script.json")))
code, text = script[min(len(calls), len(script)) - 1]
print(text)
sys.exit(code)
"""


class Flash(unittest.TestCase):
    """install.flash against a stand-in esptool that exits as its script says."""

    def run_flash(self, script, port=None, interactive=False, answers=()):
        with tempfile.TemporaryDirectory() as d:
            pathlib.Path(d, "fake.py").write_text(FAKE_ESPTOOL)
            pathlib.Path(d, "script.json").write_text(json.dumps(script))
            out, error = io.StringIO(), None
            with contextlib.redirect_stdout(out), mock.patch("builtins.input", side_effect=list(answers)):
                try:
                    install.flash([sys.executable, os.path.join(d, "fake.py")], "factory.bin", port, interactive)
                except install.Problem as problem:
                    error = str(problem)
            log = pathlib.Path(d, "calls.json")
            return json.loads(log.read_text()) if log.exists() else [], out.getvalue(), error

    def test_writes_the_image_at_zero(self):
        calls, _, error = self.run_flash([[0, "ok"]])
        self.assertIsNone(error)
        self.assertEqual(calls, [["--chip", "esp32s3", "write-flash", "0x0", "factory.bin"]])

    def test_a_given_port_is_passed(self):
        calls, _, _ = self.run_flash([[0, "ok"]], port="/dev/cu.usbmodem1")
        self.assertEqual(calls[0][:4], ["--chip", "esp32s3", "--port", "/dev/cu.usbmodem1"])

    def test_not_in_boot_mode_shows_the_steps_and_tries_again(self):
        script = [[2, "A fatal error occurred: Failed to connect to ESP32-S3: No serial data received."], [0, "ok"]]
        calls, out, error = self.run_flash(script, interactive=True, answers=[""])
        self.assertIsNone(error)
        self.assertEqual(len(calls), 2)
        self.assertIn("Hold the BOOT button", out)

    def test_gives_up_after_three_tries(self):
        script = [[2, "Failed to connect"]]
        calls, _, error = self.run_flash(script, interactive=True, answers=["", ""])
        self.assertEqual(len(calls), install.TRIES)
        self.assertIn("Couldn't connect", error)

    def test_without_a_keyboard_it_shows_the_steps_once(self):
        calls, out, error = self.run_flash([[2, "Failed to connect"]], interactive=False)
        self.assertEqual(len(calls), 1)
        self.assertIn("Hold the BOOT button", out)
        self.assertIn("Run this again", error)

    def test_a_busy_port_says_to_close_the_monitor(self):
        calls, _, error = self.run_flash([[2, "Could not open /dev/cu.usbmodem1, the port is busy"]])
        self.assertEqual(len(calls), 1)
        self.assertIn("serial monitor", error)

    def test_another_failure_is_reported_without_retrying(self):
        calls, _, error = self.run_flash([[2, "A fatal error occurred: Invalid head of packet"]], interactive=True)
        self.assertEqual(len(calls), 1)
        self.assertIn("failed", error)


if __name__ == "__main__":
    unittest.main()
