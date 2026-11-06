#include "stream.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <memory>

#include "audio.h"
#include "log.h"
#include "stream_dsp.h"

#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "third_party/minimp3.h"

namespace {

constexpr size_t kRingBytes = 192 * 1024;  // network to decoder, in PSRAM
constexpr size_t kPrebufferBytes = 16 * 1024;
constexpr uint32_t kPrebufferMs = 4000;          // a slow (low bitrate) stream starts with what it has by then...
constexpr size_t kPrebufferMinBytes = 4 * 1024;  // ...if that's at least this
constexpr size_t kNetChunk = 2048;
constexpr size_t kDecodeBytes = 16 * 1024;  // minimp3 wants a few frames of lookahead to find its footing
constexpr size_t kOutFrames = 4096;         // after resampling: a 8 kHz stream's frame doubles to 1152
constexpr uint32_t kTimeoutMs = 10 * 1000;
constexpr uint32_t kRetryMs = 4000;
constexpr int kMaxRedirects = 3;
constexpr int kVolume = 160;  // of 256: the little speaker distorts near full scale
constexpr uint32_t kReaderStack = 12 * 1024;
constexpr uint32_t kPlayerStack = 28 * 1024;  // minimp3 keeps about 15 KB of scratch on the stack
constexpr uint32_t kPublishMs = 250;

std::string streamUrl;
ByteRing* ring = nullptr;
SemaphoreHandle_t mutex = nullptr;
StreamState state;
uint32_t stateRev = 0;

std::atomic<bool> wanted{false};
std::atomic<uint32_t> epoch{0};    // the reader bumps it for each connection...
std::atomic<uint32_t> acked{0};    // ...and the player says it has dropped what it had (and may read the ring)
std::atomic<bool> flowing{false};  // the reader has the stream open

void publish(StreamState::Phase phase, const char* message = "") {
  xSemaphoreTake(mutex, portMAX_DELAY);
  bool changed = state.phase != phase || state.message != message;
  state.phase = phase;
  state.message = message;
  if (phase == StreamState::Off || phase == StreamState::Connecting) state.rate = 0;
  if (changed) stateRev++;
  xSemaphoreGive(mutex);
}

void publishBuffer(int rate = -1) {
  int pct = (int)(ring->available() * 100 / ring->size());
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (state.bufferPct != pct || (rate >= 0 && state.rate != rate)) {
    state.bufferPct = pct;
    if (rate >= 0) state.rate = rate;
    stateRev++;
  }
  xSemaphoreGive(mutex);
}

// ---- the reader: the network into the ring ---------------------------------------------------------------------

String headerValue(const String& line) {
  int colon = line.indexOf(':');
  String value = colon < 0 ? String("") : line.substring(colon + 1);
  value.trim();
  return value;
}

// Opens `url`, following redirects, and leaves the client at the start of the audio. False, with why, if it can't.
bool openStream(std::string url, std::unique_ptr<WiFiClient>& client, std::string& error) {
  for (int hop = 0; hop <= kMaxRedirects; hop++) {
    StreamUrl u;
    if (!parseStreamUrl(url, u)) {
      error = "bad stream URL";
      return false;
    }
    if (u.tls) {
      // A prototype: the audio isn't secret and nothing is sent but the request, so the certificate isn't checked.
      auto* secure = new WiFiClientSecure();
      secure->setInsecure();
      client.reset(secure);
    } else {
      client.reset(new WiFiClient());
    }
    client->setTimeout(kTimeoutMs / 1000);
    if (!client->connect(u.host.c_str(), u.port, kTimeoutMs)) {
      error = "can't connect";
      return false;
    }
    client->printf(
        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: metarmap-remote\r\nAccept: */*\r\nIcy-MetaData: 0\r\n"
        "Connection: close\r\n\r\n",
        u.path.c_str(), u.host.c_str());

    String status = client->readStringUntil('\n');
    status.trim();
    int code = 0;
    if (status.startsWith("HTTP/")) code = status.substring(9, 12).toInt();
    if (status.startsWith("ICY ")) code = status.substring(4, 7).toInt();  // Shoutcast
    String location, type;
    for (int i = 0; i < 40; i++) {  // the headers, up to the blank line
      String line = client->readStringUntil('\n');
      line.trim();
      if (line.length() == 0) break;
      String name = line.substring(0, max(0, line.indexOf(':')));
      name.toLowerCase();
      if (name == "location") location = headerValue(line);
      if (name == "content-type") type = headerValue(line);
    }
    if (code >= 301 && code <= 308 && location.length() > 0) {
      url = resolveLocation(u, location.c_str());
      continue;
    }
    if (code != 200) {
      error = code ? "server said " + std::to_string(code) : "no answer";
      return false;
    }
    type.toLowerCase();
    if (type.indexOf("aac") >= 0 || type.indexOf("ogg") >= 0 || type.indexOf("mp4") >= 0) {
      error = "not MP3 (" + std::string(type.c_str()) + ")";
      return false;
    }
    return true;
  }
  error = "too many redirects";
  return false;
}

// One connection: reads until it drops or the user stops it. Returns why it ended ("" when stopped).
std::string readOnce() {
  std::unique_ptr<WiFiClient> client;
  std::string error;
  publish(StreamState::Connecting);
  if (!openStream(streamUrl, client, error)) return error;

  // The player drops what it has of the last connection first.
  uint32_t mine = epoch.fetch_add(1) + 1;
  for (uint32_t waited = 0; acked.load() != mine; waited += 5) {
    if (waited > 2000 || !wanted) return wanted ? "decoder not answering" : "";
    delay(5);
  }
  flowing = true;
  publish(StreamState::Buffering);

  std::unique_ptr<uint8_t[]> chunk(new uint8_t[kNetChunk]);
  uint32_t lastData = millis();
  while (wanted) {
    if (ring->space() < kNetChunk) {
      delay(20);  // the decoder is behind: let TCP's window do the waiting
      continue;
    }
    int available = client->available();
    if (available <= 0) {
      if (!client->connected()) {
        flowing = false;
        return "stream ended";
      }
      if (millis() - lastData > kTimeoutMs) {
        flowing = false;
        return "stream stalled";
      }
      delay(5);
      continue;
    }
    int n = client->read(chunk.get(), std::min<size_t>(available, kNetChunk));
    if (n <= 0) continue;
    ring->write(chunk.get(), n);
    lastData = millis();
  }
  flowing = false;
  return "";
}

void readerTask(void*) {
  for (;;) {
    if (!wanted) {
      publish(StreamState::Off);
      delay(100);
      continue;
    }
    std::string why;
    if (WiFi.status() != WL_CONNECTED) {
      why = "no Wi-Fi";
    } else {
      why = readOnce();
    }
    if (!wanted) continue;
    LOGF("stream: %s; trying again\n", why.c_str());
    publish(StreamState::Retrying, why.c_str());
    for (uint32_t waited = 0; wanted && waited < kRetryMs; waited += 100) delay(100);
  }
}

// ---- the player: the ring to the speaker --------------------------------------------------------------------------

struct Player {
  mp3dec_t decoder;
  mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  uint8_t* input = nullptr;  // up to kDecodeBytes of MP3, from `start`
  size_t start = 0, length = 0;
  int16_t* out = nullptr;  // kOutFrames stereo frames
  Resampler resampler{kAudioRate};
  bool prebuffering = true;
  uint32_t prebufferSince = 0;
  uint32_t lastPublish = 0;
  uint32_t seen = 0;
};

void restart(Player& p) {
  ring->clear();
  mp3dec_init(&p.decoder);
  p.start = p.length = 0;
  p.resampler.configure(0, 1);
  p.prebuffering = true;
  p.prebufferSince = millis();
}

void play(Player& p, int samples, int channels, int rate) {
  if (rate != p.resampler.inRate() || channels != p.resampler.channels()) {
    p.resampler.configure(rate, channels);
    LOGF("stream: %d Hz, %d channel%s\n", rate, channels, channels == 1 ? "" : "s");
    publishBuffer(rate);
  }
  size_t frames = p.resampler.process(p.pcm, (size_t)samples * channels, p.out, kOutFrames);
  for (size_t done = 0; done < frames && wanted && epoch.load() == p.seen;) {
    size_t n = audio_write(p.out + 2 * done, frames - done, 100);  // blocks: the speaker sets the pace
    done += n;
  }
}

bool readyToPlay(const Player& p) {
  size_t have = ring->available() + (p.length - p.start);
  if (have >= kPrebufferBytes) return true;
  return have >= kPrebufferMinBytes && millis() - p.prebufferSince >= kPrebufferMs;
}

void playerTask(void*) {
  Player* p = new Player();
  p->input = static_cast<uint8_t*>(heap_caps_malloc(kDecodeBytes, MALLOC_CAP_INTERNAL));
  p->out = static_cast<int16_t*>(heap_caps_malloc(kOutFrames * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM));
  if (!p->input || !p->out) {
    LOGF("stream: out of memory for the decoder\n");
    vTaskDelete(nullptr);
  }
  p->resampler.setVolume(kVolume);
  restart(*p);

  for (;;) {
    uint32_t e = epoch.load();
    if (e != p->seen) {  // a new connection: forget the last one's data
      restart(*p);
      p->seen = e;
      acked = e;
    }
    if (!wanted || !flowing) {
      delay(20);
      continue;
    }
    if (millis() - p->lastPublish >= kPublishMs) {
      p->lastPublish = millis();
      publishBuffer();
    }

    if (p->prebuffering) {
      if (!readyToPlay(*p)) {
        delay(20);
        continue;
      }
      p->prebuffering = false;
    }

    // top up the decoder's input
    if (p->start > kDecodeBytes / 2) {
      memmove(p->input, p->input + p->start, p->length - p->start);
      p->length -= p->start;
      p->start = 0;
    }
    p->length += ring->read(p->input + p->length, kDecodeBytes - p->length);
    size_t have = p->length - p->start;
    if (have < 1024) {  // underrun: wait for more, silently
      p->prebuffering = true;
      p->prebufferSince = millis();
      publish(StreamState::Buffering);
      delay(20);
      continue;
    }

    mp3dec_frame_info_t info;
    int samples = mp3dec_decode_frame(&p->decoder, p->input + p->start, (int)have, p->pcm, &info);
    if (info.frame_bytes > 0) {
      p->start += info.frame_bytes;
    } else if (have >= kDecodeBytes - p->start && p->start == 0) {
      p->start = p->length;  // a full buffer with no frame in it: junk, start over
    } else {
      delay(5);  // part of a frame: more is coming
    }
    if (samples > 0 && info.channels > 0) {
      publish(StreamState::Playing);
      play(*p, samples, info.channels, info.hz);
    }
  }
}

}  // namespace

void stream_begin(const std::string& url) {
  if (url.empty() || !audio_ready()) return;
  StreamUrl check;
  if (!parseStreamUrl(url, check)) {
    LOGF("stream: %s isn't an http:// or https:// URL\n", url.c_str());
    return;
  }
  uint8_t* storage = static_cast<uint8_t*>(heap_caps_malloc(kRingBytes, MALLOC_CAP_SPIRAM));
  if (!storage) {
    LOGF("stream: no PSRAM for the buffer\n");
    return;
  }
  streamUrl = url;
  ring = new ByteRing(storage, kRingBytes);
  mutex = xSemaphoreCreateMutex();
  // Core 0, beside Wi-Fi, as the map client and the updater; LVGL runs in the Arduino loop on core 1. The player is
  // above them (priority 2): it spends nearly all its time waiting on the speaker.
  xTaskCreatePinnedToCore(readerTask, "stream_net", kReaderStack, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(playerTask, "stream_play", kPlayerStack, nullptr, 2, nullptr, 0);
  LOGF("stream: ready (%s)\n", url.c_str());
}

bool stream_available() { return ring != nullptr; }

void stream_set_playing(bool on) {
  if (!ring) return;
  if (on && !wanted) publish(StreamState::Connecting);
  wanted = on;
}

bool stream_get(StreamState& out, uint32_t& rev) {
  if (!ring) return false;
  xSemaphoreTake(mutex, portMAX_DELAY);
  bool changed = rev != stateRev;
  if (changed) {
    out = state;
    rev = stateRev;
  }
  xSemaphoreGive(mutex);
  return changed;
}
