// The ring buffer and the resampler of the audio stream, on the host.
#include <unity.h>

#include <cmath>
#include <vector>

#include "stream_dsp.h"

void setUp() {}
void tearDown() {}

// ---- ByteRing ----

void test_ring_in_and_out() {
  uint8_t storage[8];
  ByteRing ring(storage, sizeof(storage));
  TEST_ASSERT_EQUAL_UINT(0, ring.available());
  TEST_ASSERT_EQUAL_UINT(8, ring.space());
  const uint8_t in[5] = {1, 2, 3, 4, 5};
  TEST_ASSERT_EQUAL_UINT(5, ring.write(in, 5));
  uint8_t out[8] = {};
  TEST_ASSERT_EQUAL_UINT(3, ring.read(out, 3));
  TEST_ASSERT_EQUAL_UINT8(1, out[0]);
  TEST_ASSERT_EQUAL_UINT8(3, out[2]);
  TEST_ASSERT_EQUAL_UINT(2, ring.available());
}

void test_ring_wraps_around() {
  uint8_t storage[8];
  ByteRing ring(storage, sizeof(storage));
  uint8_t data[6] = {10, 11, 12, 13, 14, 15}, out[8] = {};
  ring.write(data, 6);
  ring.read(out, 5);  // the next write wraps the end of the buffer
  const uint8_t more[5] = {20, 21, 22, 23, 24};
  TEST_ASSERT_EQUAL_UINT(5, ring.write(more, 5));
  TEST_ASSERT_EQUAL_UINT(6, ring.read(out, 8));
  const uint8_t expected[6] = {15, 20, 21, 22, 23, 24};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 6);
}

void test_ring_full_and_empty_differ() {
  uint8_t storage[4];
  ByteRing ring(storage, sizeof(storage));
  const uint8_t data[6] = {1, 2, 3, 4, 5, 6};
  TEST_ASSERT_EQUAL_UINT(4, ring.write(data, 6));  // only what fits
  TEST_ASSERT_EQUAL_UINT(0, ring.space());
  TEST_ASSERT_EQUAL_UINT(0, ring.write(data, 1));
  uint8_t out[4];
  TEST_ASSERT_EQUAL_UINT(4, ring.read(out, 4));
  TEST_ASSERT_EQUAL_UINT(0, ring.available());
  TEST_ASSERT_EQUAL_UINT(0, ring.read(out, 1));
}

void test_ring_clear() {
  uint8_t storage[4];
  ByteRing ring(storage, sizeof(storage));
  const uint8_t data[3] = {1, 2, 3};
  ring.write(data, 3);
  ring.clear();
  TEST_ASSERT_EQUAL_UINT(0, ring.available());
  TEST_ASSERT_EQUAL_UINT(4, ring.space());
}

// ---- WAV ----

static std::vector<uint8_t> wav(int rate, int channels, int bits, int format = 1, bool extraChunk = false) {
  std::vector<uint8_t> h;
  auto u32 = [&](uint32_t v) {
    for (int i = 0; i < 4; i++) h.push_back((v >> (8 * i)) & 0xff);
  };
  auto u16 = [&](uint16_t v) {
    h.push_back(v & 0xff);
    h.push_back(v >> 8);
  };
  auto id = [&](const char* s) { h.insert(h.end(), s, s + 4); };
  id("RIFF");
  u32(0);
  id("WAVE");
  id("fmt ");
  u32(16);
  u16(format);
  u16(channels);
  u32(rate);
  u32(rate * channels * bits / 8);
  u16(channels * bits / 8);
  u16(bits);
  if (extraChunk) {  // e.g. a LIST chunk of tags, odd length: padded
    id("LIST");
    u32(5);
    h.insert(h.end(), {'a', 'b', 'c', 'd', 'e', 0});
  }
  id("data");
  u32(1000);
  h.push_back(0x34);  // the first sample
  h.push_back(0x12);
  return h;
}

