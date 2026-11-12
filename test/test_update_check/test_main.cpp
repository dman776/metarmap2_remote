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
  {"tag_name": "v26.11.06", "name": "Initial release", "draft": false, "prerelease": false,
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
  TEST_ASSERT_EQUAL_STRING("v26.11.06", r.version.c_str());  // the tag, not the title
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
  for (Channel c : {Channel::Releases, Channel::Latest, Channel::Branch, Channel::Release}) {
    TEST_ASSERT_TRUE(c == update::channelFromName(update::channelName(c)));
  }
  // As saved: the names older firmware knows don't change; Pick release is "release", not "releases".
  TEST_ASSERT_EQUAL_STRING("releases", update::channelName(Channel::Releases));
  TEST_ASSERT_EQUAL_STRING("latest", update::channelName(Channel::Latest));
  TEST_ASSERT_EQUAL_STRING("branch", update::channelName(Channel::Branch));
  TEST_ASSERT_EQUAL_STRING("release", update::channelName(Channel::Release));
  TEST_ASSERT_TRUE(Channel::Releases == update::channelFromName(""));
  TEST_ASSERT_TRUE(Channel::Releases == update::channelFromName("nonsense"));
  // The screen's chips are in the enum's order.
  TEST_ASSERT_EQUAL_INT(0, (int)Channel::Releases);
  TEST_ASSERT_EQUAL_INT(3, (int)Channel::Release);
}

// ---- Pick release ----

// GitHub's /releases, as this repo's are: newest first by date, releases from v26.11.08 on with a factory image
// beside the firmware, branch builds, a release whose firmware file is missing (or named otherwise), a
// pre-release with a release-like tag, and a draft-like rc.
const char* kHistory = R"json([
  {"tag_name": "build-stream-no-busy-wait", "name": "v26.11.10+11 91dbaad", "prerelease": true,
   "target_commitish": "91dbaad0000000000000000000000000000000000",
   "assets": [{"name": "metarmap_remote-es3c28p-factory.bin", "size": 4194304, "browser_download_url": "https://x/f.bin"},
              {"name": "metarmap_remote-es3c28p.bin", "size": 1200000, "browser_download_url": "https://x/b.bin"}]},
  {"tag_name": "v26.11.10", "name": "v26.11.10", "prerelease": false, "target_commitish": "main",
   "assets": [{"name": "metarmap_remote-es3c28p-factory.bin", "size": 4194304, "browser_download_url": "https://x/10f.bin"},
              {"name": "metarmap_remote-es3c28p.bin", "size": 1190000, "digest": "sha256:aa",
               "browser_download_url": "https://github.com/dman776/metarmap2_remote/releases/download/v26.11.10/metarmap_remote-es3c28p.bin"}]},
  {"tag_name": "v26.11.11", "name": "", "prerelease": true, "target_commitish": "main",
   "assets": [{"name": "metarmap_remote-es3c28p.bin", "size": 1, "browser_download_url": "https://x/pre.bin"}]},
  {"tag_name": "v26.11.08", "name": "", "prerelease": false, "target_commitish": "main",
   "assets": [{"name": "metarmap_remote-es3c28p.bin", "size": 1180000, "browser_download_url": "https://x/08.bin"}]},
  {"tag_name": "v26.11.07.9", "name": "no firmware", "prerelease": false, "target_commitish": "main",
   "assets": [{"name": "metarmap_remote-es3c28p-factory.bin", "size": 4194304, "browser_download_url": "https://x/f.bin"}]},
  {"tag_name": "v26.11.07", "name": "", "prerelease": false, "target_commitish": "main",
   "assets": [{"name": "metarmap_remote-es3c28p.bin", "size": 1170000, "browser_download_url": "https://x/07.bin"}]},
  {"tag_name": "v26.11.06.1", "name": "", "prerelease": false, "target_commitish": "main",
   "assets": [{"name": "metarmap_remote-es3c28p.bin", "size": 1, "browser_download_url": "https://x/061.bin"}]},
  {"tag_name": "v26.11.06", "name": "", "prerelease": false, "target_commitish": "main",
   "assets": [{"name": "metarmap_remote-es3c28p.bin", "size": 1, "browser_download_url": "https://x/06.bin"}]},
  {"tag_name": "v26.12.01-rc1", "name": "", "prerelease": false, "target_commitish": "main",
   "assets": [{"name": "metarmap_remote-es3c28p.bin", "size": 1, "browser_download_url": "https://x/rc.bin"}]},
  {"tag_name": "v26.11.05.5", "name": "", "prerelease": false, "target_commitish": "main",
   "assets": [{"name": "metarmap_remote-es3c28p.bin", "size": 1, "browser_download_url": "https://x/055.bin"}]},
  {"tag_name": "v26.11.05.10", "name": "", "prerelease": false, "target_commitish": "main",
   "assets": [{"name": "metarmap_remote-es3c28p.bin", "size": 1, "browser_download_url": "https://x/0510.bin"}]},
  {"tag_name": "build-main", "name": "v26.11.10+9 fecf3ab", "prerelease": true, "target_commitish": "fecf3ab",
   "assets": [{"name": "metarmap_remote-es3c28p.bin", "size": 1, "browser_download_url": "https://x/m.bin"}]}
])json";

