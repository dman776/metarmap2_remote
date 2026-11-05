// Reading GitHub's releases and deciding what's a firmware update.
#include <ArduinoJson.h>
#include <unity.h>

#include "update_check.h"

using update::Channel;
using update::Release;

namespace {

// GitHub's /releases JSON (fields as the API gives them), trimmed: a release,
// a branch build, one without the firmware file, and an unrelated pre-release.
const char* kReleases = R"json([
  {"tag_name": "v26.11.06", "name": "v26.11.06", "draft": false, "prerelease": false,
   "target_commitish": "main", "body": "Release notes...",
   "assets": [
     {"name": "notes.txt", "size": 10, "browser_download_url": "https://github.com/x/notes.txt"},
     {"name": "metarmap_remote-es3c28p.bin", "size": 1153024,
      "digest": "sha256:f756e7859d90dbb5e85aae5194206c37da220bec9bac23d3d659078a8e766fdc",
      "browser_download_url": "https://github.com/dman776/metarmap2_remote/releases/download/v26.11.06/metarmap_remote-es3c28p.bin"}
   ]},
  {"tag_name": "build-main", "name": "v26.11.06+2 1a2b3c4", "prerelease": true,
   "target_commitish": "1a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d",
   "assets": [{"name": "metarmap_remote-es3c28p.bin", "size": 1153100,
               "browser_download_url": "https://github.com/dman776/metarmap2_remote/releases/download/build-main/metarmap_remote-es3c28p.bin"}]},
  {"tag_name": "build-feature-radar", "name": "abc1234", "prerelease": true,
   "target_commitish": "abc1234ffffffffffffffffffffffffffffffff",
   "assets": [{"name": "metarmap_remote-es3c28p.bin", "size": 1,
               "browser_download_url": "https://github.com/x/build-feature-radar.bin"}]},
  {"tag_name": "build-empty", "name": "", "prerelease": true, "target_commitish": "deadbee", "assets": []},
  {"tag_name": "v26.12.01-rc1", "name": "", "prerelease": true, "target_commitish": "main", "assets": []}
])json";

std::vector<Release> releases() {
  JsonDocument filter;
  update::releaseListFilter(filter);
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, kReleases, DeserializationOption::Filter(filter)));
  return update::parseReleases(doc.as<JsonVariantConst>());
}

