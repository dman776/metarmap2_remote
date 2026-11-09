// Screenshots as PNG: the file's structure, and its stored blocks give back the pixels.
#include <unity.h>

#include <cstring>
#include <vector>

#include "png.h"

void setUp() {}
void tearDown() {}

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// The image bytes (filter byte + RGB per row) from a PNG of stored deflate blocks
static std::vector<uint8_t> inflateStored(const std::vector<uint8_t>& png, uint32_t* w, uint32_t* h) {
  const uint8_t* p = png.data() + 8;
  std::vector<uint8_t> z, raw;
  while (p < png.data() + png.size()) {
    uint32_t n = be32(p);
    if (!memcmp(p + 4, "IHDR", 4)) {
      *w = be32(p + 8);
      *h = be32(p + 12);
      TEST_ASSERT_EQUAL_UINT8(8, p[16]);  // bit depth
      TEST_ASSERT_EQUAL_UINT8(2, p[17]);  // RGB
    }
    if (!memcmp(p + 4, "IDAT", 4)) z.insert(z.end(), p + 8, p + 8 + n);
    p += 12 + n;
  }
  TEST_ASSERT_EQUAL_UINT8(0x78, z[0]);
  size_t at = 2;
  bool last = false;
  while (!last) {
    last = z[at] & 1;
    uint32_t len = z[at + 1] | z[at + 2] << 8, nlen = z[at + 3] | z[at + 4] << 8;
    TEST_ASSERT_EQUAL_UINT32(0xffff, len ^ nlen);
    raw.insert(raw.end(), z.begin() + at + 5, z.begin() + at + 5 + len);
    at += 5 + len;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  TEST_ASSERT_EQUAL_HEX32((b << 16) | a, be32(z.data() + at));
  return raw;
}

void test_signature_and_chunks() {
  uint16_t px[2] = {0xf800, 0x07e0};
  std::vector<uint8_t> png = png_from_rgb565(2, 1, px);
  const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  TEST_ASSERT_EQUAL_MEMORY(sig, png.data(), 8);
  TEST_ASSERT_EQUAL_MEMORY("IEND", png.data() + png.size() - 8, 4);
  TEST_ASSERT_EQUAL_HEX32(0xae426082, be32(png.data() + png.size() - 4));  // IEND's CRC, as every PNG has it
}

void test_pixels_round_trip() {
  uint16_t px[4] = {0xf800, 0x07e0, 0x001f, 0xffff};  // red, green, blue, white
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> raw = inflateStored(png_from_rgb565(2, 2, px), &w, &h);
  TEST_ASSERT_EQUAL_UINT32(2, w);
  TEST_ASSERT_EQUAL_UINT32(2, h);
  const uint8_t want[14] = {0, 255, 0, 0, 0, 255, 0, 0, 0, 0, 255, 255, 255, 255};
  TEST_ASSERT_EQUAL_MEMORY(want, raw.data(), sizeof(want));
}

void test_the_screen_takes_several_blocks() {
  std::vector<uint16_t> px(320 * 240, 0x1234);
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> raw = inflateStored(png_from_rgb565(320, 240, px.data()), &w, &h);
  TEST_ASSERT_EQUAL_UINT32(240 * (1 + 320 * 3), raw.size());  // more than 65535: four blocks
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_signature_and_chunks);
  RUN_TEST(test_pixels_round_trip);
  RUN_TEST(test_the_screen_takes_several_blocks);
  return UNITY_END();
}
