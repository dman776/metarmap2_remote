#!/usr/bin/env python3
"""Flash the MetarMap2 Remote onto a new ESP32-S3 board (the LCDwiki ES3C28P), with nothing but Python.

    python3 install.py                 the newest release
    python3 install.py --latest        the newest build of the main branch (may be unfinished)
    python3 install.py --file F.bin    a factory.bin you built (it has your include/secrets.h inside)
    python3 install.py --port PORT     the board's serial port, if it isn't found by itself

It downloads the release's factory image (metarmap_remote-es3c28p-factory.bin: the whole flash in one file),
checks it against the SHA-256 GitHub gives for it, and writes it with esptool: yours if it's installed, else one
it installs for itself in a temporary Python environment and removes afterwards.

A flashed board has no Wi-Fi settings: it starts in Wi-Fi setup (its own network, MetarMap2-Remote-XXXX, and a
QR code on its screen) and you set it up from your phone. It also replaces what the board had, saved settings
included. To update a board that's already set up, use its own update screen.

Only Python 3.8 or newer is needed; the script has no other requirements.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request

REPO = "dman776/metarmap2_remote"
RELEASES_URL = f"https://api.github.com/repos/{REPO}/releases?per_page=30"
ASSET = "metarmap_remote-es3c28p-factory.bin"
LATEST_TAG = "build-main"  # CI's pre-release of the main branch (the remote's Latest commit channel)
CHIP = "esp32s3"
TRIES = 3  # connecting to a board that's not in boot mode, with the BOOT + RESET steps between

BOOT_STEPS = """\
The board didn't answer. Put it in boot mode:
  1. Hold the BOOT button.
  2. Tap RESET.
  3. Release BOOT.
(The USB-C cable must carry data, not only power.)"""


class Problem(Exception):
    """Something to tell the user, without a traceback."""


# ---- which release ---------------------------------------------------------------------------------------------


def pick_release(releases, latest=False):
    """(release, asset) to flash, from GitHub's list of releases.

    The newest published release with a factory image, or with `latest` the main branch's build.
    """
    if latest:
        for release in releases:
            if release.get("tag_name") == LATEST_TAG:
                asset = find_asset(release)
                if asset:
                    return release, asset
        raise Problem(f"There's no {LATEST_TAG} build with a factory image yet.")
    candidates = [r for r in releases if not r.get("prerelease") and not r.get("draft") and find_asset(r)]
    if not candidates:
        raise Problem(
            "No release has a factory image yet (they began after v26.11.07).\n"
            "Use --latest for the newest build of the main branch, or --file for one you built."
        )
    newest = max(candidates, key=lambda r: r.get("created_at", ""))
    return newest, find_asset(newest)


def find_asset(release):
    for asset in release.get("assets", []):
        if asset.get("name") == ASSET:
            return asset
    return None


def asset_digest(asset):
    """The SHA-256 GitHub gives for an asset ("sha256:<hex>"), or "" if it doesn't."""
    digest = asset.get("digest") or ""
    return digest[len("sha256:") :].lower() if digest.startswith("sha256:") else ""


# ---- downloading -----------------------------------------------------------------------------------------------


def request(url):
    return urllib.request.Request(url, headers={"User-Agent": "metarmap-remote-installer", "Accept": "*/*"})


def fetch_json(url):
    try:
        with urllib.request.urlopen(request(url), timeout=30) as response:
            return json.load(response)
    except urllib.error.URLError as err:
        raise Problem(f"Can't reach GitHub ({err.reason if hasattr(err, 'reason') else err}). Is this computer online?")


def download(url, path, show=None):
    """Saves `url` to `path`; returns the file's SHA-256 (hex)."""
    digest = hashlib.sha256()
    try:
        with urllib.request.urlopen(request(url), timeout=60) as response, open(path, "wb") as out:
            total = int(response.headers.get("Content-Length") or 0)
            done = 0
            while True:
                block = response.read(64 * 1024)
                if not block:
                    break
                out.write(block)
                digest.update(block)
                done += len(block)
                if show and total:
                    show(done, total)
    except urllib.error.URLError as err:
        raise Problem(f"The download failed ({err.reason if hasattr(err, 'reason') else err}).")
    return digest.hexdigest()


def progress(done, total):
    sys.stdout.write(f"\r  {done * 100 // total:3d}%  {done // 1024} of {total // 1024} KB")
    sys.stdout.flush()
    if done >= total:
        sys.stdout.write("\n")


def get_image(args, workdir):
    """The factory image's path, downloaded and checked, or the one asked for."""
    if args.file:
        if not os.path.isfile(args.file):
            raise Problem(f"{args.file} isn't a file.")
        print(f"Using {args.file}.")
        print("  (A factory.bin you built has your include/secrets.h inside: don't share it.)")
        return args.file
    print("Looking for the firmware on GitHub...")
    release, asset = pick_release(fetch_json(RELEASES_URL), args.latest)
    title = release.get("name") or release["tag_name"]
    print(f"Downloading {title} ({asset['name']}):")
    path = os.path.join(workdir, ASSET)
    got = download(asset["browser_download_url"], path, progress)
    expected = asset_digest(asset)
    if expected and got != expected:
        raise Problem("The download doesn't match the checksum GitHub gives for it. Try again.")
    print("  checksum OK" if expected else "  (GitHub gave no checksum for it)")
    return path