Release release(const char* tag, const char* commit = "", bool pre = false) {
  Release r;
  r.tag = tag;
  r.commit = commit;
  r.prerelease = pre;
  r.url = "https://example/fw.bin";
  return r;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_parse_release() {
  std::vector<Release> list = releases();
  TEST_ASSERT_EQUAL_UINT(5, list.size());
  const Release& r = list[0];
  TEST_ASSERT_EQUAL_STRING("v26.11.06", r.tag.c_str());
  TEST_ASSERT_EQUAL_STRING("v26.11.06", r.version.c_str());
  TEST_ASSERT_FALSE(r.prerelease);
  TEST_ASSERT_EQUAL_STRING("", r.commit.c_str());  // only builds carry a commit
  // The firmware file, not the other asset.
  TEST_ASSERT_EQUAL_STRING(
      "https://github.com/dman776/metarmap2_remote/releases/download/v26.11.06/metarmap_remote-es3c28p.bin",
      r.url.c_str());
  TEST_ASSERT_EQUAL_UINT32(1153024, r.size);
  TEST_ASSERT_EQUAL_STRING("f756e7859d90dbb5e85aae5194206c37da220bec9bac23d3d659078a8e766fdc", r.sha256.c_str());
}

void test_parse_build() {
  Release b = releases()[1];  // a copy: the list is a temporary
  TEST_ASSERT_TRUE(b.prerelease);
  TEST_ASSERT_EQUAL_STRING("v26.11.06+2 1a2b3c4", b.version.c_str());  // the build's version, as its title
  TEST_ASSERT_EQUAL_STRING("1a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d", b.commit.c_str());
  TEST_ASSERT_EQUAL_STRING("", b.sha256.c_str());  // no digest given
}

void test_untitled_release_uses_its_tag() { TEST_ASSERT_EQUAL_STRING("build-empty", releases()[3].version.c_str()); }

void test_no_firmware_file() { TEST_ASSERT_EQUAL_STRING("", releases()[3].url.c_str()); }

void test_build_branches() {
  // Only pre-releases tagged build-* with the firmware file; sorted.
  std::vector<std::string> b = update::buildBranches(releases());
  TEST_ASSERT_EQUAL_UINT(2, b.size());
  TEST_ASSERT_EQUAL_STRING("feature-radar", b[0].c_str());
  TEST_ASSERT_EQUAL_STRING("main", b[1].c_str());
}

void test_build_tag() {
  TEST_ASSERT_EQUAL_STRING("build-main", update::buildTag("main").c_str());
  TEST_ASSERT_EQUAL_STRING("build-feature-radar", update::buildTag("feature/radar").c_str());
  TEST_ASSERT_EQUAL_STRING("build-fix_1.2", update::buildTag("fix_1.2").c_str());
}

void test_version_parts() {
  TEST_ASSERT_EQUAL_UINT(3, update::versionParts("v26.11.05").size());
  TEST_ASSERT_EQUAL_INT(5, update::versionParts("v26.11.05")[2]);
  TEST_ASSERT_EQUAL_INT(2, update::versionParts("v3.2.0")[1]);
  TEST_ASSERT_TRUE(update::versionParts("").empty());
  TEST_ASSERT_TRUE(update::versionParts("26.11.05").empty());
  TEST_ASSERT_TRUE(update::versionParts("v26.11.05-rc1").empty());
  TEST_ASSERT_TRUE(update::versionParts("v26..05").empty());
  TEST_ASSERT_TRUE(update::versionParts("v26.11.").empty());
  TEST_ASSERT_TRUE(update::versionParts("build-main").empty());
}

void test_release_channel() {
  Channel c = Channel::Releases;
  TEST_ASSERT_TRUE(update::isUpdate(c, release("v26.11.06"), "v26.11.05", "aaaaaaa"));
  TEST_ASSERT_TRUE(update::isUpdate(c, release("v26.12.01"), "v26.11.30", "aaaaaaa"));  // by number, not text
  TEST_ASSERT_TRUE(update::isUpdate(c, release("v26.11.10"), "v26.11.9", "aaaaaaa"));
  // A second release the same day (metarmap2's .1, .2...).
  TEST_ASSERT_TRUE(update::isUpdate(c, release("v26.11.05.1"), "v26.11.05", "aaaaaaa"));
  TEST_ASSERT_TRUE(update::isUpdate(c, release("v26.11.05.2"), "v26.11.05.1", "aaaaaaa"));
  TEST_ASSERT_TRUE(update::isUpdate(c, release("v26.11.06"), "v26.11.05.1", "aaaaaaa"));
  TEST_ASSERT_FALSE(update::isUpdate(c, release("v26.11.05"), "v26.11.05.1", "aaaaaaa"));
  TEST_ASSERT_FALSE(update::isUpdate(c, release("v26.11.05"), "v26.11.05", "aaaaaaa"));  // the same
  TEST_ASSERT_FALSE(update::isUpdate(c, release("v26.11.04"), "v26.11.05", "aaaaaaa"));  // older
  // Commits after v26.11.05 run FIRMWARE_TAG v26.11.05: v26.11.05 isn't newer.
  TEST_ASSERT_FALSE(update::isUpdate(c, release("v26.11.05"), "v26.11.05", "1234567"));
  // Before the first release, any release is.
  TEST_ASSERT_TRUE(update::isUpdate(c, release("v26.11.05"), "", "1234567"));
  // Not a release.
  TEST_ASSERT_FALSE(update::isUpdate(c, release("v26.12.01-rc1", "", true), "v26.11.05", "aaaaaaa"));
  TEST_ASSERT_FALSE(update::isUpdate(c, release("v26.12.01", "", true), "v26.11.05", "aaaaaaa"));
}

void test_commit_channels() {
  for (Channel c : {Channel::Latest, Channel::Branch}) {
    Release b = release("build-main", "1a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d", true);
    TEST_ASSERT_FALSE(update::isUpdate(c, b, "v26.11.05", "1a2b3c4"));  // running that commit
    TEST_ASSERT_TRUE(update::isUpdate(c, b, "v26.11.05", "9f8e7d6"));
    TEST_ASSERT_TRUE(update::isUpdate(c, b, "", "9f8e7d6"));
  }
}

void test_nothing_to_install() {
  Release r = release("v26.11.06");
  r.url = "";
  TEST_ASSERT_FALSE(update::isUpdate(Channel::Releases, r, "v26.11.05", "aaaaaaa"));
}

void test_channel_names() {
  for (Channel c : {Channel::Releases, Channel::Latest, Channel::Branch}) {
    TEST_ASSERT_TRUE(c == update::channelFromName(update::channelName(c)));
  }
  TEST_ASSERT_TRUE(Channel::Releases == update::channelFromName(""));
  TEST_ASSERT_TRUE(Channel::Releases == update::channelFromName("nonsense"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_release);
  RUN_TEST(test_parse_build);
  RUN_TEST(test_untitled_release_uses_its_tag);
  RUN_TEST(test_no_firmware_file);
  RUN_TEST(test_build_branches);
  RUN_TEST(test_build_tag);
  RUN_TEST(test_version_parts);
  RUN_TEST(test_release_channel);
  RUN_TEST(test_commit_channels);
  RUN_TEST(test_nothing_to_install);
  RUN_TEST(test_channel_names);
  return UNITY_END();
}
