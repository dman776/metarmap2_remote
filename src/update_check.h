// Firmware updates from GitHub: reading the releases and deciding what's an
// update. Plain C++ and ArduinoJson, unit tested on the host
// (test/test_update_check); src/updater.cpp does the downloading.
//
// The channels are metarmap2's:
//   Releases       the newest release (a tag like v26.11.05), if newer
//   Latest commit  the newest commit of main
//   Branch         the newest commit of a branch
// and one more:
//   Pick release   one release you choose, older or newer than the installed one
// CI publishes each release's firmware to its GitHub release, and each
// branch's newest firmware to a pre-release tagged build-<branch>.
#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace update {

// Release is last: the screen's chips are in this order, and the older ones keep their numbers.
enum class Channel { Releases, Latest, Branch, Release };

// A release against the installed firmware.
enum class Age { Unknown, Older, Same, Newer };

constexpr const char* kRepo = "dman776/metarmap2_remote";
// The firmware file in each release (every release since the first, v26.11.05, has it by this name; from v26.11.08
// on they also carry metarmap_remote-es3c28p-factory.bin, the whole flash, which isn't for updating).
constexpr const char* kAsset = "metarmap_remote-es3c28p.bin";
constexpr const char* kBuildPrefix = "build-";
constexpr const char* kMainBranch = "main";
constexpr size_t kMaxReleases = 15;  // how many the Pick release list offers, newest first

struct Release {
  std::string tag;      // v26.11.05, or build-<branch>
  std::string version;  // what the screens show: the tag; for a build, its title (the build's FIRMWARE_VERSION)
  std::string commit;   // the build's commit, for build-<branch> pre-releases ("" for releases)
  bool prerelease = false;
  // The firmware file; empty url when the release has none.
  std::string url;
  uint32_t size = 0;
  std::string sha256;  // hex, from GitHub's digest; "" when GitHub doesn't give one
};

// ArduinoJson filters for GitHub's release JSON: one release, or a list.
void releaseFilter(JsonDocument& filter);
void releaseListFilter(JsonDocument& filter);

Release parseRelease(JsonVariantConst release);
std::vector<Release> parseReleases(JsonVariantConst list);

// "v26.11.05" -> {26, 11, 5}; empty for anything that isn't a release tag.
std::vector<int> versionParts(const std::string& tag);

// The build-<branch> tag for a branch: "/" and anything else unusual become "-".
std::string buildTag(const std::string& branch);

// The branches with a firmware build, from a list of releases.
std::vector<std::string> buildBranches(const std::vector<Release>& releases);

// The releases Pick release offers: those with the firmware file (not pre-releases, not build-*, tags like
// v26.11.07), newest first, at most `max`.
std::vector<std::string> releaseTags(const std::vector<Release>& releases, size_t max = kMaxReleases);

// The release tagged `tag`, if it can be installed (the checks releaseTags makes); an empty Release otherwise.
Release findRelease(const std::vector<Release>& releases, const std::string& tag);

// A release's tag against the installed firmware: currentTag (FIRMWARE_TAG, "" before the first release) and
// currentVersion (FIRMWARE_VERSION). Firmware a few commits after v26.11.05 (v26.11.05+3 abc1234) is newer than
// v26.11.05, so that's Older; only firmware built at the tag itself is the Same.
Age ageOf(const std::string& tag, const std::string& currentTag, const std::string& currentVersion);
const char* ageText(Age age);  // "older than installed"...; "" for Unknown

// Is this release an update for firmware at currentTag (FIRMWARE_TAG, "" before
// the first release) and currentCommit (FIRMWARE_COMMIT)? Releases and Release:
// a newer release (any, before the first). Latest and Branch: another commit.
// It's what the star and the splash's Update button show.
bool isUpdate(Channel channel, const Release& release, const std::string& currentTag, const std::string& currentCommit);

// Can UPDATE install it? On Release, any release with the firmware file that isn't the installed one (older ones
// too); on the others, an update (isUpdate).
bool canInstall(Channel channel, const Release& release, const std::string& currentTag,
                const std::string& currentVersion, const std::string& currentCommit);

const char* channelName(Channel channel);  // releases, latest, branch, release: as saved
Channel channelFromName(const std::string& name);

}  // namespace update
