#include "update_check.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>

namespace update {

namespace {

void assetFields(JsonObject asset) {
  asset["name"] = true;
  asset["browser_download_url"] = true;
  asset["size"] = true;
  asset["digest"] = true;
}

void releaseFields(JsonObject release) {
  release["tag_name"] = true;
  release["name"] = true;
  release["prerelease"] = true;
  release["target_commitish"] = true;
  assetFields(release["assets"].add<JsonObject>());
}

}  // namespace

void releaseFilter(JsonDocument& filter) {
  filter.clear();
  releaseFields(filter.to<JsonObject>());
}

void releaseListFilter(JsonDocument& filter) {
  filter.clear();
  releaseFields(filter.add<JsonObject>());
}

Release parseRelease(JsonVariantConst json) {
  Release r;
  r.tag = json["tag_name"] | "";
  // A release's title is free text ("Initial release"): show its tag. A build's
  // title is CI's: that build's FIRMWARE_VERSION.
  std::string name = json["name"] | "";
  bool build = r.tag.rfind(kBuildPrefix, 0) == 0;
  r.version = build && !name.empty() ? name : r.tag;
  r.prerelease = json["prerelease"] | false;
  if (r.tag.rfind(kBuildPrefix, 0) == 0) r.commit = json["target_commitish"] | "";
  for (JsonVariantConst asset : json["assets"].as<JsonArrayConst>()) {
    if (strcmp(asset["name"] | "", kAsset) != 0) continue;
    r.url = asset["browser_download_url"] | "";
    r.size = asset["size"] | 0u;
    std::string digest = asset["digest"] | "";
    if (digest.rfind("sha256:", 0) == 0) r.sha256 = digest.substr(7);
  }
  return r;
}

std::vector<Release> parseReleases(JsonVariantConst list) {
  std::vector<Release> out;
  for (JsonVariantConst json : list.as<JsonArrayConst>()) out.push_back(parseRelease(json));
  return out;
}

std::vector<int> versionParts(const std::string& tag) {
  if (tag.size() < 2 || tag[0] != 'v' || !isdigit((unsigned char)tag[1])) return {};
  std::vector<int> parts;
  int n = 0;
  bool inNumber = false;
  for (size_t i = 1; i < tag.size(); i++) {
    char c = tag[i];
    if (isdigit((unsigned char)c)) {
      n = n * 10 + (c - '0');
      inNumber = true;
    } else if (c == '.' && inNumber) {
      parts.push_back(n);
      n = 0;
      inNumber = false;
    } else {
      return {};  // v26.11.05-rc1 and the like aren't releases
    }
  }
  if (!inNumber) return {};
  parts.push_back(n);
  return parts;
}

std::string buildTag(const std::string& branch) {
  std::string tag = kBuildPrefix;
  for (char c : branch) tag += (isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-') ? c : '-';
  return tag;
}

std::vector<std::string> buildBranches(const std::vector<Release>& releases) {
  std::vector<std::string> branches;
  size_t prefix = strlen(kBuildPrefix);
  for (const Release& r : releases) {
    if (!r.prerelease || r.url.empty() || r.tag.rfind(kBuildPrefix, 0) != 0) continue;
    branches.push_back(r.tag.substr(prefix));
  }
  std::sort(branches.begin(), branches.end());
  return branches;
}

bool isUpdate(Channel channel, const Release& release, const std::string& currentTag,
              const std::string& currentCommit) {
  if (release.url.empty()) return false;  // nothing to install
  if (channel == Channel::Releases || channel == Channel::Release) {
    std::vector<int> candidate = versionParts(release.tag);
    if (candidate.empty() || release.prerelease) return false;
    std::vector<int> current = versionParts(currentTag);
    return current.empty() || candidate > current;
  }
  if (release.commit.size() < 7 || currentCommit.size() < 7) return !release.commit.empty();
  return release.commit.compare(0, 7, currentCommit, 0, 7) != 0;
}

namespace {

// A release that can be installed: the firmware file, a release tag, not a pre-release.
bool installableRelease(const Release& r) { return !r.url.empty() && !r.prerelease && !versionParts(r.tag).empty(); }

}  // namespace

std::vector<std::string> releaseTags(const std::vector<Release>& releases, size_t max) {
  std::vector<Release> usable;
  std::copy_if(releases.begin(), releases.end(), std::back_inserter(usable), installableRelease);
  std::stable_sort(usable.begin(), usable.end(),
                   [](const Release& a, const Release& b) { return versionParts(a.tag) > versionParts(b.tag); });
  std::vector<std::string> tags;
  for (const Release& r : usable) {
    if (tags.size() >= max) break;
    if (std::find(tags.begin(), tags.end(), r.tag) == tags.end()) tags.push_back(r.tag);
  }
  return tags;
}

Release findRelease(const std::vector<Release>& releases, const std::string& tag) {
  auto at = std::find_if(releases.begin(), releases.end(),
                         [&](const Release& r) { return r.tag == tag && installableRelease(r); });
  return at == releases.end() ? Release() : *at;
}

Age ageOf(const std::string& tag, const std::string& currentTag, const std::string& currentVersion) {
  std::vector<int> candidate = versionParts(tag);
  if (candidate.empty()) return Age::Unknown;
  std::vector<int> current = versionParts(currentTag);
  if (current.empty() || candidate > current) return Age::Newer;  // before the first release, any is newer
  if (candidate < current) return Age::Older;
  return currentVersion == currentTag ? Age::Same : Age::Older;  // commits after the tag are newer than it
}

const char* ageText(Age age) {
  switch (age) {
    case Age::Older:
      return "older than installed";
    case Age::Same:
      return "same as installed";
    case Age::Newer:
      return "newer than installed";
    default:
      return "";
  }
}

bool canInstall(Channel channel, const Release& release, const std::string& currentTag,
                const std::string& currentVersion, const std::string& currentCommit) {
  if (channel != Channel::Release) return isUpdate(channel, release, currentTag, currentCommit);
  if (!installableRelease(release)) return false;
  Age age = ageOf(release.tag, currentTag, currentVersion);
  return age == Age::Older || age == Age::Newer;
}

const char* channelName(Channel channel) {
  switch (channel) {
    case Channel::Latest:
      return "latest";
    case Channel::Branch:
      return "branch";
    case Channel::Release:
      return "release";
    default:
      return "releases";
  }
}

Channel channelFromName(const std::string& name) {
  if (name == "latest") return Channel::Latest;
  if (name == "branch") return Channel::Branch;
  if (name == "release") return Channel::Release;
  return Channel::Releases;
}

}  // namespace update
