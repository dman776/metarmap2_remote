"""PlatformIO post-build script: a factory image, for flashing a blank board in one go.

Writes .pio/build/<env>/factory.bin next to firmware.bin: the bootloader, the partition table, boot_app0 and
the app, merged by esptool at the addresses a USB upload uses (PlatformIO's FLASH_EXTRA_IMAGES and
ESP32_APP_OFFSET), so it can't drift from them. Flash it with:

    esptool.py --chip esp32s3 write_flash 0x0 factory.bin

The gaps between the parts are blank (0xFF), so it also clears the saved settings (Wi-Fi, the map, the web
page's password): a board flashed with it starts in Wi-Fi setup. firmware.bin stays the app alone, for the
remote's own updater.
"""

import os
import subprocess

Import("env")  # noqa: F821 (PlatformIO's SCons)


def merge(source, target, env):
    build = env.subst("$BUILD_DIR")
    out = os.path.join(build, "factory.bin")
    board = env.BoardConfig()
    parts = [(offset, env.subst(path)) for offset, path in env.get("FLASH_EXTRA_IMAGES", [])]
    parts.append((env.subst("$ESP32_APP_OFFSET"), str(target[0])))
    cmd = [
        env.subst("$PYTHONEXE"),
        env.subst("$OBJCOPY"),  # esptool.py, on this platform
        "--chip",
        board.get("build.mcu", "esp32s3"),
        "merge_bin",
        "-o",
        out,
        "--flash_mode",
        env.subst("${__get_board_flash_mode(__env__)}"),
        "--flash_freq",
        env.subst("${__get_board_f_image(__env__)}"),
        "--flash_size",
        board.get("upload.flash_size", "keep"),
    ]
    for offset, path in parts:
        cmd += [offset, path]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    print(f"Factory image: {os.path.relpath(out)} ({', '.join(f'{o} {os.path.basename(p)}' for o, p in parts)})")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)  # noqa: F821
