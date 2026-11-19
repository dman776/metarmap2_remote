// Glue between LVGL and the LovyanGFX display/touch driver.
#pragma once

#include <lvgl.h>

#include <vector>

#include "display.h"

void lvgl_port_init(LGFX& lcd);

// The Chart's quiet hours (/api/live's `quiet`): the backlight dims to about 10%, a touch wakes it for 30 s (that touch
// only wakes, it presses nothing), and it's full again at once when they end (quiet_dim.h). Call from the loop task.
void lvgl_port_set_quiet(bool quiet);

// Writes the screen to Serial as "SCREENSHOT <w> <h>\n", w*h little-endian
// RGB565 pixels, then "\nEND\n". tools/screenshot.py turns it into a PNG.
void lvgl_port_screenshot();

// The screen as it is now: w*h RGB565 pixels, row by row (the web page's screenshot). Call from the loop task, as
// LVGL runs there: it reads the display back.
void lvgl_port_capture(std::vector<uint16_t>& pixels, int& w, int& h);
