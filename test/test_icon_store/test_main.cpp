// The icons fetched from the Chart: names, the saved file, the LVGL image, the backoff after a failure, and which
// icons to ask for (one at a time).
#include <unity.h>

#include <cstring>

#include "icon_store.h"

using icon_store::Failure;
using icon_store::Fetcher;
using icon_store::Have;
using icon_store::IconDone;
using icon_store::kBadAnswerMs;
using icon_store::kFileBytes;
using icon_store::kMaskBytes;
using icon_store::kNotFoundMs;
using icon_store::Misses;
using icon_store::Want;

void setUp() {}
void tearDown() {}

namespace {

Visualizer vis(const char* id, const char* icon, const char* rev = "", bool exclusive = false) {
  Visualizer v;
  v.id = id;
  v.icon = icon;
  v.iconRev = rev;
  v.exclusive = exclusive;
  return v;
}

MapStatus status(std::vector<Visualizer> list, bool hasIconRev) {
  MapStatus s;
  s.valid = true;
  s.hasIconRev = hasIconRev;
  s.visualizers = std::move(list);
  return s;
}

// The built-in set as icons.c has it (a few), and what's saved, for missingOrStale()
struct Fake {
  std::vector<std::pair<std::string, std::string>> compiled{{"plane", "7cb23636"}, {"rainbow", "505962a2"}};
  std::vector<std::pair<std::string, std::string>> saved;
  icon_store::Lookup lookup() const {
    return [this](const std::string& name) {
      Have h;
      for (const auto& c : compiled) {
        if (c.first == name) h.compiled = true, h.compiledRev = c.second;
      }
      for (const auto& c : saved) {
        if (c.first == name) h.saved = true, h.savedRev = c.second;
      }
      return h;
    };
  }
};

std::string names(const std::vector<Want>& wants) {
  std::string s;
  for (const Want& w : wants) s += w.name + "@" + w.rev + " ";
  return s;
}

// A mock of lv_image_dsc_t's fields (LVGL isn't built for the host)
struct FakeDsc {
  struct {
    uint32_t magic = 0, cf = 0, flags = 0, w = 0, h = 0, stride = 0;
  } header;
  uint32_t data_size = 0;
  const uint8_t* data = nullptr;
};

}  // namespace

// ---- names and revs ----

void test_valid_name() {
  TEST_ASSERT_TRUE(icon_store::validName("plane"));
  TEST_ASSERT_TRUE(icon_store::validName("hourglass"));
  TEST_ASSERT_TRUE(icon_store::validName("a"));
  TEST_ASSERT_TRUE(icon_store::validName("wind_gusts2"));
  TEST_ASSERT_TRUE(icon_store::validName(std::string(32, 'a')));
  TEST_ASSERT_FALSE(icon_store::validName(std::string(33, 'a')));
  TEST_ASSERT_FALSE(icon_store::validName(""));
  TEST_ASSERT_FALSE(icon_store::validName("../x"));
  TEST_ASSERT_FALSE(icon_store::validName("a/b"));
  TEST_ASSERT_FALSE(icon_store::validName("Plane"));
  TEST_ASSERT_FALSE(icon_store::validName("2plane"));
  TEST_ASSERT_FALSE(icon_store::validName("_plane"));
  TEST_ASSERT_FALSE(icon_store::validName("pla ne"));
  TEST_ASSERT_FALSE(icon_store::validName("plane.bin"));
}

void test_valid_rev() {
  TEST_ASSERT_TRUE(icon_store::validRev("7cb23636"));
  TEST_ASSERT_FALSE(icon_store::validRev("7CB23636"));
  TEST_ASSERT_FALSE(icon_store::validRev("7cb2363"));
  TEST_ASSERT_FALSE(icon_store::validRev("7cb236361"));
  TEST_ASSERT_FALSE(icon_store::validRev("7cb2363g"));
  TEST_ASSERT_FALSE(icon_store::validRev(""));
}