void test_wav_header() {
  std::vector<uint8_t> h = wav(16000, 1, 16);
  WavInfo info;
  TEST_ASSERT_EQUAL_INT(1, parseWavHeader(h.data(), h.size(), info));
  TEST_ASSERT_EQUAL_INT(16000, info.rate);
  TEST_ASSERT_EQUAL_INT(1, info.channels);
  TEST_ASSERT_EQUAL_UINT(44, info.dataOffset);  // Piper's: the standard 44-byte header
  TEST_ASSERT_EQUAL_HEX8(0x34, h[info.dataOffset]);
}

void test_wav_header_with_another_chunk() {
  std::vector<uint8_t> h = wav(22050, 2, 16, 1, true);
  WavInfo info;
  TEST_ASSERT_EQUAL_INT(1, parseWavHeader(h.data(), h.size(), info));
  TEST_ASSERT_EQUAL_INT(22050, info.rate);
  TEST_ASSERT_EQUAL_INT(2, info.channels);
  TEST_ASSERT_EQUAL_HEX8(0x34, h[info.dataOffset]);
}

void test_wav_header_needs_more() {
  std::vector<uint8_t> h = wav(16000, 1, 16);
  WavInfo info;
  for (size_t n : {0, 3, 11, 20, 40, 43}) TEST_ASSERT_EQUAL_INT_MESSAGE(0, parseWavHeader(h.data(), n, info), "short");
}

void test_not_a_wav_we_play() {
  WavInfo info;
  const uint8_t mp3[16] = {0xff, 0xf3, 0x44, 0xc4};
  TEST_ASSERT_EQUAL_INT(-1, parseWavHeader(mp3, sizeof(mp3), info));
  const uint8_t id3[16] = {'I', 'D', '3', 4};
  TEST_ASSERT_EQUAL_INT(-1, parseWavHeader(id3, sizeof(id3), info));
  std::vector<uint8_t> h = wav(16000, 1, 8);
  TEST_ASSERT_EQUAL_INT(-1, parseWavHeader(h.data(), h.size(), info));  // 8-bit
  h = wav(16000, 1, 32, 3);
  TEST_ASSERT_EQUAL_INT(-1, parseWavHeader(h.data(), h.size(), info));  // float
  h = wav(16000, 6, 16);
  TEST_ASSERT_EQUAL_INT(-1, parseWavHeader(h.data(), h.size(), info));  // 5.1
}

void test_pcm_ms() {
  TEST_ASSERT_EQUAL_UINT32(1000, pcmMs(48000, 24000, 1));     // Kokoro: 24 kHz mono, 48000 bytes a second
  TEST_ASSERT_EQUAL_UINT32(73300, pcmMs(3518400, 24000, 1));  // KHOU's ATIS: 73.3 s
  TEST_ASSERT_EQUAL_UINT32(500, pcmMs(32000, 16000, 2));
  TEST_ASSERT_EQUAL_UINT32(0, pcmMs(1000, 0, 1));
  TEST_ASSERT_EQUAL_UINT32(0, pcmMs(1000, 16000, 0));
}

// ---- StreamUrl ----

void test_url_parts() {
  StreamUrl u;
  TEST_ASSERT_TRUE(parseStreamUrl("http://example.com:8000/tower.mp3?x=1", u));
  TEST_ASSERT_FALSE(u.tls);
  TEST_ASSERT_EQUAL_STRING("example.com", u.host.c_str());
  TEST_ASSERT_EQUAL_UINT16(8000, u.port);
  TEST_ASSERT_EQUAL_STRING("/tower.mp3?x=1", u.path.c_str());
}

void test_url_defaults() {
  StreamUrl u;
  TEST_ASSERT_TRUE(parseStreamUrl("http://192.168.1.5", u));
  TEST_ASSERT_EQUAL_UINT16(80, u.port);
  TEST_ASSERT_EQUAL_STRING("/", u.path.c_str());
  TEST_ASSERT_TRUE(parseStreamUrl("https://radio.example.org/live", u));
  TEST_ASSERT_TRUE(u.tls);
  TEST_ASSERT_EQUAL_UINT16(443, u.port);
}