std::vector<Release> history() {
  JsonDocument filter;
  update::releaseListFilter(filter);
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, kHistory, DeserializationOption::Filter(filter)));
  return update::parseReleases(doc.as<JsonVariantConst>());
}

void test_release_tags() {
  // Releases with the firmware file, newest first by version (not GitHub's order): no build-*, no
  // pre-release, no rc, not the one with only a factory image.
  std::vector<std::string> tags = update::releaseTags(history());
  const char* want[] = {"v26.11.10", "v26.11.08",    "v26.11.07",  "v26.11.06.1",
                        "v26.11.06", "v26.11.05.10", "v26.11.05.5"};
  TEST_ASSERT_EQUAL_UINT(7, tags.size());
  for (size_t i = 0; i < tags.size(); i++) TEST_ASSERT_EQUAL_STRING(want[i], tags[i].c_str());
}

void test_release_tags_at_most() {
  std::vector<std::string> tags = update::releaseTags(history(), 3);
  TEST_ASSERT_EQUAL_UINT(3, tags.size());
  TEST_ASSERT_EQUAL_STRING("v26.11.07", tags[2].c_str());
  TEST_ASSERT_TRUE(update::releaseTags({}).empty());
}

void test_release_firmware_not_factory() {
  // A release with both: the update installs the app, never the factory image.
  Release r = update::findRelease(history(), "v26.11.10");
  TEST_ASSERT_EQUAL_STRING(
      "https://github.com/dman776/metarmap2_remote/releases/download/v26.11.10/metarmap_remote-es3c28p.bin",
      r.url.c_str());
  TEST_ASSERT_EQUAL_UINT32(1190000, r.size);
  TEST_ASSERT_EQUAL_STRING("aa", r.sha256.c_str());
}

void test_find_release() {
  std::vector<Release> list = history();
  TEST_ASSERT_EQUAL_STRING("https://x/07.bin", update::findRelease(list, "v26.11.07").url.c_str());
  TEST_ASSERT_TRUE(update::findRelease(list, "v26.11.07.9").tag.empty());  // no firmware file
  TEST_ASSERT_TRUE(update::findRelease(list, "v26.11.11").tag.empty());    // a pre-release
  TEST_ASSERT_TRUE(update::findRelease(list, "build-main").tag.empty());
  TEST_ASSERT_TRUE(update::findRelease(list, "v26.01.01").tag.empty());  // not on GitHub
  TEST_ASSERT_TRUE(update::findRelease(list, "").tag.empty());
}