void test_etag_rev() {
  TEST_ASSERT_EQUAL_STRING("7cb23636", icon_store::etagRev("\"7cb23636\"").c_str());
  TEST_ASSERT_EQUAL_STRING("7cb23636", icon_store::etagRev("W/\"7cb23636\"").c_str());
  TEST_ASSERT_EQUAL_STRING("7cb23636", icon_store::etagRev("7cb23636").c_str());
  TEST_ASSERT_EQUAL_STRING("", icon_store::etagRev("\"nope\"").c_str());
  TEST_ASSERT_EQUAL_STRING("", icon_store::etagRev("").c_str());
}

// ---- the saved file ----

void test_icon_file_round_trip() {
  uint8_t mask[kMaskBytes];
  for (size_t i = 0; i < kMaskBytes; i++) mask[i] = (uint8_t)i;
  uint8_t file[kFileBytes];
  icon_store::buildIconFile("3e3d702c", mask, file);
  TEST_ASSERT_EQUAL_UINT(410, kFileBytes);
  TEST_ASSERT_EQUAL_UINT8('I', file[0]);
  TEST_ASSERT_EQUAL_UINT8(1, file[1]);
  TEST_ASSERT_EQUAL_MEMORY("3e3d702c", file + 2, 8);

  std::string rev;
  uint8_t got[kMaskBytes] = {};
  TEST_ASSERT_TRUE(icon_store::parseIconFile(file, sizeof(file), rev, got));
  TEST_ASSERT_EQUAL_STRING("3e3d702c", rev.c_str());
  TEST_ASSERT_EQUAL_MEMORY(mask, got, kMaskBytes);

  // saved without a rev (an answer without an ETag from a Chart that doesn't say): eight zero bytes, "" back
  icon_store::buildIconFile("", mask, file);
  for (int i = 2; i < 10; i++) TEST_ASSERT_EQUAL_UINT8(0, file[i]);
  rev = "x";
  TEST_ASSERT_TRUE(icon_store::parseIconFile(file, sizeof(file), rev, got));
  TEST_ASSERT_EQUAL_STRING("", rev.c_str());
}

void test_icon_file_rejects() {
  uint8_t mask[kMaskBytes] = {};
  uint8_t file[kFileBytes + 1] = {};
  icon_store::buildIconFile("3e3d702c", mask, file);
  std::string rev = "kept";
  uint8_t got[kMaskBytes];
  TEST_ASSERT_FALSE(icon_store::parseIconFile(file, kFileBytes - 1, rev, got));  // short
  TEST_ASSERT_FALSE(icon_store::parseIconFile(file, kFileBytes + 1, rev, got));  // long
  TEST_ASSERT_FALSE(icon_store::parseIconFile(nullptr, kFileBytes, rev, got));
  file[0] = 'X';  // bad magic
  TEST_ASSERT_FALSE(icon_store::parseIconFile(file, kFileBytes, rev, got));
  file[0] = 'I';
  file[1] = 2;  // a version this firmware doesn't know
  TEST_ASSERT_FALSE(icon_store::parseIconFile(file, kFileBytes, rev, got));
  file[1] = 1;
  file[2] = 'Z';  // a rev that isn't one
  TEST_ASSERT_FALSE(icon_store::parseIconFile(file, kFileBytes, rev, got));
  TEST_ASSERT_EQUAL_STRING("kept", rev.c_str());  // untouched by a rejected file
}

void test_fill_dsc() {
  uint8_t mask[kMaskBytes] = {};
  FakeDsc d;
  d.header.flags = 7;  // cleared
  icon_store::fillDsc(d, mask, 0x19, 0x0e);
  TEST_ASSERT_EQUAL_UINT32(0x19, d.header.magic);
  TEST_ASSERT_EQUAL_UINT32(0x0e, d.header.cf);
  TEST_ASSERT_EQUAL_UINT32(0, d.header.flags);
  TEST_ASSERT_EQUAL_UINT32(20, d.header.w);
  TEST_ASSERT_EQUAL_UINT32(20, d.header.h);
  TEST_ASSERT_EQUAL_UINT32(20, d.header.stride);
  TEST_ASSERT_EQUAL_UINT32(400, d.data_size);
  TEST_ASSERT_EQUAL_PTR(mask, d.data);
}

