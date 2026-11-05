// Firmware updates from GitHub: reading the releases and deciding what's an
// update. Plain C++ and ArduinoJson, unit tested on the host
// (test/test_update_check); src/updater.cpp does the downloading.
//
// The channels are metarmap2's:
//   Releases       the newest release (a tag like v26.11.05), if newer
//   Latest commit  the newest commit of main
//   Branch         the newest commit of a branch
// CI publishes each release's firmware to its GitHub release, and each
// branch's newest firmware to a pre-release tagged build-<branch>.
#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

namespace update {

enum class Channel { Releases, Latest, Branch };

constexpr const char* kRepo = "dman776/metarmap2_remote";
constexpr const char* kAsset = "metarmap_remote-es3c28p.bin";  // the firmware file in each release
constexpr const char* kBuildPrefix = "build-";
constexpr const char* kMainBranch = "main";

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

// Is this release an update for firmware at currentTag (FIRMWARE_TAG, "" before
// the first release) and currentCommit (FIRMWARE_COMMIT)? Releases: a newer
// release (any, before the first). Latest and Branch: another commit.
bool isUpdate(Channel channel, const Release& release, const std::string& currentTag, const std::string& currentCommit);

const char* channelName(Channel channel);  // releases, latest, branch: as saved
Channel channelFromName(const std::string& name);

}  // namespace update