void test_url_rejects_the_unusable() {
  StreamUrl u;
  for (const char* bad : {"", "example.com/x", "ftp://example.com/x", "http://", "http:///x",
                          "http://user:pw@example.com/", "http://example.com:0/", "http://example.com:99999/",
                          "http://example.com:/", "http://example.com:80a/", "http://[::1]/x"})
    TEST_ASSERT_FALSE_MESSAGE(parseStreamUrl(bad, u), bad);
}

void test_redirects_resolve() {
  StreamUrl from;
  parseStreamUrl("http://a.example.com:8000/x", from);
  TEST_ASSERT_EQUAL_STRING("https://b.example.com/y", resolveLocation(from, "https://b.example.com/y").c_str());
  TEST_ASSERT_EQUAL_STRING("http://a.example.com:8000/z", resolveLocation(from, "/z").c_str());
  TEST_ASSERT_EQUAL_STRING("http://a.example.com:8000/z", resolveLocation(from, "z").c_str());
  parseStreamUrl("https://a.example.com/x", from);
  TEST_ASSERT_EQUAL_STRING("https://a.example.com/z", resolveLocation(from, "/z").c_str());
}

// ---- Resampler ----

static std::vector<int16_t> run(Resampler& r, const std::vector<int16_t>& in) {
  std::vector<int16_t> out(2 * r.maxFrames(in.size()));
  size_t frames = r.process(in.data(), in.size(), out.data(), out.size() / 2);
  out.resize(2 * frames);
  return out;
}

void test_same_rate_passes_through_as_stereo() {
  Resampler r(16000);
  r.configure(16000, 1);
  std::vector<int16_t> out = run(r, {100, -200, 300});
  TEST_ASSERT_EQUAL_UINT(6, out.size());
  TEST_ASSERT_EQUAL_INT16(100, out[0]);
  TEST_ASSERT_EQUAL_INT16(100, out[1]);  // both channels
  TEST_ASSERT_EQUAL_INT16(-200, out[2]);
  TEST_ASSERT_EQUAL_INT16(300, out[4]);
}

void test_output_count_follows_the_rates() {
  for (int rate : {8000, 22050, 32000, 44100, 48000}) {
    Resampler r(16000);
    r.configure(rate, 1);
    std::vector<int16_t> in(rate, 1000);  // one second
    std::vector<int16_t> out = run(r, in);
    TEST_ASSERT_INT_WITHIN(1, 16000, (int)out.size() / 2);
  }
}

void test_a_constant_stays_constant() {
  for (int rate : {8000, 22050, 44100, 48000}) {
    Resampler r(16000);
    r.configure(rate, 1);
    std::vector<int16_t> out = run(r, std::vector<int16_t>(rate / 4, 5000));
    for (size_t i = 0; i < out.size(); i++) TEST_ASSERT_EQUAL_INT16(5000, out[i]);
  }
}

void test_stereo_is_mixed_to_mono() {
  Resampler r(16000);
  r.configure(16000, 2);
  std::vector<int16_t> out = run(r, {1000, 3000, -400, 400});  // (L, R) frames
  TEST_ASSERT_EQUAL_UINT(4, out.size());
  TEST_ASSERT_EQUAL_INT16(2000, out[0]);
  TEST_ASSERT_EQUAL_INT16(2000, out[1]);
  TEST_ASSERT_EQUAL_INT16(0, out[2]);
}

void test_downsampling_averages() {
  Resampler r(16000);
  r.configure(32000, 1);  // two input samples per output sample
  std::vector<int16_t> out = run(r, {100, 300, -50, 50});
  TEST_ASSERT_EQUAL_UINT(4, out.size());
  TEST_ASSERT_EQUAL_INT16(200, out[0]);
  TEST_ASSERT_EQUAL_INT16(0, out[2]);
}