// ---- the backoff ----

void test_misses_backoff() {
  Misses m;
  m.failed("hourglass", "3e3d702c", Failure::NotFound, 1000);
  TEST_ASSERT_TRUE(m.blocked("hourglass", "3e3d702c", 1000));
  TEST_ASSERT_TRUE(m.blocked("hourglass", "3e3d702c", 1000 + kNotFoundMs - 1));
  TEST_ASSERT_FALSE(m.blocked("hourglass", "3e3d702c", 1000 + kNotFoundMs));
  TEST_ASSERT_FALSE(m.blocked("hourglass", "aaaaaaaa", 1000));  // a new rev: asked at once
  TEST_ASSERT_FALSE(m.blocked("plane", "3e3d702c", 1000));
  TEST_ASSERT_FALSE(m.unsupported());  // a 404 with a rev: the Chart has /api/icon, just not this one now

  m.failed("plane", "7cb23636", Failure::BadAnswer, 5000);
  TEST_ASSERT_TRUE(m.blocked("plane", "7cb23636", 5000 + kBadAnswerMs - 1));
  TEST_ASSERT_FALSE(m.blocked("plane", "7cb23636", 5000 + kBadAnswerMs));
  TEST_ASSERT_EQUAL_UINT32(60000, kBadAnswerMs);
  TEST_ASSERT_EQUAL_UINT32(600000, kNotFoundMs);

  m.failed("wind", "d3c55152", Failure::Connection, 5000);  // not the icon's fault
  TEST_ASSERT_FALSE(m.blocked("wind", "d3c55152", 5000));

  m.failed("wind", "d3c55152", Failure::NoRoom, 5000);
  TEST_ASSERT_TRUE(m.blocked("wind", "d3c55152", 5000 + kNotFoundMs - 1));

  // wrapping round millis()
  m.failed("eye", "96a6ead3", Failure::BadAnswer, 0xFFFFFF00u);
  TEST_ASSERT_TRUE(m.blocked("eye", "96a6ead3", 0x100));
  TEST_ASSERT_FALSE(m.blocked("eye", "96a6ead3", 0xFFFFFF00u + kBadAnswerMs));

  m.clear();  // a switch to another Chart
  TEST_ASSERT_FALSE(m.blocked("hourglass", "3e3d702c", 1000));
  TEST_ASSERT_FALSE(m.blocked("plane", "7cb23636", 5000));
}

void test_misses_unsupported() {
  Misses m;
  m.failed("hourglass", "", Failure::NotFound, 1000);  // an older Chart: no /api/icon
  TEST_ASSERT_TRUE(m.unsupported());
  m.clear();
  TEST_ASSERT_FALSE(m.unsupported());
  m.failed("hourglass", "", Failure::BadAnswer, 1000);  // a timeout says nothing about /api/icon
  TEST_ASSERT_FALSE(m.unsupported());
}

// ---- which icons to ask for ----

