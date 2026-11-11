#include "skips.h"

#include <algorithm>
#include <iterator>

namespace skips {

namespace {

// a is b or older, with the numbers wrapping round
bool upTo(uint32_t a, uint32_t b) { return (int32_t)(a - b) <= 0; }

}  // namespace

void Marks::polled(bool known, const std::vector<std::string>& ids, uint32_t now) {
  if (settling_ && now - settledAt_ < kSettleMs) return;
  settling_ = false;
  if (known == known_ && ids == base_) return;
  known_ = known;
  base_ = ids;
  revision_++;  // known_ may have changed with shown_ the same
  recompute();
}

Hold Marks::hold(bool exclusive) const {
  if (!known_) return Hold::OldChart;
  if (exclusive) return Hold::Exclusive;
  return Hold::Ask;
}

void Marks::asked(uint32_t seq, const std::string& id, bool skip, uint32_t now) {
  if (!seq) return;  // not queued: no answer will come, so nothing to wait for or mark
  pending_.push_back({seq, id, skip, now});
  recompute();
}

Said Marks::answered(const SkipAnswer& a, uint32_t now) {
  bool mine = std::any_of(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.seq == a.seq; });
  pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const Pending& p) { return upTo(p.seq, a.seq); }),
                 pending_.end());
  if (a.ok) {
    if (!known_) revision_++;
    known_ = true;
    base_ = a.skip;  // the Chart's whole list after it: the truth, whoever's hold it was
    settling_ = true;
    settledAt_ = now;
    recompute();
    return a.skipped ? Said::Skipped : Said::BackInRotation;
  }
  recompute();  // the failed hold's mark is undone; others still waiting stay
  if (!mine) return Said::Nothing;
  if (a.reason == "last") return Said::Last;
  if (a.reason == "exclusive") return Said::Exclusive;
  return Said::Failed;
}

std::vector<std::string> Marks::expire(uint32_t now) {
  // the ones still waiting first, in order; then the late ones, in order
  auto late =
      std::stable_partition(pending_.begin(), pending_.end(), [&](const Pending& p) { return now - p.at < kWaitMs; });
  std::vector<std::string> ids;
  std::transform(late, pending_.end(), std::back_inserter(ids), [](const Pending& p) { return p.id; });
  if (ids.empty()) return ids;
  pending_.erase(late, pending_.end());
  recompute();
  return ids;
}

bool Marks::isSkipped(const std::string& id) const {
  return std::find(shown_.begin(), shown_.end(), id) != shown_.end();
}

void Marks::recompute() {
  std::vector<std::string> ids = base_;
  for (const Pending& p : pending_) {
    bool has = std::find(ids.begin(), ids.end(), p.id) != ids.end();
    if (p.skip && !has) ids.push_back(p.id);
    if (!p.skip && has) ids.erase(std::remove(ids.begin(), ids.end(), p.id), ids.end());
  }
  if (ids == shown_) return;
  shown_ = std::move(ids);
  revision_++;
}

}  // namespace skips
