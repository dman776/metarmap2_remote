// Glue between LVGL and the LovyanGFX display/touch driver.
#pragma once

#include <lvgl.h>

#include "display.h"

void lvgl_port_init(LGFX& lcd);

// Writes the screen to Serial as "SCREENSHOT <w> <h>\n", w*h little-endian
// RGB565 pixels, then "\nEND\n". tools/screenshot.py turns it into a PNG.
void lvgl_port_screenshot();