void test_missing_with_revs() {
  Fake fake;
  Misses misses;
  MapStatus s =
      status({vis("flight_category", "plane", "7cb23636"), vis("faa_delays", "hourglass", "3e3d702c"),
              vis("rainbow", "rainbow", "505962a2", true), vis("wind", "wind", "d3c55152"),
              vis("custom", "sparkle", ""), vis("bad", "../x", "aaaaaaaa"), vis("dup", "hourglass", "3e3d702c")},
             true);
  // plane and rainbow are built in at the Chart's rev; sparkle has no mask; ../x isn't a name; hourglass once
  TEST_ASSERT_EQUAL_STRING("hourglass@3e3d702c wind@d3c55152 ",
                           names(missingOrStale(s, fake.lookup(), misses, 0)).c_str());

  fake.saved = {{"hourglass", "3e3d702c"}, {"wind", "00000000"}};  // wind saved at an older rev
  TEST_ASSERT_EQUAL_STRING("wind@d3c55152 ", names(missingOrStale(s, fake.lookup(), misses, 0)).c_str());

  // a changed mask on the Chart: the built-in one isn't it any more
  s.visualizers[0].iconRev = "12345678";
  TEST_ASSERT_EQUAL_STRING("plane@12345678 wind@d3c55152 ", names(missingOrStale(s, fake.lookup(), misses, 0)).c_str());

  misses.failed("plane", "12345678", Failure::BadAnswer, 0);
  TEST_ASSERT_EQUAL_STRING("wind@d3c55152 ", names(missingOrStale(s, fake.lookup(), misses, 10)).c_str());
  TEST_ASSERT_EQUAL_STRING("plane@12345678 wind@d3c55152 ",
                           names(missingOrStale(s, fake.lookup(), misses, kBadAnswerMs)).c_str());

  MapStatus none;  // no /api/status yet (or a switch cleared it)
  TEST_ASSERT_EQUAL_UINT(0, missingOrStale(none, fake.lookup(), misses, 0).size());
}

void test_missing_old_chart() {
  Fake fake;
  Misses misses;
  // a Chart without iconRev: what's built in or saved will do; the rest is asked for without a rev
  MapStatus s = status(
      {vis("flight_category", "plane"), vis("faa_delays", "hourglass"), vis("wind", "wind"), vis("old", "")}, false);
  fake.saved = {{"wind", "d3c55152"}};
  TEST_ASSERT_EQUAL_STRING("hourglass@ ", names(missingOrStale(s, fake.lookup(), misses, 0)).c_str());
  // its 404: no /api/icon, nothing more asked of it, however long
  misses.failed("hourglass", "", Failure::NotFound, 0);
  TEST_ASSERT_EQUAL_UINT(0, missingOrStale(s, fake.lookup(), misses, 0).size());
  TEST_ASSERT_EQUAL_UINT(0, missingOrStale(s, fake.lookup(), misses, 10 * kNotFoundMs).size());
  misses.clear();  // until a switch
  TEST_ASSERT_EQUAL_UINT(1, missingOrStale(s, fake.lookup(), misses, 0).size());
}

// ---- one at a time ----

void test_fetcher_one_at_a_time() {
  Fake fake;
  Fetcher f;
  MapStatus s = status({vis("faa_delays", "hourglass", "3e3d702c"), vis("wind", "wind", "d3c55152")}, true);
  Want w;
  TEST_ASSERT_FALSE(f.next(s, fake.lookup(), false, 0, w));  // offline: nothing
  TEST_ASSERT_TRUE(f.next(s, fake.lookup(), true, 0, w));
  TEST_ASSERT_EQUAL_STRING("hourglass", w.name.c_str());
  TEST_ASSERT_EQUAL_STRING("3e3d702c", w.rev.c_str());
  f.asked(0);
  TEST_ASSERT_TRUE(f.waiting());
  TEST_ASSERT_FALSE(f.next(s, fake.lookup(), true, 100, w));  // waits for its answer

  IconDone d;
  d.name = "hourglass";
  d.rev = "3e3d702c";
  d.ok = true;
  d.saved = true;
  f.answered(d, true, 200);
  fake.saved = {{"hourglass", "3e3d702c"}};
  TEST_ASSERT_FALSE(f.waiting());
  TEST_ASSERT_TRUE(f.next(s, fake.lookup(), true, 200, w));
  TEST_ASSERT_EQUAL_STRING("wind", w.name.c_str());
  f.asked(200);

  // a 404 for wind: not asked again for 10 minutes; nothing else to ask
  IconDone nf;
  nf.name = "wind";
  nf.rev = "d3c55152";
  nf.why = Failure::NotFound;
  f.answered(nf, false, 300);
  TEST_ASSERT_FALSE(f.next(s, fake.lookup(), true, 400, w));
  TEST_ASSERT_TRUE(f.next(s, fake.lookup(), true, 300 + kNotFoundMs, w));

  // a switch: the backoff goes, and the saved one still counts
  f.answered(nf, false, 500);
  f.switched();
  TEST_ASSERT_TRUE(f.next(s, fake.lookup(), true, 600, w));
  TEST_ASSERT_EQUAL_STRING("wind", w.name.c_str());
}

