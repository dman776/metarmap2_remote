#include "png.h"

#include <algorithm>
#include <cstring>

namespace {

uint32_t crcTable[256];

uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc = 0) {
  if (!crcTable[1]) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
      crcTable[i] = c;
    }
  }
  crc = ~crc;
  for (size_t i = 0; i < n; i++) crc = crcTable[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
  return ~crc;
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) out.push_back((uint8_t)(v >> s));
}

// A chunk: length, type, data, CRC of type and data
void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  put32(out, (uint32_t)data.size());
  size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  put32(out, crc32(out.data() + start, out.size() - start));
}

// zlib's stored blocks: no compression
std::vector<uint8_t> storedZlib(const std::vector<uint8_t>& raw) {
  std::vector<uint8_t> z = {0x78, 0x01};
  z.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
  size_t at = 0;
  do {
    size_t n = std::min<size_t>(65535, raw.size() - at);
    bool last = at + n == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back((uint8_t)(n & 0xff));
    z.push_back((uint8_t)(n >> 8));
    z.push_back((uint8_t)(~n & 0xff));
    z.push_back((uint8_t)((~n >> 8) & 0xff));
    z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
    at += n;
  } while (at < raw.size());
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  put32(z, (b << 16) | a);
  return z;
}

}  // namespace

std::vector<uint8_t> png_from_rgb565(int w, int h, const uint16_t* pixels, Deflate deflate) {
  // the image data: each row a filter byte (0, none) and its RGB bytes
  std::vector<uint8_t> raw;
  raw.reserve((size_t)h * (1 + 3 * (size_t)w));
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    for (int x = 0; x < w; x++) {
      uint16_t v = pixels[(size_t)y * w + x];
      uint8_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
      raw.push_back((uint8_t)((r * 255 + 15) / 31));
      raw.push_back((uint8_t)((g * 255 + 31) / 63));
      raw.push_back((uint8_t)((b * 255 + 15) / 31));
    }
  }

  std::vector<uint8_t> z = deflate ? deflate(raw) : std::vector<uint8_t>();
  if (z.empty()) z = storedZlib(raw);

  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  std::vector<uint8_t> ihdr;
  put32(ihdr, (uint32_t)w);
  put32(ihdr, (uint32_t)h);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8 bits, RGB, deflate, no filter method, not interlaced
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", {});
  return out;
}