void test_upsampling_holds() {
  Resampler r(16000);
  r.configure(8000, 1);  // two output samples per input sample
  std::vector<int16_t> out = run(r, {400, -800});
  TEST_ASSERT_EQUAL_UINT(8, out.size());
  TEST_ASSERT_EQUAL_INT16(400, out[0]);
  TEST_ASSERT_EQUAL_INT16(400, out[2]);
  TEST_ASSERT_EQUAL_INT16(-800, out[4]);
  TEST_ASSERT_EQUAL_INT16(-800, out[6]);
}

void test_the_result_continues_across_calls() {
  std::vector<int16_t> in(4410);
  for (size_t i = 0; i < in.size(); i++) in[i] = (int16_t)(8000 * sinf(2 * (float)M_PI * 440 * i / 44100));
  Resampler whole(16000), parts(16000);
  whole.configure(44100, 1);
  parts.configure(44100, 1);
  std::vector<int16_t> a = run(whole, in);
  std::vector<int16_t> b, chunk;
  for (size_t at = 0; at < in.size(); at += 1000) {
    size_t n = std::min<size_t>(1000, in.size() - at);
    chunk = run(parts, std::vector<int16_t>(in.begin() + at, in.begin() + at + n));
    b.insert(b.end(), chunk.begin(), chunk.end());
  }
  TEST_ASSERT_EQUAL_UINT(a.size(), b.size());
  TEST_ASSERT_EQUAL_INT16_ARRAY(a.data(), b.data(), a.size());
}

void test_volume() {
  Resampler r(16000);
  r.configure(16000, 1);
  r.setVolume(128);
  std::vector<int16_t> out = run(r, {1000, -1000});
  TEST_ASSERT_EQUAL_INT16(500, out[0]);
  TEST_ASSERT_EQUAL_INT16(-500, out[2]);
  r.setVolume(0);
  out = run(r, {1000});
  TEST_ASSERT_EQUAL_INT16(0, out[0]);
}

void test_output_is_capped() {
  Resampler r(16000);
  r.configure(16000, 1);
  std::vector<int16_t> in(100, 7), out(2 * 10);
  TEST_ASSERT_EQUAL_UINT(10, r.process(in.data(), in.size(), out.data(), 10));  // the rest dropped, no overrun
}

void test_unconfigured_makes_nothing() {
  Resampler r(16000);
  std::vector<int16_t> in(10, 1), out(20);
  TEST_ASSERT_EQUAL_UINT(0, r.process(in.data(), in.size(), out.data(), 10));
  TEST_ASSERT_EQUAL_UINT(0, r.maxFrames(10));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ring_in_and_out);
  RUN_TEST(test_ring_wraps_around);
  RUN_TEST(test_ring_full_and_empty_differ);
  RUN_TEST(test_ring_clear);
  RUN_TEST(test_wav_header);
  RUN_TEST(test_pcm_ms);
  RUN_TEST(test_wav_header_with_another_chunk);
  RUN_TEST(test_wav_header_needs_more);
  RUN_TEST(test_not_a_wav_we_play);
  RUN_TEST(test_url_parts);
  RUN_TEST(test_url_defaults);
  RUN_TEST(test_url_rejects_the_unusable);
  RUN_TEST(test_redirects_resolve);
  RUN_TEST(test_same_rate_passes_through_as_stereo);
  RUN_TEST(test_output_count_follows_the_rates);
  RUN_TEST(test_a_constant_stays_constant);
  RUN_TEST(test_stereo_is_mixed_to_mono);
  RUN_TEST(test_downsampling_averages);
  RUN_TEST(test_upsampling_holds);
  RUN_TEST(test_the_result_continues_across_calls);
  RUN_TEST(test_volume);
  RUN_TEST(test_output_is_capped);
  RUN_TEST(test_unconfigured_makes_nothing);
  return UNITY_END();
}