void test_fetcher_gives_up_waiting() {
  Fake fake;
  Fetcher f;
  MapStatus s = status({vis("faa_delays", "hourglass", "3e3d702c")}, true);
  Want w;
  TEST_ASSERT_TRUE(f.next(s, fake.lookup(), true, 0, w));
  f.asked(0);
  TEST_ASSERT_FALSE(f.next(s, fake.lookup(), true, Fetcher::kAnswerMs - 1, w));
  TEST_ASSERT_TRUE(f.next(s, fake.lookup(), true, Fetcher::kAnswerMs, w));  // the answer was lost (a switch)
}

void test_fetcher_no_room() {
  Fake fake;
  Fetcher f;
  MapStatus s = status({vis("faa_delays", "hourglass", "3e3d702c")}, true);
  IconDone d;
  d.name = "hourglass";
  d.rev = "3e3d702c";
  d.ok = true;
  f.answered(d, false, 0);  // neither saved nor kept: not asked again at once (no loop)
  Want w;
  TEST_ASSERT_FALSE(f.next(s, fake.lookup(), true, 1, w));
  TEST_ASSERT_TRUE(f.next(s, fake.lookup(), true, kNotFoundMs, w));
}

// The Chart changed the mask between its /api/status and the fetch: the answer's ETag is the new rev. The old rev
// isn't asked for again (a loop till the next status) for a minute; the new one, once status says it, at once.
void test_fetcher_newer_than_asked() {
  Fake fake;
  Fetcher f;
  MapStatus s = status({vis("faa_delays", "hourglass", "3e3d702c")}, true);
  Want w;
  TEST_ASSERT_TRUE(f.next(s, fake.lookup(), true, 0, w));
  f.asked(0);
  IconDone d;
  d.name = "hourglass";
  d.askedRev = "3e3d702c";
  d.rev = "99999999";
  d.ok = true;
  d.saved = true;
  f.answered(d, true, 100);
  fake.saved = {{"hourglass", "99999999"}};
  TEST_ASSERT_FALSE(f.next(s, fake.lookup(), true, 200, w));
  TEST_ASSERT_TRUE(f.next(s, fake.lookup(), true, 100 + kBadAnswerMs, w));
  s.visualizers[0].iconRev = "99999999";  // the next /api/status: it has it
  TEST_ASSERT_FALSE(f.next(s, fake.lookup(), true, 300, w));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_valid_name);
  RUN_TEST(test_valid_rev);
  RUN_TEST(test_etag_rev);
  RUN_TEST(test_icon_file_round_trip);
  RUN_TEST(test_icon_file_rejects);
  RUN_TEST(test_fill_dsc);
  RUN_TEST(test_misses_backoff);
  RUN_TEST(test_misses_unsupported);
  RUN_TEST(test_missing_with_revs);
  RUN_TEST(test_missing_old_chart);
  RUN_TEST(test_fetcher_one_at_a_time);
  RUN_TEST(test_fetcher_gives_up_waiting);
  RUN_TEST(test_fetcher_no_room);
  RUN_TEST(test_fetcher_newer_than_asked);
  return UNITY_END();
}
