// PNG files from the screen's pixels, for the remote's web page (a screenshot to download, the live view). Plain
// C++, unit tested on the host (test/test_png).
#pragma once

#include <cstdint>
#include <vector>

// Compresses PNG image data to a zlib stream (header, deflate data, Adler-32); empty if it can't.
using Deflate = std::vector<uint8_t> (*)(const std::vector<uint8_t>& raw);

// An RGB PNG of w x h RGB565 pixels (row by row, as the display reads them back). With `deflate` (the ESP32's ROM
// compressor) the data is compressed, about 15-30 KB for the screen; without it, or if it fails, it's stored as it
// is, about 230 KB.
std::vector<uint8_t> png_from_rgb565(int w, int h, const uint16_t* pixels, Deflate deflate = nullptr);