void test_age() {
  using update::Age;
  using update::ageOf;
  TEST_ASSERT_TRUE(Age::Older == ageOf("v26.11.07", "v26.11.10", "v26.11.10"));
  TEST_ASSERT_TRUE(Age::Newer == ageOf("v26.11.10", "v26.11.07", "v26.11.07"));
  TEST_ASSERT_TRUE(Age::Same == ageOf("v26.11.10", "v26.11.10", "v26.11.10"));
  TEST_ASSERT_TRUE(Age::Newer == ageOf("v26.11.05.1", "v26.11.05", "v26.11.05"));  // a second release that day
  TEST_ASSERT_TRUE(Age::Older == ageOf("v26.11.05", "v26.11.05.1", "v26.11.05.1"));
  TEST_ASSERT_TRUE(Age::Older == ageOf("v26.11.05.5", "v26.11.05.10", "v26.11.05.10"));  // by number
  // Commits after a release are newer than it: its own tag is older.
  TEST_ASSERT_TRUE(Age::Older == ageOf("v26.11.10", "v26.11.10", "v26.11.10+11 91dbaad"));
  TEST_ASSERT_TRUE(Age::Older == ageOf("v26.11.10", "v26.11.10", "v26.11.10 modified"));
  TEST_ASSERT_TRUE(Age::Newer == ageOf("v26.11.11", "v26.11.10", "v26.11.10+11 91dbaad"));
  // Before the first release (FIRMWARE_TAG ""), any release is newer.
  TEST_ASSERT_TRUE(Age::Newer == ageOf("v26.11.05", "", "abc1234"));
  TEST_ASSERT_TRUE(Age::Unknown == ageOf("build-main", "v26.11.10", "v26.11.10"));
  TEST_ASSERT_TRUE(Age::Unknown == ageOf("", "v26.11.10", "v26.11.10"));
  TEST_ASSERT_EQUAL_STRING("older than installed", update::ageText(Age::Older));
  TEST_ASSERT_EQUAL_STRING("same as installed", update::ageText(Age::Same));
  TEST_ASSERT_EQUAL_STRING("newer than installed", update::ageText(Age::Newer));
  TEST_ASSERT_EQUAL_STRING("", update::ageText(Age::Unknown));
}

void test_pick_release_installs_older() {
  Channel c = Channel::Release;
  // Older: installable (Pick release installs exactly that tag), but not an update (no star).
  TEST_ASSERT_TRUE(update::canInstall(c, release("v26.11.07"), "v26.11.10", "v26.11.10", "aaaaaaa"));
  TEST_ASSERT_FALSE(update::isUpdate(c, release("v26.11.07"), "v26.11.10", "aaaaaaa"));
  // Newer: both.
  TEST_ASSERT_TRUE(update::canInstall(c, release("v26.11.10"), "v26.11.07", "v26.11.07", "aaaaaaa"));
  TEST_ASSERT_TRUE(update::isUpdate(c, release("v26.11.10"), "v26.11.07", "aaaaaaa"));
  // The installed one: nothing to do.
  TEST_ASSERT_FALSE(update::canInstall(c, release("v26.11.10"), "v26.11.10", "v26.11.10", "aaaaaaa"));
  // Commits after v26.11.10: v26.11.10 itself goes back to the release.
  TEST_ASSERT_TRUE(update::canInstall(c, release("v26.11.10"), "v26.11.10", "v26.11.10+11 91dbaad", "91dbaad"));
  // Not a release, or no file.
  TEST_ASSERT_FALSE(update::canInstall(c, Release(), "v26.11.10", "v26.11.10", "aaaaaaa"));
  TEST_ASSERT_FALSE(update::canInstall(c, release("v26.11.11", "", true), "v26.11.10", "v26.11.10", "aaaaaaa"));
  TEST_ASSERT_FALSE(update::canInstall(c, release("build-main", "1a2b3c4", true), "v26.11.10", "v26.11.10", "x"));
  Release noFile = release("v26.11.07");
  noFile.url = "";
  TEST_ASSERT_FALSE(update::canInstall(c, noFile, "v26.11.10", "v26.11.10", "aaaaaaa"));
}

void test_other_channels_install_updates_only() {
  // Releases never goes back: canInstall is isUpdate there.
  TEST_ASSERT_FALSE(update::canInstall(Channel::Releases, release("v26.11.07"), "v26.11.10", "v26.11.10", "a"));
  TEST_ASSERT_TRUE(update::canInstall(Channel::Releases, release("v26.11.11"), "v26.11.10", "v26.11.10", "a"));
  Release b = release("build-main", "1a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d", true);
  TEST_ASSERT_FALSE(update::canInstall(Channel::Latest, b, "v26.11.10", "v26.11.10", "1a2b3c4"));
  TEST_ASSERT_TRUE(update::canInstall(Channel::Branch, b, "v26.11.10", "v26.11.10", "9f8e7d6"));
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
  RUN_TEST(test_release_tags);
  RUN_TEST(test_release_tags_at_most);
  RUN_TEST(test_release_firmware_not_factory);
  RUN_TEST(test_find_release);
  RUN_TEST(test_age);
  RUN_TEST(test_pick_release_installs_older);
  RUN_TEST(test_other_channels_install_updates_only);
  return UNITY_END();
}