# ---- esptool ---------------------------------------------------------------------------------------------------


def find_esptool(which=shutil.which):
    """The command of an esptool that's installed, or None."""
    for name in ("esptool", "esptool.py"):
        found = which(name)
        if found:
            return [found]
    return None


def flash_verb(help_text):
    """esptool 5 spells it write-flash and deprecates write_flash; older ones only know write_flash."""
    return "write-flash" if "write-flash" in help_text else "write_flash"


def classify_failure(output):
    """Why an esptool run failed: "boot" (not in boot mode / not found), "busy" (the port is taken), or "other"."""
    text = output.lower()
    if re.search(r"could not open|resource busy|permission denied|access is denied|in use by another", text):
        return "busy"
    if re.search(r"failed to connect|no serial data received|wrong boot mode|timed out waiting|no serial ports", text):
        return "boot"
    return "other"


def run_streaming(cmd):
    """Runs `cmd`, printing its output as it comes; returns (exit code, all of the output)."""
    lines = []
    with subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1) as process:
        for line in process.stdout:
            sys.stdout.write(line)
            lines.append(line)
    return process.returncode, "".join(lines)


def make_esptool(workdir):
    """An esptool command: the installed one, else one installed into a temporary environment."""
    found = find_esptool()
    if found:
        return found
    print("esptool isn't installed: setting one up for this run (it's removed afterwards)...")
    venv = os.path.join(workdir, "venv")
    subprocess.run([sys.executable, "-m", "venv", venv], check=True)
    python = os.path.join(venv, "Scripts" if os.name == "nt" else "bin", "python")
    result = subprocess.run([python, "-m", "pip", "install", "--quiet", "esptool"], capture_output=True, text=True)
    if result.returncode != 0:
        raise Problem("Couldn't install esptool:\n" + result.stdout + result.stderr + "\nTry: pip install esptool")
    return [python, "-m", "esptool"]


def flash(esptool, image, port, interactive):
    help_text = subprocess.run(esptool + ["--help"], capture_output=True, text=True).stdout
    cmd = esptool + ["--chip", CHIP] + (["--port", port] if port else []) + [flash_verb(help_text), "0x0", image]
    for attempt in range(1, TRIES + 1):
        print("Writing the firmware" + (f" (try {attempt} of {TRIES})" if attempt > 1 else "") + "...")
        code, output = run_streaming(cmd)
        if code == 0:
            return
        why = classify_failure(output)
        if why == "busy":
            raise Problem(
                "The board's serial port is in use or can't be opened. Close any serial monitor "
                "(PlatformIO, Arduino, screen...) and run this again."
            )
        if why != "boot":
            raise Problem("esptool failed (above).")
        print("\n" + BOOT_STEPS)
        if attempt == TRIES or not interactive:
            raise Problem(
                "Couldn't connect to the board." + ("" if interactive else " Run this again after the steps.")
            )
        input("\nPress Enter to try again... ")
    raise Problem("Couldn't connect to the board.")


# ---- main ------------------------------------------------------------------------------------------------------


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Flash the MetarMap2 Remote onto a new ESP32-S3 board.",
        epilog="The board needs a USB-C data cable. If it isn't found, hold BOOT, tap RESET, release BOOT.",
    )
    parser.add_argument("--latest", action="store_true", help="the newest main-branch build, not the newest release")
    parser.add_argument("--file", metavar="FILE", help="flash this factory.bin instead of downloading one")
    parser.add_argument("--port", metavar="PORT", help="the board's serial port (found by itself if not given)")
    parser.add_argument("-y", "--yes", action="store_true", help="don't ask before flashing")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    interactive = sys.stdin.isatty() and not args.yes
    try:
        with tempfile.TemporaryDirectory(prefix="metarmap-remote-") as workdir:
            image = get_image(args, workdir)
            esptool = make_esptool(workdir)
            if interactive:
                print(
                    "\nThis writes the whole flash of the board plugged in, replacing what's on it "
                    "(its saved Wi-Fi settings too)."
                )
                if input("Go ahead? [Y/n] ").strip().lower() not in ("", "y", "yes"):
                    print("Cancelled.")
                    return 1
            flash(esptool, image, args.port, interactive)
    except Problem as problem:
        print(f"\n{problem}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nCancelled.", file=sys.stderr)
        return 1
    print(
        "\nDone. The remote restarts and shows Wi-Fi setup:\n"
        "  1. On your phone, join the Wi-Fi network it names (MetarMap2-Remote-XXXX), or scan its QR code.\n"
        "  2. The setup page opens by itself (or go to http://192.168.4.1/):\n"
        "     pick your Wi-Fi and enter the map's address."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
